# 2026-07-14 — cs1093 "terrain" attribution (premise falsified) + En_Kusa type-0 field-grass CMB fix

Task brief: at title-cs 1093 the dominant remaining divergence was believed to be the TERRAIN
(oracle warmer/yellower, dense yellow flower speckling; SoH greener/flatter), candidate cause a
missing Obj_Mure2 (0x151) 3DS port. Score 0.64 vs 0.83+ elsewhere.

## Attribution — measured on matched harness captures (intsync2/base464 = pre-session baseline)

The terrain premise is **falsified by measurement**. On the pre-fix cs1093 pair
(`scratch/title_ab/intsync2_03_cs1093.*`):

- Terrain region means (az−soh): bottom-left band (y190-240,x0-200) = (−2.5,−3.7,−2.2)/255;
  mid-left (y150-200,x0-120) = (−4.2,−8.5,−10.3)/255. SoH is marginally BRIGHTER, not darker,
  and within a few LSB.
- Yellow-speckle pixel counts (R>110,G>110,B<100): az 3571 vs soh 3931 (ratio 1.10) bottom-left;
  az 1996 vs soh 1649 (ratio 0.83) mid-left. The speckling is predominantly the terrain
  TEXTURE, already at parity — not missing flower geometry.
- Total |diff| decomposition at cs1093 (pre-fix): **wordmark/logo box (x100-310,y60-190) = 42%**,
  **right-edge rider strip (x310-400) = 33%**, rest of frame (terrain+sky, ~74% of the area)
  = 25%. The score gap at cs1093 is the KNOWN open fireglow-wash residual
  (2026-07-10-fireglow-combiner journal §1 residual) + the rider framing — not terrain.

Candidates (a) mesh/vertex-color, (b) light schedule: both previously exonerated with live
byte-matching (2026-07-10 journals; formula sub-LSB exact, ambient byte-matched). (c) fog: known
open, needs the 3DS LUT-fill decompile; ~5% at near-terrain depths, minor warm bias only.
No lighting/fog constant touched this session (banned without decomp provenance).

## Real defect found under (d): En_Kusa TYPE 0 drew the WRONG CMB (Kokiri bush, not field grass)

Obj_Mure2 itself draws nothing (pure spawner, `z_obj_mure2.c`: children = `ACTOR_EN_KUSA`
(0x125), always `params&3==0`). N64 ground truth `z_en_kusa.c`:
`sObjectIds[] = {OBJECT_GAMEPLAY_FIELD_KEEP, OBJECT_KUSA, OBJECT_KUSA}` and
`dLists[] = {gFieldBushDL, object_kusa_DL_000140, …}` — type 0 is the FIELD grass tuft from
gameplay_field_keep; types 1/2 the Kokiri cuttable bush. SoH3D's `sModelTable` had ONE
unconditional `ACTOR_EN_KUSA` row → glModelId 2 = `zelda_kusa.zar|obj_kusa01_model` (Kokiri
bush, scale 0.5): every Mure2 field cluster rendered as a leafy bush. OoT3D asset ground truth:
ROM zar dump (tools/ctr_romfs.py + tools/zar.py) of `/actor/zelda_field_keep.zar` ships
`Model/grass05_model.cmb` (+`_up`/`_dn`), mapped "field grass" in
`oot3d-decomp/docs/keep_objects.md`.

### Fix (Shipwright/soh/src/zelda3d/zelda3d.c, zelda3d.h)

Param-keyed intercept in `Zelda3D_TryDrawActor` (same block as Obj_Hana/En_Ishi):
`ACTOR_EN_KUSA && (params&3)==0` → forced-CMB auto model `ZKEEP_FIELD "|grass05_model"`
(`ZKEEP_FIELD` new in zelda3d.h). Types 1/2 fall through to the existing bush entry. Scale
self-calibrated (measured N64 draw height / CMB height) via the existing `Zelda3D_ForcedMeas`
measure-bracket mechanism (windmill/well-arch pattern, #77/#82); REPL `gscale 12` overrides.
`actorsnear` coverage label: `KUSA-field-grass(3DS)`.

Gotcha: the pattern's `tries>=8` give-up budget counts VISITS and ~5+ live instances share the
one calibration slot — the budget burned out in ~2 frames, before the GPU-side
`Zelda3D_MeasureResult` (lands a frame later) could deliver. Budget raised to 64 visits
(mechanism unchanged).

### Verification (live, headless, rebuilt game + harness)

- Kokiri Forest (entrance 238): calibration converges first opportunity —
  `SOH3D AUTO: field-grass (grass05_model) -> scale=0.47302 (n64h=39.2 modelh=82.9)`; all
  nearby 0x125 type-0 list `KUSA-field-grass(3DS)`; close-up render shows the long-blade
  grass05 tuft, grounded, correct size (`scratch/screenshots/grass05_field*.png`).
- Hyrule Field entrance 0: nearby En_Kusa are `p=0xFF01` (type 1 bush) → still `TABLE`
  (fall-through preserved, no regression).
- Title SBS (`tools/title_sbs_verify.py`, name=mure2_port, 6/8 instants before wall-clock cap):
  cs150 0.948, cs464 0.831, cs779 0.887, **cs1093 0.655**, cs1407 0.886, cs1721 0.922 — vs
  intsync2 baseline 0.948/0.691/0.873/0.639/0.877/0.916. No regressions. cs1093 +0.016 only,
  consistent with the attribution above (in the title framing the Mure2 clusters are outside
  the draw-culled range / marginal on screen; the cs464 +0.14 is the earlier wordmark-decoration
  commits, which the intsync2 baseline predates — confirmed by a before/after SoH-frame diff
  that is entirely the wordmark, `scratch/title_ab/z_soh_beforeafter_cs1093.png`).

## Remaining cs1093 gap — where the 0.65 actually lives (for the next session)

1. Fireglow wash intensity/coverage on the wordmark (~40% of the diff) — open residual with
   decomp pointers in `2026-07-10-fireglow-combiner-and-terrain-decomposition.md` §1 (alpha
   staging/fade-in timing, title_logo_actor.md §5.2/§6.2).
2. Rider (Link/Epona) framing at the right edge (~33%) — pose/position offset vs oracle.
3. ~~PICA fog LUT port (blocked on LUT-fill decompile)~~ — **CORRECTION (2026-07-14, later
   session, see `2026-07-14-fog-lut-already-ported.md`): this line was stale/wrong when
   written.** The LUT-fill was decompiled and the fog port SHIPPED on 2026-07-10 (commit
   `19081f9a`, ground truth in `oot3d-decomp/docs/title_env_lighting.md` §13; SoH-side
   journal `2026-07-10-title-3ds-fog-port.md`). Re-verified in the later session:
   `fog3dNode()` + `gZelda3dFog3d`/`Zelda3D_Fog3dSet` in
   `Shipwright/libultraship/src/fast/{zelda3d_gl,zelda3d_sdl3gpu}.cpp` still implement the
   exact node/128-entry-LERP structure from §13, unchanged since the port commit, and are
   still wired from `title_presentation.cpp:405`. Nothing to port here; this entry mis-listed
   it as open, likely leftover from drafting before the fog-port session's numbers were
   folded in. The oracle-side terrain ~1.9x brightness question (the per-light
   ambient-summation mechanism, §10/§11 of `title_env_lighting.md`) remains genuinely open —
   that part of the line was correct.
