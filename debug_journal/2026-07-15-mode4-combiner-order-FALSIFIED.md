# Mat10/11 synthetic mode-4 premise — FALSIFIED (corrected 2026-08-30)

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

The 2026-07-15 experiment correctly ruled out clamp ordering, but still assumed mode 4 existed.
The 2026-08-30 exact-cursor identity capture and decompiled title draw falsify that larger premise:

- all ten mat10/11 oracle identities have `texEn=1/0/0` and the authored
  `MODULATE(PRIMARY,TEX0) x1 → REPLACE(PREVIOUS)` stages;
- the CMB has `tex1_idx=-1` and coordinator 0 set to CameraSphereEnvMap;
- the decompiled title draw writes alpha/light/transform state, with no TEV rewrite or TEX1 alias.

Mode 4 was therefore the cause of the white clipping, not an imperfect approximation awaiting a
different clamp order. The proper fix is independent coordinator-0 sphere-map transport into TEX0
plus the existing generic TEV evaluator. `tools/title_oracle_probe.py` now caches exact-cursor
uniform/fragment artifacts and returns before spawning on a cache hit, so this evidence is reusable
without rerunning the oracle.
