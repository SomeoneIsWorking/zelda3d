# cs464 composition offset EXONERATED; cs1093 fireglow remeasured post-decoration-fix (2026-07-14)

Follow-up to `2026-07-14-title-cs464-wordmark-and-composition-and-fireglow.md` (D2/D3) and
`2026-07-14-wordmark-spheremap-normal-transform.md` (the corrected D1 history; commit `a20566da`
had inferred the camera basis, later falsified by captured c4-c6 identity).

## Divergence 2 (whole-frame "low-right" shift at cs464): NO BUG — measurement artifacts, camera port verified at <=1px

Two independent direct measurements on the post-D1 exact-frame-locked captures
(`sphfix y2_00_cs464` / `sphfixy2_01_cs1093`):

1. **Terrain silhouette (the 3D projection's ground truth): <=1px parity everywhere.**
   Measured the ridge SKYLINE edge (first non-sky row per column, sky = blue-dominant-or-dark
   classifier) at every column, both frames:
   - cs464: median diff +0.0 px (spot columns all 0/+1 across x=20..380)
   - cs1093: median diff +0.0 px, std 1.9
   A camera/projection bias (translation OR fov/scale) would shift this silhouette; it does
   not. **The ported title camera/projection path is verified correct at sub-pixel level at
   two different cs segments.**

2. **The earlier "dy=-6 terrain band" cross-correlation result is a texture-correlation
   artifact, not geometry.** Band-wise gradient SSD gave inconsistent dy across heights
   (0 / -6 / -1 / +4 at increasing rows — fits neither a translation nor a center-scale
   model); the -6 in the ridge band tracks interior hill shading/texture differences
   (out-of-scope lighting detail), not the silhouette, which measures 0.

3. **The 2D overlay placement is also at parity post-D1**: wordmark red-mask bbox at cs464
   az x[115,318] y[79,168] centroid (215.0,123.0) vs soh x[117,317] y[86,167] centroid
   (213.6,121.1) — centroids within 2px (the y-top edge delta is fade-edge mask noise).

**Conclusion:** the original visual "shifted low-right" impression was produced by the
D1 decoration-rendering bug (invisible/pale gold outlines changed the wordmark's apparent
visual weight) combined with the mid-assembly CSAB fly-in pose at cs464 — both now at
parity. No fix needed in the harness capture, the camera spline, or the overlay placement;
all three were checked and exonerated with numbers. The prior session's
`harness-title-sync` note about a "~2-3% lower/right wordmark" should be considered
resolved by the D1 fix (bbox above).

## Divergence 3 (cs1093 fireglow extent): improved by the D1 fix; residual is flame-tongue shape, not gain

Metric cleanup first: `fireglow_ab.glow_stats`' gold-hue mask now OVER-counts on SoH
because the (fixed) letters pass its g∈(0.3r,0.9r) window — post-D1 the raw box number
inverts to 1.87x and is meaningless. The clean extent metric is **gold minus strict-red**
(glow wash without letter fill), box-scoped:

| | oracle | SoH | coverage |
|---|---|---|---|
| BEFORE (intsync2 baseline) | 3217 px, lum 162.1 | 2379 px, lum 160.6 | 0.740 |
| AFTER D1 fix | 3262 px, lum 162.9 | 2553 px, lum 161.8 | **0.783** |

Per-pixel luminance is at parity (161.8 vs 162.9) — confirming (again) there is no gain
bug: the x2 combiner scale and the mableT dual-texture term were already ported
(2026-07-10) and measure correct. Vertical warm-profiles through the below-shield fringe
show comparable peak values (~245) and comparable fringe falloff, with the flame TONGUES
peaking at slightly different rows — the residual ~20% mask-count gap is tongue-shape /
hue-bin detail, not a size or brightness error.

**Remaining residual (bounded follow-up, not closed):** the glow flame-tongue shapes at
DISPLAY-hold differ subtly from the oracle's. Next concrete step: data-level compare of
the oracle's live coordinator-1 translation register (mableT UV phase) at cs1093 via the
harness draw-log vs SoH's `uv1Trans + uvV` (one suspect checked analytically already: the
V-wrap at frozen track value 1.0 gives the same effective phase on both sides, ruled out).
Also note for a future session: SoH advances the fire CMAB cursor in cs-frames (30fps)
while the CMAB's duration=300 is authored at 60fps per `title_logo_fireglow_cmab.md` — a
2x playback-rate difference during the first 300 frames of the flicker (irrelevant at
cs1093, which is long past the freeze, but it will matter if anyone A/Bs the fade-in
flicker window cs345-495).

## No code changes in this entry

Both items are measurement/exoneration work on the already-committed D1 fix build.
