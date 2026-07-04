# Plan — port OoT3D's title-demo scripted path into SoH

**Direction confirmed by user (2026-07-04):** "we need the same scripted
path". Stop the CS-writer RE arc (7 falsified probes); port Az's
title-demo playback bytes so SoH's title scene runs the identical
per-frame script.

## Ground truth (verified this session)

1. **Az's title-demo IS a normal Play state.** Prior
   `soh3d-oot3d-title-not-play` memory was FALSIFIED. Live play ptr at
   VA `0x00539F98` = `0x0871E854` (=play+0x14). Play_Main
   (FUN_0045238c) runs on PlayState @ `0x0871E840`. See
   `oot3d-decomp/docs/title_gamestate_v2.md`.

2. **Scene is 0x51 = spot00 = SCENE_HYRULE_FIELD** on both engines.
   Same scene id, same shape. The delta is the cutscene bytes + actor
   scheduling that drive the flyover.

3. **PICA fragment lighting is GLOBALLY DISABLED at title** (73k
   triangles surveyed, all `lit_dis=1`). See
   `oot3d-decomp/docs/title_lighting_disabled.md`. There is no
   `CS_SET_LIGHTING` handler to port for title — palette values drive
   CMB combiner constants + vertex colors, not the PICA lighting unit.

4. **Camera + landscape + BlueSky dome + moon disc are solved** in
   SoH3D (see `title_render_pipeline_scope.md` "Solved" section). The
   `#111` world-shade is opt-in and correctly-off at title (would
   ADD lighting Az doesn't apply).

## What SoH's title currently does

```
ovl_title (Title_Init/Main)               // N64 Rogo splash
  ↓ this->exit
ovl_opening (Opening_SetupTitleScreen)    // 1-frame passthrough
    gSaveContext.cutsceneIndex = 0xFFF3
    gSaveContext.sceneSetupIndex = 7      // "cutscene setup" for spot00
    → SET_NEXT_GAMESTATE(Play_Init)
Play_Init → spot00 (SCENE_HYRULE_FIELD)
Play_Main + z_demo runs cs 0xFFF3          // N64 title-cs flyover
```

The cs 0xFFF3 script is baked into spot00's N64 setup-7 scene data
(consumed by `Cutscene_Update` in `z_demo.c`).

## What Az does differently

Same scaffolding (Play_Main on scene 0x51), but the cutscene bytes
attached to setup-7 come from the OoT3D-refactored ROM and drive:
- Different camera keyframes (already partially ported via
  `kZelda3dTitleEye/At/Up`, but that's a STATIC snapshot — Az's cam
  animates)
- Actor spawn schedule (Player + Epona pre-spawned per `title_gamestate_v2.md`)
- Timing to file-select transition (the `START` press)
- Env-palette advance (drives CMB combiner colors, NOT PICA lights)

## The port strategy

**Phase 1 — locate + dump the OoT3D title-cs bytes**
- Extend Ghidra RE for `Scene_CmdCutsceneData` = `FUN_0023449c` (per
  `scene_command_handler.md`) to identify where the cs script pointer
  is stored in spot00's ZSI.
- Decode spot00's ZSI header from the OoT3D ROM (asset provider
  already extracts spot00.zar; parse its scene-level commands to find
  the cutscene-data command payload).
- Dump the cs script as a byte blob into `soh/src/zelda3d/title_cs.inc`.

**Phase 2 — OoT3D cs opcode decode**
- The OoT3D cs format is a superset/variant of N64's. Enumerate the
  opcodes actually used by the title-cs (start small: probably ~10–20
  distinct opcodes). Map each to its behavior via Ghidra decomp of
  the cs interpreter (Az's cs_update-equivalent).
- Document the opcode table in `oot3d-decomp/docs/cutscene_opcodes.md`.

**Phase 3 — bolt an OoT3D-cs interpreter into SoH**
- Do NOT try to translate OoT3D cs bytes → N64 cs bytes (format
  drift risk). Instead: implement `Zelda3D_Cutscene_Update(play)` in
  `Shipwright/soh/src/zelda3d/zelda3d_cutscene.c` that consumes
  OoT3D-format bytes. Called from `Play_Main` when
  `gSaveContext.cutsceneIndex == 0xFFF3` AND scene is spot00 AND we're
  in the title path.
- Existing N64 `Cutscene_Update` is preserved untouched; the 3DS
  interpreter runs INSTEAD OF it during title-demo only.

**Phase 4 — actor scheduling**
- Pre-spawn Player + Epona per Az's title state (already largely
  handled by SoH's scene 0x51 default actor list; verify via
  `compare actors` in harness).
- Any additional actor slots (rider anims, ambient) — spawn them from
  the ported cs interpreter's `SPAWN_ACTOR` opcode.

**Phase 5 — verify byte-for-byte parity**
- Per-frame A/B: for each SoH title-cs frame, compare (camera basis,
  player pos, epona pos, envCtx palette slot, gfx display list byte
  hash) vs Az at the corresponding cs frame.
- Iterate on the ported interpreter until all four dims match for the
  full title-demo run (~30 sec loop).

## Concrete first commit

**Extend `oot3d-decomp/tools/ghidra_scripts/DecompDump.py`** (or add a
new script) to:
1. Decomp `FUN_0023449c` (Scene_CmdCutsceneData handler)
2. Decomp `FUN_002e4de4` (scene command dispatcher — locate cmd 0x17
   handler entry)
3. Emit both to `oot3d-decomp/scratch/title_cs_dispatch/*.c`

Then hand-read the decomp to identify the cs-script-pointer field on
the scene ZSI blob for spot00, and where it gets stored on envCtx or
csCtx during scene init.

## What to STOP doing

- **Stop probing Env_Update for a CS_SET_LIGHTING dispatcher.** It
  doesn't drive title lighting (PICA lighting is off).
- **Stop `force titletime` VBLANK nudges.** They produce
  non-deterministic slot values that mislead RE.
- **Stop tuning world-shade constants at title.** They correctly
  don't apply (Az has no vertex lighting on title landscape).
- **Stop building static per-frame envCtx replay overrides.** The port
  target is the SCRIPT, not the state snapshots.

## Files (read-order for next session)

1. `oot3d-decomp/docs/title_gamestate_v2.md` — solved boot chain
2. `oot3d-decomp/docs/title_lighting_disabled.md` — PICA off proof
3. `oot3d-decomp/docs/title_render_pipeline_scope.md` — remaining gaps
4. `oot3d-decomp/docs/scene_command_handler.md` — scene ZSI dispatcher
5. This plan
