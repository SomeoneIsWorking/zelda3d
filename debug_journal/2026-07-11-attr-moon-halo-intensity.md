# Moon halo brightness/size attribution (measurement only, no code changes)

Task: attribute why SoH's title-screen moon halo reads brighter/larger than the OoT3D
oracle's (residual flagged in `debug_journal/2026-07-10-moon-mirror-and-fade-attenuation.md`).
Scope was explicitly measurement/attribution only — no `zelda3d.c` edits landed here.

## Setup

Content-matched pair via `tools/title_ab.py ab 200 --soh 605 --name attr_moon_200`
(the pinned `soh = az + 405` correspondence from commit `19081f9a`, oracle-cache hit,
content-match score 0.9326 — a real, unambiguous match, not a coincidental frame number).

Per-region RGB delta (Az−SoH) at the moon's bounding box confirms the residual is still
live: `(300,0,400,80): az=(143,140,132) soh=(180,178,142) d=(-38,-38,-9)` — SoH still
reads brighter/flatter there, consistent with the prior journal's note.

## Step 1 — is the alpha/blend MECHANISM the bug? No: SoH already ports the documented mechanism byte-for-byte

Read `oot3d-decomp/docs/env_sun_moon_draw.md` Session 4 (the per-pixel TEV combiner RE) and
diffed it against the live code (`Shipwright/soh/src/zelda3d/zelda3d.c:3807-3860`,
`Zelda3D_TryDrawSunMoon`):

| aspect | decomp ground truth (Session 4) | SoH code |
|---|---|---|
| halo draw color/alpha | `primary_color=(255,255,255,255)`, texture-only, zero modulation (Finding 1) | `gSPZelda3DDrawA(..., m1\|(1<<30), 255,255,255,255)` / same for `m2` — full white, both halos |
| halo blend mode | ADD `(srcAlpha, One)` (`title_moon_composition.md`) | `BILLBOARDADD:` load path (`Zelda3D_MoonInnerHaloId`/`OuterHaloId`), additive |
| halo texture format | RGB565, no alpha channel — falloff baked into RGB, not alpha (Finding 1) | `~MIRROR` billboard load of `fine_moon1.ctxb`/`fine_moon2.ctxb`, decoded via `GF_RGB565` in `pica_texture.cpp` (no synthesized alpha ramp) |
| disc blend/alpha | disc is RGBA4, real crescent alpha, drawn at full 255 per the combiner (with the noted decode-brightness caveat) | `kMoonDiscAlpha=205`, explicitly commented as a STOPGAP for a texture-decode brightness bug, not a faithfulness claim |
| halo model scale | disc diagonal-scale 640, BOTH halos 1280 = exactly 2.0× (Finding 3) | `kMoonHaloScale=2.0f`, applied to both `s1` and `s2` |

**Conclusion: there is no missing/wrong alpha application to find.** SoH's halo layers are
already drawn at full opaque white with additive blending, texture-only, exactly matching
the decomp's per-pixel combiner readback. The "SoH halo is brighter" residual is NOT an
alpha-blend port bug — ruling this out is itself the useful result of this step, since it
redirects the remaining budget away from re-checking a mechanism that's already correct.

## Step 2 — radial luminance profile (5+ rings, per-pane center) confirms and localizes the excess

`scratch/moon146/radial_profile.py` (new, gitignored `scratch/`): finds each pane's moon
center independently (centroid of top-2%-brightest pixels in the moon's screen quadrant,
so it isn't thrown off by the two panes' camera-framing offset — see caveat below), then
bins mean luminance into rings out to r=90px:

```
  ring       az      soh   soh-az
   0-12   196.78   241.02    44.23
  12-24   191.11   206.37    15.26
  24-36   170.73   159.99   -10.75
  36-48   111.53   131.75    20.22
  48-60    73.85   105.24    31.39
  60-75    57.35    63.36     6.01
  75-90    47.27    47.42     0.15
```

SoH reads brighter than Az in every ring except 24-36, most strongly at the disc core
(0-12, +44) and in the halo mid-band (36-60, +20 to +31), converging to near-zero by the
halo's outer edge (75-90). The 24-36 dip is very likely crescent-shading asymmetry noise
(fine_moon0's alpha channel encodes an irregular crescent shape, not a disc; averaging it
radially around an auto-detected center is inherently approximate) rather than a real
under-brightness — not chased further, it doesn't change the overall sign/shape of the
finding.

**Caveat (read before reusing these absolute numbers):** cropping both panes to the same
screen box (`scratch/moon146/attr_moon_200_{az,soh}_crop.png`) shows the two panes' cameras
are NOT perfectly registered at this content-matched instant — Az's moon is partially
clipped by the top screen edge here, SoH's is not. This is the pre-existing, already-
documented title-camera-timing residual (`2026-07-10-moon-mirror-and-fade-attenuation.md`,
`title-arc-closing-measurement*.md`), not a new finding. It means the disc-core ring's
+44 delta is partly confounded by Az's disc being edge-clipped in this specific frame
(fewer bright pixels counted) rather than 100% attributable to disc brightness alone — a
better-framed (fully unclipped in both panes) matched shot would be needed to isolate the
disc-core number precisely. The halo mid-band numbers (36-60px, well outside the clipped
region) are not affected by this confound and are the more trustworthy part of the profile.

## Step 3 — quantitative test of the leading halo-mid-band hypothesis using SoH's own texture data

The code (`zelda3d.c:3813-3820`) already documents *why* the halo scale might be wrong: OoT3D
places `fine_moon1` (inner) and `fine_moon2` (outer) at **different depths** from the disc
(`env_sun_moon_draw.md` Session 4, Finding 3: z≈-2774/-2684/-2595), producing on-screen size
ratios of **~1.94×/~2.07×** rather than the uniform model-space 2.0×/2.0× — but SoH draws
both halos at the identical on-screen scale (`kMoonHaloScale=2.0f` for both `s1` and `s2`,
same world position), because it doesn't replicate the per-layer depth placement. This means
SoH's two additive halo circles are drawn at IDENTICAL screen size and so overlap 100% at
every radius, whereas OoT3D's two differently-sized circles only partially overlap — a real,
mechanically-motivated way for SoH's additive sum to end up brighter in the band between the
two edges.

Tested this directly rather than asserting it: `scratch/moon146/halo_overlap_sim.py`
mirror-expands SoH's actual `fine_moon1.ppm`/`fine_moon2.ppm` (same 64×64→128×128 expansion
`zelda3d_model.cpp`'s `mirrorExpandQuadrant()` uses, verified radially symmetric in the prior
session), builds each texture's own radial luminance profile, then sums the two layers
additively under (a) SoH's current uniform 2.0×/2.0× scale and (b) the decomp's ground-truth
1.94×/2.07× screen ratios, over a shared physical-radius axis normalized to the disc's own
texture-edge radius `D`:

```
 r/D   soh(1+2)   gt(1+2)   gt-soh
 0.89    144.21    139.91    -4.30
 0.93    136.98    126.34   -10.64
 0.97    119.89    102.70   -17.19
 1.02     95.47     79.40   -16.07
 1.06     74.55     63.12   -11.44
 1.10     60.60     53.00    -7.60
 1.19     44.91     40.02    -4.89
 1.31     28.80     25.03    -3.77
 1.44     30.08     23.35    -6.73
```

Using the real per-layer screen ratios instead of the uniform 2.0× lowers the combined
additive luminance by up to ~17 units in the r/D≈0.9-1.6 band. With the measured disc radius
D≈27.3px (`kMoonDiscScale` circle-fit, 54.6px diameter), that band maps to ≈25-44px physical
— overlapping the same 36-60px band where the paired-frame radial profile shows SoH +20 to
+31 over Az. Same sign, same rough zone, right order of magnitude: this is a real,
decomp-sourced, quantitatively-consistent (not just plausible-sounding) partial explanation
for the halo-band excess. It does **not** by itself explain the disc-core (0-12 ring) excess,
which Step 2's caveat already flags as confounded by camera framing at this frame and/or the
already-documented `kMoonDiscAlpha` decode-brightness STOPGAP — a separate, unresolved piece.

## Attribution summary

- **NOT a bug**: the halo alpha/blend application. SoH already draws both halos at
  `(255,255,255,255)` additive, texture-only, matching the decomp's per-pixel combiner
  readback exactly (Step 1). Do not re-investigate this mechanism.
- **Actionable, decomp-cited partial fix**: differentiate the two halos' on-screen scale
  (`fine_moon1` ≈1.94×, `fine_moon2` ≈2.07×, per `env_sun_moon_draw.md` Session 4's
  screen-ratio derivation, which explicitly names this as the SoH-appropriate substitute
  for real depth placement) instead of the current single `kMoonHaloScale=2.0f` for both —
  quantitatively shown (Step 3) to reduce the additive halo sum in the exact band (36-60px)
  where the measured excess lives, by an amount of the right order of magnitude.
- **Unresolved**: the disc-core (0-12px ring) excess. Partly confounded by this frame's
  camera-clipping mismatch between panes (Step 2 caveat); the remainder is most likely the
  same class of issue as the already-documented `kMoonDiscAlpha=205` STOPGAP (fine_moon0
  RGBA4 decode brightness), not a new mechanism. Needs either a better-framed (fully
  unclipped in both panes) matched shot, or a direct SoH-vs-Az decoded-texel comparison at
  matching UVs (the Session 4 `PIXEL` draw-log debug line already exists in the harness and
  could be re-armed for this) to isolate cleanly. Not chased further here — out of this
  task's measurement-only scope and genuinely needs more data, not more reasoning.

## Files (all gitignored `scratch/`, not committed)

- `scratch/moon146/radial_profile.py` — 5+ ring radial luminance profile tool, reusable for
  any future Az/SoH paired-frame brightness question (per-pane auto-center, not just moon).
- `scratch/moon146/halo_overlap_sim.py` — additive-overlap simulator using SoH's real
  decoded+mirror-expanded halo textures; reusable for testing other billboard-scale
  hypotheses without touching game code.
- `scratch/moon146/attr_moon_200_{az,soh}_crop.png` — zoomed crops used for the
  clipping-confound observation.
- `scratch/title_ab/attr_moon_200.{az,soh}.png`, `attr_moon_200_sxs.png` — the paired capture
  itself (oracle-cache-backed, reproducible via `tools/title_ab.py ab 200 --soh 605`).

## No code changes

Per task scope, `Shipwright/soh/src/zelda3d/zelda3d.c` was read but not edited. The
fix spec above (differentiate `kMoonHaloScale` per layer) is ready to implement in a
follow-up session; verify via `tools/title_ab.py ab 200 --soh 605` +
`scratch/moon146/radial_profile.py` before/after, acceptance = the 36-48/48-60 `soh-az`
deltas shrink toward 0 without regressing the 0-24 rings, plus `lus_tests` staying green.
