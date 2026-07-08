> **SUPERSEDED 2026-07-08 (see 2026-07-08-title-divergence-remeasure.md):** the pairs used here
> (az360↔soh360, az396↔soh413) were NOT actually content-matched — tools/title_ab.py proved the
> true match is az360↔soh449 (~89 frames off). The magnitudes below are inflated/mis-framed by the
> mismatch. Re-measured verdicts at matched frames: terrain darkness REAL but ~2x (not 3x); stars
> REAL but it's per-star BRIGHTNESS (SoH max ~70 vs Az >140), not count; sky collapse REAL +
> sky-unfreeze confirmed warranted. Trust the remeasure doc, not the numbers here.

# 2026-07-08 — Title-screen parity audit: ranked REAL divergences (content-matched)

Content-matched A/B (both engines free-run from `scratch/title_settled.state` via plain `step N`,
NO `soh_titlecs` override — avoids the clock-desync trap of the sky-color/halo-hue false alarms).
Two content-matched pairs (by eye + `soh_env`/`compare lighting` internal-state cross-check):
pair1 = raw step 360/360; pair2 = step 396 Az / 413 SoH (clocks diverge in rate past ~step 360).
Artifacts (gitignored): `scratch/title_audit/{f360_sxs,pair2_sxs}.png`, `crops/*`.

## Ranked genuine gaps (robust across BOTH pairs = not desync artifacts)

### 1. Terrain/hill/grass tint ~3x TOO DARK & desaturated — HIGH
- hill (200,150): Az (47,37,38) vs SoH (15,13,11); grass (200,220): Az (57,79,22) vs SoH (17,26,7).
- Holds at both pairs → not schedule-dependent. Az cliffs show warm-brown rock detail at night;
  SoH cliffs are near-flat black silhouettes.
- Locus: `zelda3d.c` ~3620-3634 tint block `tint[i]=ka*ambient[i]+kd*light0[i]+ke*light1[i]` →
  `gSPZelda3DDraw(POLY_OPA_DISP++, modelId, tint[0..2])`. At this frame `soh_env` reported
  `ambient=(50,67,110)`, yet resulting hill tint is (15,13,11) — far below `ka*ambient` alone.
  worldShade Ka/Kd/Ke (`gZelda3dWorldShadeKa/Kd/Ke`) or the light0/light1 color feed for the
  title-cs path is under-driving the terrain draw. Raw ZSI slots ARE ported
  (`Zelda3D_TitleCsLightSlotsRaw`); looks like a scale/application bug DOWNSTREAM of them.
- Fix: derive real OoT3D spot99 terrain draw color from ROM/decomp; do NOT just tune constants
  (see [[soh3d-stop-microtuning-lighting]]).

### 2. Star field ~3-9x TOO SPARSE/dim — HIGH
- star count: pair1 Az 83 / SoH 9; pair2 Az 85 / SoH 25. Consistent deficit both pairs.
- Locus: `fine_star` draw, `Zelda3D_AutoModelId("SKY:/kankyo/BlueSky.zar|fine_star")` ~3709.
- Fix: dump `fine_star` per-point alpha/brightness from `BlueSky.zar`, compare to what SoH emits;
  likely over-attenuated per-point alpha OR wrong blend/time-of-day fade dimming stars too early.

### 3. Sky dome color collapses mid-cutscene — MEDIUM-HIGH (mechanism root-caused)
- pair1 diffs <=8/255 (fine). pair2: R/G diffs 20-40/255, SoH darker/more blue-saturated.
- MECHANISM (via `soh_env`): at SoH raw step 413 (`csCtx.frames=182`), `envCtx.skybox2Index`
  snaps 0->3 = `skybox1Index`; `Zelda3D_TryDrawSky` blend guard (~3775-3820,
  `doBlend = ... && idx2 != skybox1Index`) then turns the sunrise blend OFF -> reverts to pure
  night dome, discarding fading-in sunrise warmth = the R/G collapse.
- UPSTREAM: `skybox2Index`/`skyboxBlend` <- `gSaveContext.dayTime` <- `Zelda3D_TitleCsTimeOfDay`
  (`zelda3d_cutscene.cpp:444-458`): `dayTime = anchor + 6*(frame - nearest_preceding_cue)` — a
  FLAT LINEAR EXTRAPOLATION from sparse `sTimeCues` anchors. Drifts further from real dayTime the
  further past an anchor -> crosses a skybox segment boundary early.
- Fix (direct-from-ROM): pull the COMPLETE time-of-day keyframe set from spot99's cutscene
  command stream in the ROM (not just sparse anchors), interpolate between real adjacent ROM
  keyframes instead of extrapolating past one.

### 4. Moon core hue/brightness — LOW confidence
- core RGB Az (191,181,113) vs SoH (220,210,113): B matches, R/G ~30 higher on SoH (washed-white
  vs golden). Size measurement unreliable (clips frame edge). NOT actioned — needs radial-falloff
  fit + `fine_moon0.ctxb` texel check before touching. (Moon otherwise faithful per moon RE; the
  "RGBA4 decode too bright" theory was a misdiagnosis — `e4` is correct bit-replication. See
  addendum in `2026-07-08-title-moon-size.md`.)

## Non-findings / tooling gaps
- Rider pose/pos, hill silhouette, camera FOV: matched (that's how pairs were selected).
- Fog: harness can't read Az's computed fog RGB (only envCtx slot index) — tooling gap, extend if
  fog parity becomes a target.
