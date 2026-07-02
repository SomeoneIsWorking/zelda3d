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

## Root cause: **PARTIALLY RE'd** (`build/decomp/001b4944.c` = EnHy_Draw)

The per-type color mechanism is now decoded. Two tables:

- **`DAT_001b4c70`** — per-type "object id" table, stride 0x18 (first short = OBJECT_AHG (0x107) /
  OBJECT_BOJ (0x108) / OBJECT_CNE (0x10C) / OBJECT_BOB (0x111) / …). Selects which body archetype
  code path runs (each calls a different set of `FUN_0036932c` = `Model_ApplyMatAnim` on
  specific material indices — e.g. BOJ applies mat 2, 3; AHG applies 1, 2, 3, 4; BOB applies 1–8).

- **`DAT_001b4c70 - 0x348`** — per-type "body color override" table, stride 0x28 per EN_HY_TYPE.
  Layout PER ENTRY (from the switch at 0x001b4b10):
  ```
  +0x00  u8   ??? (reset/pre-material index; -1 = skip)
  +0x01  u8   ??? (BOB path only; -1 = skip)
  +0x02  u8   materialIndex_A  (target for constant 4; -1 = skip)
  +0x03  u8   materialIndex_B  (target for constant 3; -1 = skip)
  +0x04  RGBA colorA[4]         (constant 4 override — clothing colour A)
  +0x14  RGBA colorB[4]         (constant 3 override — clothing colour B)
  +0x18  ??? (16 bytes trailing, unused in this switch — verify)
  ```
  The call is `FUN_00357a50(model, matIdx, constIdx, &color, 1)` =
  `Model_SetMaterialConstantColor(model, matIdx, constIdx ∈ {3, 4}, u8[4] rgba)`.

  Cases 6, 12 (index 0xC), 18 (0x12) are SKIPPED — no override applies (default palette).

- Case 8 (BOJ variant): additionally overrides constant 2 on mat 2 with `DAT_001b4c74`
  (probably a shared "generic clothes" colour).
- Case 11 (0xB): overrides constant 2 on mat 2 with a 4-byte-inline colour at `DAT_001b4c78`.

## Fix direction — MULTI-STEP (not landed)

1. **Extract the tables from the OoT3D binary.** Read the raw bytes at
   `DAT_001b4c70 - 0x348 + type*0x28` for each type 0..0x14, and at `DAT_001b4c70 + type*0x18`
   for the object-id mapping. Bake into a static C array in the port. Tooling: extend
   `oot3d-decomp/tools` with a `dump_enhy_body_table.py` that reads the .elf sections at
   those absolute VAs (they're const-data in .rodata).

2. **Add CMB material-constant-color override infrastructure to SoH3D.** Currently the
   facial-CMB path swaps a TEXTURE frame (see `Zelda3D_ModelSetTextureFrame`); there is no
   per-actor per-material *constant colour* override path yet. Needs: a per-actor array of
   (matIdx, constIdx, rgba) written before submit, honored by the shader/emission code.
   Distinct feature; do not conflate with facial-frame texture swap.

3. **Wire `TownsfolkBehavior::applyDrawOverrides`** to read
   `(actor->params & 0x7F)` and apply the two colour overrides for that type. Skip when
   the table row indicates -1 (default palette).

Each step is a separately-verifiable increment.

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
