# #146 — Title moon "too big": quantified diagnosis (fix NOT a scale constant)

User playtest report: the title-screen moon is too big vs OoT3D.

## Ground truth (Az / OoT3D, from oot3d-decomp docs/title_moon_composition.md)

3-layer composite on the 400×240 top screen at settled title:
- disc `fine_moon0` (128×128, alpha crescent): screen bbox (377,0)-(480,91) → **103×91**, ~25.8% of width, edge-clipped top-right.
- inner glow `fine_moon1` (64×64, additive): 188×133.
- outer glow `fine_moon2` (64×64, additive): 200×139.

## Measured (harness Az-vs-SoH SxS, same cs frame 100, 400×240)

`scratch/measure_moon.py` + a lum-threshold sweep on
`scratch/screenshots/moon_f100.{az,soh}.ppm`:

| | Az | SoH |
|---|---|---|
| bright footprint (lum≥100) | 180×95, **752 px** | 117×138, **13625 px** |
| bright core (lum≥150) | 81×18 @ x220-300 | 94×128 @ x306-399 |
| peak (lum≥200) | present (81×18) | **none** |

Three distinct divergences, not one:

1. **Size.** SoH footprint 138 px tall vs Az 95 → SoH is **≥1.45× too tall**,
   and SoH's is CLIPPED (top y=0, right x=399), so the true oversize is larger.
2. **Fill / opacity.** SoH has **18× more bright pixels** (13625 vs 752). Az's
   moon is a thin crescent + faint halo (halo textures have transparent centres,
   drawn dim); SoH renders a big solid bright blob. SoH's additive halo layers
   (1.65× and 1.85× the disc, drawn at full moon alpha `aA`) are far too
   bright/large — this is most of the "too big" the user sees.
3. **Position.** Az's bright core is at x220-300 (mid-right); SoH's is at
   x306-399 (jammed in the clipped far corner), ~90 px further right and higher.

## Root cause (why it is NOT a one-line scale tweak)

SoH draws the moon via the **dynamic environment sun/moon path**
(`Zelda3D_TryDrawSunMoon`, zelda3d.c): moon world pos = `eye − sunPos`,
`|sunPos| = 120*25 = 3000`, and `scale = (-15*color)+25`, halos ×1.65/×1.85 at
full alpha. Az's title moon is a **fixed-framing composite** baked for the title
shot (see the standing comment in `Zelda3D_TryDrawSunMoon` and
title_moon_composition.md). Because the two use different placement AND different
halo compositing, SoH's moon differs from Az's in position, size, and fill
simultaneously. Matching Az means matching its FRAMING (screen position + disc
angular size + halo alpha/scale), not just multiplying one `scale`.

A scale-only reduction would make the disc smaller at frame 100 while leaving the
wrong position and the over-bright halos — i.e. tuning one number to "look right"
at one frame, which is the bandaid the project rules forbid.

## The correct fix (derivation, for the next pass)

Two sub-parts, both quantitative:

1. **Disc angular size.** Az disc = 91 px on 240 at vertical FOV 35° (shot-0 fov)
   → subtends (91/240)*35° = **13.3° vertically**. The SoH disc's on-screen height
   is linear in the draw `scale` at fixed distance, so once an UNCLIPPED SoH disc
   height `H_soh` is measured (needs a cs frame where SoH's moon is fully in
   frame — capture shot-0 across cs 0..300 and pick the centred frame), the disc
   scale correction is `scale *= (13.3° target px) / H_soh`. Do NOT eyeball it —
   compute from the measured px.
2. **Halo alpha + scale.** Az halos are additive with transparent centres and
   read far dimmer than SoH's. Match Az's screen ratios (inner 1.72× wider /1.46×
   taller than disc; outer 1.94×/1.53×) AND reduce the halo alpha so the bright
   footprint matches Az's ~752 px, not 13625. The current full-`aA` additive
   halos are the dominant over-brightness.
3. **Position.** Confirm whether the dynamic `eye − sunPos` placement is close
   enough to Az's baked framing at the shots where the moon is visible, or whether
   the moon must be pinned to Az's title framing (the open RE item the
   `Zelda3D_TryDrawSunMoon` comment already flags). Measure Az's moon screen
   centre across shots 0-2 and compare to SoH's before deciding.

## Status (diagnosis pass — superseded by RESOLVED below)

Diagnosis complete and quantified; NO fix applied this session (a verified fix
needs build+capture iteration + the position decision above, and a scale-only
change would be a bandaid). Card kept in-progress with this analysis + the SxS
measurement posted as evidence. Tooling: `scratch/measure_moon.py`,
`scratch/moon_sxs.py` (harness Az/SoH SxS at an aligned cs frame).

---

## RESOLVED (2026-07-08, follow-up session)

Two ground-truth calibrations against Az, both baked into `Zelda3D_TryDrawSunMoon`.

### Method / new tooling
- The earlier "cs frame 100" A/B was INVALID: the csCtx frame counter does NOT
  map to a stable title shot across boots (Az's demo free-runs vs when SoH boots),
  so at "frame 100" Az showed an interior vignette while SoH showed the moon-field
  shot. Alignment must be by CONTENT, not frame number. `scratch/moon/scan_az.py`
  scans Az's title loop and locates the moon-over-Hyrule-field shot by luminance.
- The moon is a far-plane billboard (fixed angular size, position moves with the
  camera pan), so disc SIZE is comparable across shots via a least-squares
  **circle-fit to the disc edge** at a matched fraction of each image's own peak
  (`scratch/moon/circlefit.py`, `calib.py`). Az disc = **54.6px** (≈25% of the
  240px screen), peak lum **232**, footprint ~5317.
- The draw-log RGBA capture (extended `sw_rasterizer` hook) shows OoT3D draws all
  three moon quads with **vertex colour (0,0,0,0)** — the moon is TEXTURE-ONLY,
  fully opaque, NOT modulated by any time-of-day alpha.

### The two fixes (both derived from Az measurements, not fudged)
1. **Disc size** — `kMoonDiscScale = 0.44`. The disc reused the N64 sprite's
   VTX -31..32 quad*scale, which renders ~125px; OoT3D's moon subtends only ~55px.
   0.44 rescales to Az's angular size.
2. **Disc/halo opacity** — `kMoonDrawAlpha = 220`, decoupled from the N64 night
   fade. The old code modulated the disc by the fade alpha (=191 at the title),
   which the draw-log proves is unfaithful (Az = opaque) and which made the disc
   ~0.75 transparent → washed, dim (peak 177 vs 232) and let the additive halos
   dominate → the "18× too filled" blob. 220 reproduces Az's peak (255 clips
   SoH's brighter-decoded texture to white and loses lunar detail). The N64 fade
   is still used ONLY as the night VISIBILITY gate (`alpha > 0`).

### Verified (baked constants, no env, harness Az-vs-SoH @ title)
| | SoH (fixed) | Az | Δ |
|---|---|---|---|
| disc diameter | 53.4px | 54.6px | 1.2px |
| peak luminance | 237 | 232 | 5 |
| footprint | 5077 | 5317 | 4.5% |

Evidence: `scratch/moon/FINAL_baked_sxs.png` (Az top / SoH bottom, size matches).

### Residual (NOT fixed — different camera pans, minor)
- Halo HUE: Az's additive glow reads greenish-teal, SoH's warm-yellow — a
  fine_moon1/2 colour/decode difference, small.
- Screen POSITION: not compared here (Az and SoH were on different title pans;
  the moon is a far-plane billboard so position tracks the camera). If a
  position divergence remains at a matched pan, it is a camera/dayTime issue,
  separate from size. Left for a matched-shot A/B if the user reports it.


---

## CORRECTION (same day) — recalibrated at content-matched frames: 0.505 / 205

The RESOLVED values above (0.44 / 220) were WRONG because they were measured with
`soh_titlecs 100` forced. `soh_titlecs` is not cosmetic: it drives
`gSaveContext.dayTime`, which sets the sky variant, world-shade lighting AND the
sun/moon base scale/alpha. Forcing an uncalibrated cursor put SoH at a different
time-of-day than the naturally-clocked Az reference frame — so the 0.44 disc match
was against a mis-timed SoH moon. (This same artifact produced a phantom
"night-sky color divergence" — see 2026-07-08-title-sky-color.md.)

**Correct method:** boot from `title_settled.state` + plain `step 40`×N (NO
`soh_titlecs`). At 9×40=360 steps both engines show the same moon-behind-rider
shot (rider pose, hill silhouette, moon position match by eye).

Recalibrated at that matched frame → **kMoonDiscScale = 0.505, kMoonDrawAlpha = 205**.

Final verify (baked constants, no env, content-matched 360-step frame):
| | SoH | Az | Δ |
|---|---|---|---|
| disc diameter | 53.9px | 54.6px | 0.7px |
| peak luminance | 233 | 232 | 1 |
| footprint (>=0.55*peak) | 3724 | 5317 | -30% |

Disc + peak nailed. RESIDUALS (noted, not tuned — a proper fix needs the OoT3D
moon scale-over-time decompiled, per "stop micro-tuning"):
1. Halo GLOW SPREAD ~30% tighter than Az (footprint 3724 vs 5317). SoH uses
   uniform disc*1.65/1.85 halo scales (decomp-doc averages); Az's measured ratios
   are non-uniform (1.72-1.94 wide / 1.46-1.53 tall).
2. Disc size drifts vs Az late in the title camera move: both grow, but SoH
   undershoots Az's growth by ~10% at the shot's end — the N64 dayTime-dependent
   scale (-15*color+25) doesn't track OoT3D's moon scale-over-time.

Evidence: `scratch/moon/146_recal_sxs.png`, `RECAL_sxs.png`.


---

## FAITHFUL PORT (Ghidra/RE-derived, replaces guessed halo constants)

Per user directive "don't hand-tune, port via Ghidra": 4 RE sessions (see
oot3d-decomp/docs/env_sun_moon_draw.md) established the moon is BAKED ASSET data
(no runtime scale formula — static xref from Environment_Update and a JIT
watchpoint on the moon vertex buffers both dead-ended; the vertices arrive via a
bulk asset-decompression memcpy). Ground truth was then read from OoT3D's actual
EXECUTION:

- **Halo scale = exactly 2.0x the disc for BOTH halos** (was guessed 1.65x/1.85x).
  Source: OoT3D's vertex-shader model-matrix uniform registers — disc diagonal
  scale 640, both halos 1280 (byte-exact 2:1). The old asymmetry was DEPTH
  PARALLAX misread as scale: OoT3D sits the 3 quads at different view-Z (disc
  -2684, inner halo -2774 behind, outer halo -2595 front).
- **Draw color/alpha = full white (255,255,255,255), TEXTURE-ONLY** (was guessed
  205). Source: per-pixel TEV combiner probe — primary_color into the Modulate
  stage = opaque white, combined == texture on every pixel. Halos are RGB565
  (falloff baked into RGB, no alpha); disc is RGBA4 (real crescent alpha).

Applied: `kMoonHaloScale = 2.0f` (both halos), halos drawn full-white 255.

### Residuals (documented, NOT hand-tuned away)
1. **Disc alpha still 205 (stopgap).** Faithful is 255, but SoH decodes fine_moon0
   (RGBA4) ~brighter than the asset, so 255 clips the disc to white (peak 255 vs
   Az ~235). 205 matches Az's disc peak. REAL fix = the fine_moon0 decode
   (separate texture-decode bug), not this alpha. Marked STOPGAP in-code.
2. **Depth parallax not reproduced.** SoH far-plane-pins all 3 layers (bit 30);
   OoT3D's per-layer z-offsets give a structured halo ring (Az footprint 5317 vs
   SoH 3722, smooth glow). Reproducing it needs a depth-offset port (or un-pinning
   from the far plane) — deferred.
3. Disc scale 0.505 kept (screen-matched, 54-56px vs Az 54.6). Both engines' discs
   drift in size later in the title move (SoH -15*color+25 vs OoT3D's asset) — a
   separate residual.

Verified (content-matched frame, faithful build): disc 55.6px vs Az 54.6, peak
230 vs 232. Evidence: scratch/moon/146_faithful_sxs.png.

## ADDENDUM 2026-07-08 (main): "fine_moon0 RGBA4 decode brightness bug" is a MISDIAGNOSIS
A parallel RE agent flagged kMoonDiscAlpha=205 as a stopgap for the RGBA4 decode being "too
bright." Checked the actual decode: `pica_texture.cpp:6` `e4(n)=(n<<4)|n` is the STANDARD,
CORRECT 4-bit->8-bit bit-replication (0xF->255, 0x8->136) — exactly what hardware does. There is
NO decode bug. The Az-peak-235 vs SoH-would-clip-255 difference is a RENDER-TIME effect
(additive-blend saturation / compositing), not the texture decoding too bright. Do not "fix" the
decode, and do not re-chase this. Moon is faithful (2.0x halo scale, full-white combiner, dynamic
eye±sunPos — all RE-derived from Azahar hardware-register readback).
