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

## Fix direction — MULTI-STEP

### Correction on absolute VAs (READ BEFORE PORTING)

`DAT_001b4c70` in the Ghidra decomp is a LITERAL-POOL ENTRY (a `u32` holding the
real table pointer), NOT the table address. Reading `code.bin @ 0x1b4c70` gives
`0x00527a4c` — the **real** object-id table address. The color-override table is at
`0x00527a4c - 0x348 = 0x00527704`. Both tables are const-data in `.rodata`.

### 1. **Extract the tables from the OoT3D binary. — LANDED**
Extractor: `oot3d-decomp/tools/dump_enhy_body_table.py`. Emits `data/enhy_body_colors.inc`,
a plain-C header with `EnHyBodyColorEntry kEnHyBodyColorTable[22]` (u16 objectId +
s8 pre/pre2/matA/matB + float[4] colorA/colorB). Row layout + regen command are in
`oot3d-decomp/docs/enhy_body_colors.md`. Colors ARE float RGBA (I misread as u8
originally — the Ghidra layout note above `+0x04 RGBA colorA[4]` in the earlier
draft is 4×float32, not 4×u8).

2. **Add CMB material-constant-color override infrastructure to SoH3D.** Currently the
   facial-CMB path swaps a TEXTURE frame (see `Zelda3D_ModelSetTextureFrame`); there is no
   per-actor per-material *constant colour* override path yet. Needs: a per-actor array of
   (matIdx, constIdx, rgba) written before submit, honored by the shader/emission code.
   Distinct feature; do not conflate with facial-frame texture swap.

   **Step 2 RE (2026-07-02, from decomp @ oot3d-decomp/build/decomp/00358778.c)**:
   `Model_SetMaterialConstantColor(model, matIdx, constIdx, rgba, mode)` = FUN_00357a50 →
   FUN_00358778(assetHandle=`model[+0x10]`, matIdx, constIdx, rgba, mode). The inner
   function operates on a runtime material struct with **stride 0x124 bytes** stored at
   `assetHandle[+4]`. A per-constant "overridden" flag lives at
   `matBase + 0x0A + constIdx` (set to 1 on any write). Mode enum:
     1 = replace RGB (leaves A); 2 = set A only; 3 = add; 6 = sub; 9 = multiply;
     0xC = multiply RGB + replace A. **EnHy uses mode 1 (replace RGB).**
   The float-4 read/write of a constant slot goes through FUN_00331094 (read) +
   FUN_003688a8 (write) — the actual per-constant byte offset lives inside those.
   Decomp targets remaining: `00368xxx / 00331094` (to pin the exact offset of
   constant N inside the 0x124 struct), and the CMB loader (to pin file→runtime map).

   **Step 2a — extend CmbMaterial to decode all 6 constant colors + expose in sgdump.**
   (Small, verifiable via structured signal, no visual compare needed.) The CMB file
   material block already carries 6 slots at +0xB4..+0xCC (u8 RGBA each) — dumped raw
   on AHG's hyliaman2.cmb: all 6 slots default to (0,0,0,0xFF) black-opaque, matching the
   expectation that the game OVERWRITES the constants per NPC-type at load. The struct
   fields + parse + `SG_DUMP` line are what land in this step; shader wiring in Step 2b.

   **Step 2b — thread `matConst[6][4]` into the shader UBO + reference in the combiner.**
   The current fragment shader combines `PRIMARY_COLOR * TEXTURE0`; the OoT3D combiner
   also references CONSTANT for stages whose `comb_src_rgb[k] == 0x8576`. The referenced
   constant index is not in the current parse — extend `_parse_mats` to also capture the
   TEV constant selector per stage (PICA200 combiner block, currently unparsed) and pass
   the resolved const-color to the fragment shader.

   **Step 2c — per-actor override channel.** Zelda3D_GL_Submit / Zelda3D_Sg_DrawModel
   gains a small `overrides[matIdx] = (constMask, rgba[6][4])` param (mirrors the facial
   `matTex` map). Populated by TownsfolkBehavior::applyDrawOverrides from
   `data/enhy_body_colors.inc`.

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
