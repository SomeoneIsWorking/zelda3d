# Mode-4 (mat10/11) sphere-decoration combiner-order hypothesis — FALSIFIED (2026-07-15)

## Hypothesis (wrong)
The wordmark gold-decoration residual (parity-map `title.wordmark-decoration`, cs1030
SBS score 0.896 — the loop's low point) was theorized to be a PICA TEV order-of-operations
bug: the oracle runs `tev[0]=SAT(uSheen.z*PRIMARY*TEX0)`, `tev[1]=SAT(PRIMARY*TEX1+tev[0])`
(PRIMARY = the wordmark sheen term, INSIDE each stage's saturate), while SoH's shader
(`zelda3d_sdl3gpu.cpp` mode-4 branch) computes `clamp(uSheen.z*t0s + t1)` then multiplies
`shade` OUTSIDE, after the clamp. Theory: for a bright gold texel with PRIMARY<1, SoH clips
`3*TEX0` to white before applying shade, desaturating the gold; oracle keeps it gold.

## Test (the falsification)
Implemented the "fold PRIMARY inside the per-stage saturates, skip the outer shade for
mode-4" version, rebuilt the harness, re-ran `tools/title_sbs_verify.py --k 6`. Measured the
logo-box (x 0.25..0.80, y 0.14..0.50) against the oracle at cs1030:

| metric        | oracle | SoH BEFORE | SoH AFTER (fix) |
|---------------|--------|------------|-----------------|
| logo-box SSD  |   —    | 130.7M     | **150.8M (WORSE)** |
| white_px      |  112   | 664 (5.9x) | **692 (6.2x) WORSE** |
| meanR ratio   |  1.0   | 1.124      | **1.181 (further)** |
| gold_px       | 3779   | 4551       | 4553 (unchanged) |

cs590 was near-neutral (gold 3728->3724). The fix pushed SoH BRIGHTER / MORE white-clipped,
i.e. AWAY from the oracle. **Reverted** (`zelda3d_sdl3gpu.cpp` restored to `clamp(t0s*
uSheen.z + t1)` + unconditional outer shade).

## What this rules out / what it means
- The wordmark residual is NOT the combiner clamp ORDER. The oracle is actually DIMMER than
  SoH in the logo region (oracle meanR 116 vs SoH 130; oracle white 112 vs SoH 664) — SoH
  OVERSHOOTS (too bright / too much white-clip). The real residual direction is "SoH too
  bright", not "SoH too dim/desaturated" as the earlier journal at cs1093 read it (angle
  dependent — different cs frames disagree, so a single order-fix can't satisfy both).
- Root cause therefore lives UPSTREAM of the combiner: either the sphere-map UV sampling a
  too-bright texel, or PRIMARY(shade) too high. Resolving needs the oracle PER-PIXEL capture
  (SOH3D_PIXEL_TEX PIXEL dump: texcol + primary + combined at the mat10/11 draw) — which this
  session could NOT drive: `draw_log`/`vsuni_log` returned 0 bytes under both `run` and `step`
  from `title_settled.state` (the SW-rasterizer ProcessTriangle patch is present in-source AND
  in the built binary — `strings` confirms "tri cbuf=" / "PIXEL tex0=" — but never appended).
  Fixing that capture path is the prerequisite for any further wordmark-decoration work.
- Per user (2026-07-15): the static title is "good otherwise"; do NOT keep grinding the
  wordmark. Priorities are (1) horse/Epona ANIMATION, (2) occlusion (tree-behind-terrain).
