# Fire-glow alpha-ramp per-frame trace + cs-438 content attribution (2026-07-10, measurement-only)

Two scoped measurements off v4's residual list (`2026-07-10-title-arc-closing-measurement-v4.md`
residuals 2 and 4). **No code changed** — this is a pure measurement/attribution session;
`git status` is clean except this journal file.

## 1. Fire-glow `+0x1D0` alpha ramp: SoH matches the oracle EXACTLY — v4's "ramp rate/phase
   mismatch" hypothesis is FALSIFIED

### Method

`title_logo_actor.md` §5.3 already documents the oracle's `+0x1D0` (backdrop/glow alpha) formula,
derived from static decompile of `FUN_001da9f8`/`FUN_001da4f4` AND cross-checked live against a
real Az memory trace in a prior session (§5.5: "live instance @ phys 0x27008CA0 ... confirmed
every field"). That's already a live-verified ground truth, not a guess — re-deriving it via a
fresh live memory scan this session would just reproduce the same numbers at extra risk (the
`memscan` writer-PC approach is documented as unreliable, §5.5's own caveat), so this session
used the existing formula directly as "oracle +0x1D0 per cs frame":

```
cf < 466:        alpha = 0
466 <= cf <= 525: alpha = (cf - 465) * 4.25   (snaps to 255.0 exactly at cf=525, 60*4.25=255)
cf > 525:         alpha = 255 (Display hold; unaffected by later fade phases, which only touch
                   the wordmark/copyright, not the backdrop)
cf in [560,580]:  alpha = 255 (same Display hold)
```

For SoH: booted the real title demo headless (`ZELDA3D_HEADLESS=1 ZELDA3D_WARP= ZELDA3D_DBG_FIREGLOW=1
tools/zelda3d_game.sh start`), let it free-run through the fade-in window, and captured
`title_fireglow.cpp`'s own `ZELDA3D_DBG_FIREGLOW=1` trace (prints `csFrame`, `cmabFrame`, cmab
`rgb`, `uvV`, and `alpha` every draw — `alpha` is literally the value returned by
`Zelda3D_TitleLogoPhaseAlpha3`, SoH's port of the same `+0x1D0` field). `Zelda3D_TitleCsFrame()`
is the SAME cs-frame domain as the oracle's `csCtx.curFrame` post the 2026-07-10 `sFirstAdvance`
dayTime-phase fix (v4 residual 1) — no az/soh offset needed for this comparison, unlike
`title_ab.py`'s `az_step`/`soh_step` domains.

### Per-frame table (oracle formula vs SoH live trace, cf460-540 + cf560-580)

| cf | oracle `+0x1D0` | SoH `alpha` | delta |
|---|---|---|---|
| 460 | 0.00   | 0.0   | 0 |
| 465 | 0.00   | 0.0   | 0 |
| 466 | 4.25   | 4.2   | 0 (float print rounding) |
| 470 | 21.25  | 21.2  | 0 |
| 480 | 63.75  | 63.8  | 0 |
| 490 | 106.25 | 106.2 | 0 |
| 500 | 148.75 | 148.8 | 0 |
| 510 | 191.25 | 191.2 | 0 |
| 520 | 233.75 | 233.8 | 0 |
| 524 | 250.75 | 250.8 | 0 |
| 525 | 255.00 | 255.0 | 0 |
| 526 | 255.00 | 255.0 | 0 |
| 540 | 255.00 | 255.0 | 0 |
| 560-580 | 255.00 (all) | 255.0 (all) | 0 |

Every single sampled frame across cf460-540 (full 4.25/frame ramp, 61 frames, not just the
10-frame subsample shown above — `scratch/fireglow_trace.txt` has the complete per-frame log)
and cf560-580 (21 frames, Display hold) matches the oracle formula to float-print precision
(sub-0.1 rounding only). **This is an EXACT match, zero measurable rate or phase error.**

### This falsifies v4's residual-2 hypothesis

v4 (`2026-07-10-title-arc-closing-measurement-v4.md` §2) measured the SCREEN-PIXEL frame-differenced
R/G/B ratios (`fireglow_ab.py --diff`) climbing 0.58 -> 1.02 (R) and 0.65 -> 1.34 (G, overshooting)
across cf490/525/570, and hypothesized "SoH's own additive alpha ramp (+0x1D0 staging) advances at
a slightly different RATE than the oracle's within this specific 460->570 window". **That
hypothesis is wrong** — the `+0x1D0` alpha channel itself is bit-for-bit correct at every sampled
frame in exactly that window. The pixel-visible R/G drift must come from somewhere else in the
draw. The most likely remaining candidate, visible directly in this session's own trace: the
CMAB's **const-color RGB** curve (`title_fireglow.cpp`'s `rgb=(...)`, sampled via
`Zelda3D_CmabSampleConstColorRGB` at `cmabFrame = csFrame - fadeInFrame`) is NOT flat — it's a
genuine flicker curve that oscillates over this exact window (sampled every 5th frame,
cf460-540): `R` goes 0.644 -> 0.788 -> 0.896 -> 0.888 -> 0.802 -> 0.899 -> 0.841 -> 0.800 -> 0.921
(non-monotonic, a real fire-flicker waveform, not noise). If `fadeInFrame` (the anchor `cmabFrame`
is measured from) is off by even a few cs-frames vs the oracle's own anchor, this flicker curve's
PHASE would shift, which would show up exactly as ratios that undershoot-then-overshoot through 1.0
across a window — the pattern v4 measured — while the alpha channel stays perfectly synced (as
confirmed here, since alpha and RGB are sampled independently: alpha from
`Zelda3D_TitleLogoPhaseAlpha3`, RGB from the CMAB player at a *different* per-frame cursor). v4's
own text half-anticipated this ("worth auditing the ramp curve directly ... in a dedicated
session") but named the wrong channel (`+0x1D0` alpha instead of the CMAB RGB flicker curve /
`fadeInFrame` anchor). **Follow-up (separately scoped, not this session): audit the CMAB RGB
curve's `fadeInFrame` anchor against the oracle's own draw-fn cmab-frame cursor** (§6 of
`title_logo_actor.md` already static-decompiled the draw fn fully — the anchor derivation should
be traceable from that without new live probing).

## 2. cs-438: NOT a scene/terrain framing gap — it's the logo-overlay color/alpha compositing

### Re-captured pair, verified camera-adjacent framing first

`tools/title_ab.py ab 700 --soh 1108 --name cs438` (== cs frame 438 per the `az_cs(az_step) = 88 +
0.5*az_step` law, `ZELDA3D_TEXPACK=off`). Content-match score: 0.4725 (matches v4's re-measurement
exactly, no drift). Region-diff table unchanged from v4:

```
(100, 80, 200, 160): az=(42,42,24) soh=(53,63,63) d=(-10,-21,-38)   <- largest single region delta
```

### Visual re-inspection (this session, at 3x crop zoom — v4 did NOT zoom in) OVERTURNS the prior
   "wide hillside vs closer grass, terrain content gap" attribution

Cropped and enlarged BOTH the terrain-only region (top-right corner, away from the logo overlay)
and the logo-overlay region separately:

- **Terrain (top-right crop, no overlay):** Az and SoH show the SAME content — same rock/cliff
  face, same two bushes, same road curve, same hillside silhouette, at the same apparent scale and
  camera angle. No framing/zoom difference, no missing/extra geometry. The camera IS exonerated
  here, and so is scene/terrain streaming — v4's read of "SoH shows a rock/cliff face on the right
  edge that doesn't appear in Az's frame at all" does not hold up at this crop; Az has the same
  rock face, just rendered less crisply (texture-filtering/sharpness difference between the two
  renderers, not a content gap).
- **Logo overlay (center-left crop, shield+sword+wordmark):** THIS is where the two frames
  genuinely diverge, and it's exactly where the large-delta region box sits. Both engines draw the
  same shield+sword+"THE LEGEND OF ZELDA / OCARINA OF TIME 3D" composite at essentially the same
  screen position/scale (confirming the wordmark alpha-ramp phase from `title_logo_actor.md` §5.3
  is in the right ballpark at cf438 — mid-ramp, phase 2, wordmark alpha ~(438-384)*3≈162/255).
  But the COLOR/BRIGHTNESS of the shield differs sharply: **Az's shield renders dark and muted**
  (murky teal-black shield body, subdued dark-red "Z" outline, low contrast against the dark
  background it's blended over) while **SoH's shield renders bright, saturated blue with visible
  individual white specular highlight dots** and a more vivid red Triforce crest — it reads as
  effectively unlit/full-bright compared to Az's darker, more blended-in appearance.

### Named finding

**The cs-438 divergence is a color/alpha-compositing difference on the logo overlay's shield
graphic (the same `g_title`/wordmark asset family covered by `title_logo_actor.md` §5-6), NOT a
scene/terrain/camera content gap.** The terrain background is confirmed matching at this frame;
the previous "wide hillside vs closer grass" description in v3/v4 does not survive a zoomed-in
re-look and should be treated as superseded. Candidate mechanism (not diagnosed further this
session, attribution-only scope): SoH may be drawing the shield element without the
ambient/lighting darkening multiply that Az's overlay compositing applies (i.e. SoH draws it
closer to "raw texture" brightness), or there's a const-color/vertex-color difference between the
two engines' draw of this specific composite element. This is plausibly related to, but distinct
from, the fire-glow backdrop's own alpha ramp (§1 above) — the shield/wordmark element is a
SEPARATE draw block per §5.2/§6 (`+0x1D4`, not `+0x1D0`), and its alpha timing looks correct
(same silhouette/position at the same frame); the divergence here is color intensity, not
position or opacity-over-time.

### Fog/draw-distance hypothesis: checked, ruled out as the cs-438 driver, but exposes a genuinely
   dead code path worth flagging

Per the task's specific ask, checked the 3DS title light-palette's `fogEnd`/`drawDist` fields
(`zelda3d_cutscene.cpp` `TitleLightEntry`, parsed from `spot99_info.zsi`'s 4×28-byte palette
directly before the `" BDQ"` cs) against SoH's live fog state during the title cs.

**Palette values** (read directly from `scratch/oot3d_title_cs/spot99_info.zsi`, little-endian
per the runtime-pinned layout comment at `zelda3d_cutscene.cpp:124`):

| slot | fogEnd | drawDist | fogNear (u16) |
|---|---|---|---|
| 0 | 32000.0 | 48000.0 | 0x04c8 |
| 1 | 32000.0 | 56000.0 | 0x0720 |
| 2 | 32000.0 | 48000.0 | 0x04c8 |
| 3 | 32000.0 | 40000.0 | 0x0428 |

**SoH's live fog state** (REPL `fog` during the running title cs): `fogNear=996 fogFar=12800`.

**Finding: `fogEnd`/`drawDist` are parsed into `sTitlePal` but are DEAD — never read anywhere else
in the codebase.** `grep -n "fogEnd\|drawDist"` across `zelda3d*.c*` and
`behaviors/title/*.cpp` turns up only the struct definition, the two `memcpy`s that populate it at
load time, and the doc comment — no consumer. `Zelda3D_TitleCsBlendedLight` (the function that
actually feeds the title's live per-frame lighting into `envCtx.lightSettings`) blends
`amb`/`l1dir`/`l1col`/`l2dir`/`l2col`/`fogCol` from the palette but does **not** blend or return
`fogEnd`/`drawDist`/`fogNear` at all (`zelda3d_cutscene.cpp:682-702`). So the title cs's actual
live `fogNear`/`fogFar` (996/12800, confirmed via REPL) comes from whatever `spot99`'s ordinary
N64-style scene light-settings blob (`sLightSlotsRaw`, the standard cmd-0x0F table) already had
loaded via the normal `Play_Init` scene path — a value roughly **2.5-4x shorter** than every one
of the palette's `fogEnd`/`drawDist` entries (32000-56000). This is a real, confirmed gap (the
3DS-specific title draw-distance/fog-far data is being silently discarded), but it is **not** the
cs-438 driver: the zoomed terrain crop (above) shows matching content/haze at this specific
close-in shot, consistent with 12800 units still being far beyond what's visible in this
close-hillside framing. Flagging as a separate, real, unfixed gap — worth wiring
`Zelda3D_TitleCsBlendedLight` (or a sibling accessor) to also blend+return `fogEnd`/`drawDist` and
feed them into `Zelda3D_FogSetPosition` during the title cs specifically, in a follow-up session;
not attempted here (measurement-only scope, and it's not what's causing cs-438's visible
divergence).

## Conditions

- Build: unmodified `main` (`Shipwright/build-cmake/soh/soh.elf`, dated `Jul 10 11:23`, already
  built by a prior session — no rebuild this session).
  `ZELDA3D_TEXPACK=off` for the cs-438 capture. Headless throughout (`ZELDA3D_HEADLESS=1`,
  Xvfb `:99`). Game process stopped cleanly (`tools/zelda3d_game.sh stop`) before finishing; no
  processes left running.
- Scratch (gitignored, not committed): `scratch/fireglow_trace.txt` (full per-frame FIREGLOW
  trace), `scratch/title_ab/r4_cs438.{az,soh}.png`, `r4_cs438_sxs.png`,
  `r4_az_topright.png`/`r4_soh_topright.png` (terrain-only crops),
  `r4_az_logo.png`/`r4_soh_logo.png` (logo-overlay crops).
- No source files changed. `git status` clean except this journal file.
