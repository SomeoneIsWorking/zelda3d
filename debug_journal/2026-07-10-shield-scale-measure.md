# Shield/sword saturation residual + logo scale — measurement only (2026-07-10)

Measurement-only session on the build with the wordmark sheen mechanism ported
(`debug_journal/2026-07-10-wordmark-sheen-mechanism-ported.md`). No code changed. Fresh captures
via `tools/title_ab.py ab` (TEXPACK=off, headless), names `shieldmeas_1000` / `shieldmeas_1522`:

- az=1000 / soh=1408 — content-match score **0.8366**
- az=1522 / soh=1930 — content-match score **0.8403**

All coordinates are 400x240 frame space (same as the prior shield journals). Measurement script:
`scratch/decomp_agent/measure_shield_scale.py` (+ inline mask variants, this session's transcript).

## 1. Shield-face / sword-blade saturation residual

### Legacy fixed patch (114,98)-(124,108) — for continuity with 2026-07-10-shield-glint-dualtex.md

| frame pair | oracle (Az) | SoH | SoH/Az per-channel |
|---|---|---|---|
| az1000/soh1408 | (64.7, 54.6, 63.5) | (30.4, 32.9, 51.6) | (0.47, 0.60, 0.81) |
| az1522/soh1930 | (64.8, 54.6, 63.5) | (30.4, 32.8, 51.6) | (0.47, 0.60, 0.81) |

**Caveat: this fixed box is no longer a clean shield-face sample.** A 4x zoom overlay
(`scratch/title_ab/shieldmeas_1000_{az,soh}_zoomgrid.png`) shows the two panes' shield poses
differ slightly (known divergence, attribution journal: sword ~29 deg vs ~32 deg): in the SoH pane
the shield's white left rim and a dark paint-chip decal cross the box, in the Az pane it lands on
the bright cyan glint dot. So the patch measures different content per pane and the prior
journal's numbers at this box (SoH (51.3,53.0,72.4) / oracle (40.3,49.6,65.9)) are not
pixel-comparable either — the oracle value at the same box in THIS deterministic title_ab capture
is (64.7,54.6,63.5), not (40.3,49.6,65.9), so that session's oracle patch came from a differently
aligned capture.

**(a) Did the sheen change move the shield?** Yes. SoH at the same box moved
(51.3,53.0,72.4) → (30.4,32.9,51.6) = x(0.59, 0.62, 0.71) darker — an approximately uniform
multiplicative darkening, consistent with the shield's vertex-lit (real-normal) draws now
receiving the animated-light shade term. (Patch contamination limits precision; direction and
rough uniformity are solid.)

### Mask-based shield-face measure (robust to the pose offset)

Blue-hue mask inside the shield region x[100,220] y[80,160]; identical results at BOTH frame
pairs (shield content is static post-ramp — SoH shield pixels bit-identical 1408 vs 1930, oracle
face means within 0.7):

| mask | oracle mean (n) | SoH mean (n) | SoH/Az per-channel |
|---|---|---|---|
| loose (H 195-265, S>0.30, V>0.15), az1000 | (16.9, 40.2, 101.1) (n=1137) | (17.2, 40.6, 116.1) (n=1475) | (1.018, 1.010, **1.148**) |
| loose, az1522 | (16.2, 40.0, 100.9) (n=1124) | (17.2, 40.6, 116.1) (n=1475) | (1.062, 1.015, **1.151**) |
| strict core (S>0.55, V>0.25), both frames | (19.0, 49.3, 120.2) (n=797) | (19.5, 46.6, 129.8) (n=1220) | (1.027, 0.945, **1.080**) |

**(b) Verdict: the residual is BLUE-SPECIFIC, not a uniform brightness factor.** R and G now sit
within ~+-5% of the oracle on both mask strictnesses; B alone runs +8% (strict core) to +15%
(loose) hot in SoH, consistently at both frames. A shading term would scale R,G,B together; this
points at the texture/tint side (blue-channel decode or a tint on the shield-face texture), not
at the lighting/shade path. Secondary observation: SoH's blue-face pixel COUNT is 29-53% larger
(1475 vs 1137/797 vs 1220 core) — SoH's shield face renders larger / less letter-occluded,
consistent with the already-documented pose + occlusion-order divergence, and this alone inflates
any fixed-box comparison.

### Sword blade

Grey-mask (S<0.30, V>0.45) in x[150,215] y[35,72] — **low confidence**: the blade is mostly
covered by the animated fire glow at both frames and the mask survivor counts are tiny:

| frame pair | oracle mean (n) | SoH mean (n) | SoH/Az |
|---|---|---|---|
| az1000/soh1408 | (122.0, 97.1, 91.9) (n=68) | (124.0, 102.4, 98.8) (n=72) | (1.016, 1.055, 1.075) |
| az1522/soh1930 | (120.9, 109.3, 91.7) (n=26) | (120.6, 115.2, 90.3) (n=131) | (0.998, 1.054, 0.985) |

Deltas are within ~7% on every channel and inside the fire-flicker noise floor at these n; no
measurable blade-specific residual at these frames. (A clean blade measure needs a cs frame where
the blade isn't wrapped in flame, or the fire-glow draw disabled — out of scope for this
measurement-only pass.)

## 2. Logo (wordmark) scale / position

Red-letter mask (hue<=15 or >=345, S>0.5) bbox over each full pane; V gates 0.12 and 0.06 give
the IDENTICAL bbox in every pane (mask-converged), so one table:

| frame pair | pane | bbox (xmin,ymin)-(xmax,ymax) | w x h | center |
|---|---|---|---|---|
| az1000/soh1408 | oracle | (114,48)-(317,168) | 204 x 121 | (215.5, 108.0) |
| az1000/soh1408 | SoH | (117,47)-(317,166) | 201 x 120 | (217.0, 106.5) |
| az1522/soh1930 | oracle | (114,53)-(317,168) | 204 x 116 | (215.5, 110.5) |
| az1522/soh1930 | SoH | (117,49)-(317,166) | 201 x 118 | (217.0, 107.5) |

| frame pair | width ratio SoH/Az | height ratio | center offset (dx,dy) px |
|---|---|---|---|
| az1000/soh1408 | **0.9853** (-1.47%) | **0.9917** (-0.83%) | (+1.5, -1.5) |
| az1522/soh1930 | **0.9853** (-1.47%) | **1.0172** (+1.72%) | (+1.5, -3.0) |

Noise note: the oracle's bbox TOP edge flickers 5px between the two frames (ymin 48 vs 53 — fire
glow bleeding into the red mask around the "THE LEGEND OF" banner), so height ratio and center-y
are only good to ~+-3px / ~+-2%; the width measure is clean and stable (xmax identical at 317 in
all four panes, ratio bit-identical across frames).

**Verdict: AT-PARITY per the spec bar** — scale within ~2% on both axes (w -1.47% both frames;
h -0.8%/+1.7%, inside the mask-edge noise), center within 3px (dy=-3.0 at az1522 sits exactly at
the bar and is attributable to the ymin flicker above).

### pxPerUnit staleness check (read-only)

Not a concern: `Zelda3D_TitleOverlayPxPerUnit`
(`Shipwright/soh/src/zelda3d/behaviors/title/title_logo.cpp:330`) recomputes
`(refH/2)/(tan(fovy/2)*34)` from `play->view.fovy` on **every call**, and
`title_presentation.cpp:131` writes `play->view.fovy = csFov` **per cs frame** from the ported
OP97 camera spline (`Zelda3D_TitleCsCamera`). The 48.803 deg constant is only the fallback for an
unloaded cs. So the overlay scale already tracks any per-frame fov variation — there is no
snapshot to go stale, and the measured -1.5% width residual (if ever worth chasing) is not a
stale-fov artifact.

## Artifacts (scratch, not committed)

- `scratch/title_ab/shieldmeas_{1000,1522}.{az,soh}.png`, `_sxs.png` — the captures
- `scratch/title_ab/shieldmeas_1000_{az,soh}_zoomgrid.png`, `blade_{az,soh}_zoom.png` — patch-placement verification
- `scratch/decomp_agent/measure_shield_scale.py` — bbox + patch measurement script
