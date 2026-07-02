# 2026-07-02 — En_Hy Hylian townsfolk render as all-white (Market Day)

## Sweep tool
`ZELDA3D_HEADLESS=1 python3 tools/parity_ab.py 0xB1 --time 0x8001 --name marketD_synced`
(This is the FIRST parity_ab run after fixing the oracle time-sync — see `parity_ab.py`
+ `oot3d-decomp/tools/link_ctl.py warp <ent> <dayTime>`. Before that fix, oracle used
whatever dayTime its save happened to carry, so day-vs-night forks like Market compared
different scenes silently.)

Composite: `scratch/screenshots/ab_marketD_synced_cmp.png`.

## Divergence
En_Hy townsfolk (actor 0x16E) around the Market fountain render with **uniformly WHITE
clothing** in SoH3D. In the OoT3D oracle they wear COLORED clothing per params:
- 0x0782 → green dress
- 0x0789 → blue dress
- 0x078A → orange dress  
- 0x078C → pink shirt / purple pants
- 0x0003 → magenta pants
- 0x0001 → blue shirt
- 0x0000 → red shirt

## Reproducing
1. `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh start 0xB1 0x8001`
2. `tools/zelda3d_repl.py cmd "actorsnear"` — enumerates the En_Hy variants
3. `tools/zelda3d_repl.py cmd "asel 0x16E N"` + `acam` + `shot` to frame each

## What is + isn't already done
Ported (Shipwright/soh/src/zelda3d/behaviors/actor/townsfolk.cpp):
- Head/torso track (per-archetype head/torso bone, RotateX·RotateZ)
- Eye material-anim (curEyeIndex → CMB material 3 for men, 1 for women)

MISSING: **per-EnHy-type BODY COLOR/CLOTHING variant**. The archetype table binds a body
zar (boj/ahg/bji/aob/…) but treats every variant of the same archetype identically. The
N64 game swaps the palette per EN_HY_TYPE_XXX; OoT3D likely swaps a body texture OR
overrides a material color per type in EnHy_Draw (@ 0x1b4944) or an OverrideAllLimbDraw
we haven't yet decompiled.

## Root cause: **OPEN** — needs RE
- Decompile `EnHy_Draw @ 0x1b4944` in the OoT3D binary and identify the per-EnHy-type
  material/texture swap point. Candidates: `Material_SetColor` on the body material, or
  a `Model_SetTexture` overriding one texture slot. The BODY MATERIAL is CMB material
  index — TBD (eye is 3 for men, 1 for women; body might be 0 or 2).
- Log the material chain during a live draw: dump the CMB materials for zelda_boj.cmb
  before/after the OoT3D Draw call. Where the RGBA differs across enHyType is the color
  channel to override in SoH3D.
- Data source: the OoT3D En_Hy overlay has a per-type constant table (color/texture id).
  Decomp target file: `sys/OoT3D_decompiled/actor/EnHy.cpp` (path TBC in ghidra project).

## Fix direction (not landed)
Extend `TownsfolkBehavior::applyDrawOverrides` with an `applyBodyMaterial(modelId, type,
kBodyColors[type])` step, keyed on `(actor->params & 0x7F)` = EN_HY_TYPE. Values from RE.

## Follow-ups + related workflow findings
- **Tooling improvement (LANDED)**: `link_ctl.py warp` now accepts `[dayTime]` and
  writes `gSaveContext.dayTime` before triggering the transition, and `parity_ab.py`
  forwards `--time` to the oracle warp. Every future parity_ab run is now
  time-of-day-faithful. This was blocking honest A/B — see
  [[2026-07-02-market-day-parity-sweep]] finding #2 (Market day/night fork).
- Related lighting-tint darkness in indoor scenes (Goron City: `tint=(32,2,117)`; Zora's
  Fountain underwater near-black) is **NOT worked** per user directive
  [[soh3d-stop-microtuning-lighting]] + [[soh3d-lighting-port]] — worldshade stays
  opt-in, do NOT tune SceneTint coefficients.
