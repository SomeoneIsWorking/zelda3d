# 2026-07-14 — Title demo promoted to a first-class scene (SCENE_TITLE / spot99)

## What changed

The title demo previously ran on the real `SCENE_HYRULE_FIELD` PlayState and
used a runtime override (`Zelda3D_Title_SceneName()`) to swap the OoT3D
render/collision layer's scene folder from `spot00` to `spot99` only while
the title presentation was active. This session promoted the title demo to
a proper first-class scene:

- `Shipwright/soh/include/tables/scene_table.h`: new entry `0x6E`
  `DEFINE_SCENE(spot99_scene, none, SCENE_TITLE, SDC_HYRULE_FIELD, 0, 0)`.
- `Shipwright/soh/include/tables/entrance_table.h`: new entrance group
  `0x614`–`0x61B`, `ENTR_TITLE_0[..7]`, all → `SCENE_TITLE` spawn 0. Eight
  layers are defined (only layer 0 == `ENTR_TITLE_0` is referenced) because
  the title boots with `sceneSetupIndex = 7`, and the entranceIndex +
  sceneSetupIndex lookup needs that many layers present to resolve.
- `Shipwright/soh/soh/z_play_otr.cpp` (`OTRPlay_SpawnScene`): `SCENE_TITLE`
  has no OTR asset of its own — its N64 actor/object/collision/cs data is
  byte-identical to spot00 (per `oot3d-decomp/docs/title_scene_spot99.md`
  §3/§4) — so the N64-side scene resource is loaded from
  `gSceneTable[SCENE_HYRULE_FIELD]` while `play->sceneNum` stays
  `SCENE_TITLE`.
- `Shipwright/soh/src/code/z_scene.c` (`Object_InitBank`): object-bank size
  for `SCENE_TITLE` matches `SCENE_HYRULE_FIELD` (1024000).
- `Shipwright/soh/src/overlays/gamestates/ovl_opening/z_opening.c`
  (`Opening_SetupTitleScreen`): sets `gSaveContext.entranceIndex =
  ENTR_TITLE_0` so `Play_Init` boots straight into `SCENE_TITLE`.
- `Shipwright/soh/src/zelda3d/zelda3d_scene_names.inc`:
  `kZelda3dSceneNames[SCENE_TITLE] = "spot99"` — the OoT3D render layer now
  resolves the folder name straight from `sceneNum`, no override needed.
- `title_presentation.cpp`/`.h`: `shouldBeActive()` now gates on
  `play->sceneNum == SCENE_TITLE` (was `SCENE_HYRULE_FIELD`).
  `Zelda3D_Title_SceneName()` is retired to a `nullptr` stub (kept for link
  compatibility; grepped — no remaining callers besides its own definition
  and the retirement comments).
- `zelda3d.c` (`Zelda3D_SceneName`): the `Zelda3D_Title_SceneName()`
  override seam is removed; the sceneNum → `kZelda3dSceneNames` table
  lookup now does the whole job uniformly for every consumer (room draw,
  terrain-warp collision build, cam-lift floor query, meshfloor REPL).
- `title_rider.cpp`: comment-only update (SCENE_HYRULE_FIELD →
  SCENE_TITLE reference in an explanatory comment; behavior unchanged —
  EnHorse_Init's kill-branches gate on ranch/stable/fortress scenes, none
  of which is SCENE_TITLE, so the rider spawn still survives).

`SCENE_ID_MAX` becomes `0x6F`; `SCENE_UNUSED_6E` (an already-unused
placeholder id) shifts to `0x6F` — confirmed via grep that every consumer
in the tree (`CrashHandlerExt.cpp`, `gameplaystats.cpp`,
`debugSaveEditor.cpp`, and all randomizer files) references the enum
symbol `SCENE_ID_MAX`/`ENTR_MAX`, never a hardcoded numeric literal, so
they track the shift automatically.

## Build

`Shipwright/build-cmake` — `cmake --build . --target soh -j4`. Binary was
already newer than every changed source file (`ninja: no work to do`),
i.e. the draft had already been built clean with no compile errors before
this session started; re-verified no stale/incremental drift by comparing
mtimes (`soh.elf` @ 2026-07-11 17:15:06, newest changed source @ 16:51:14).

## Verification (headless, `ZELDA3D_HEADLESS=1 ZELDA3D_WARP= tools/zelda3d_game.sh start`)

Boots the real `Opening_SetupTitleScreen` → `Play_Init` path (not an
entrance-warp shortcut) so this exercises the actual first-class-scene
boot flow end to end.

**a) `play->sceneNum == SCENE_TITLE` (0x6E):**
```
$ python3 tools/zelda3d_repl.py cmd "posinfo"
scene=0x6e link=(-6232,61,4899) yaw=10913 | cam eye=(-4073,58,5215) at=(-4151,75,5256) | focus=(-6229,149,4902)
```

**b) OoT3D spot99 room geometry + collision actually load** (grep of
`scratch/logs/run.log`):
```
[Zelda3D] loaded scene collision /scene/spot99_info.zsi: 306 verts, 496 polys, 17 surface types
[Zelda3D] loaded scene-room model 1000 (/scene/spot99_0_info.zsi): 29 groups, 30 textures
```

**c) Title presentation active** (camera + wordmark + rider):
```
Cutscene_HandleConditionalTriggers - entranceIndex: 0x614 cutsceneIndex: 0xfff3
[Zelda3D] title cs: 15 rider cues, 2 misc cues, 1 fade cues, hasDest=1
[Zelda3D] title cs loaded: 8 camera segments, end_frame=2400
[Zelda3D] auto-loaded model 2013 (/actor/zelda_mag.zar): cmb 'Model/title_logo_us.cmb' ...
[Zelda3D] auto-loaded model 2014 (/actor/zelda_mag.zar | title_logo_us) ...
[Zelda3D] auto-loaded model 2015 (/actor/zelda_mag.zar | g_title.cmb) ...       # wordmark
[Zelda3D] auto-loaded model 2016 (/actor/zelda_mag.zar | copy_nintendo.cmb) ... # copyright
```
`entranceIndex: 0x614` == `ENTR_TITLE_0`, confirming the boot actually went
through the new entrance, not a stale fallback.

**d) Cutscene data resolves and advances (no black-screen hang)** — five
`posinfo` polls 2s apart, then a further ~14s of idle, all show the
camera eye/at and rider (`link`) position/yaw changing frame to frame
along the scripted path (not frozen, not garbage):
```
scene=0x6e link=(-5512,119,5315) yaw=10913 | cam eye=(3440,50,6330) ...
scene=0x6e link=(-5367,115,5399) yaw=10913 | cam eye=(3206,151,5984) ...
scene=0x6e link=(-5225,112,5481) yaw=10913 | cam eye=(2958,262,5659) ...
scene=0x6e link=(-5078,120,5566) yaw=10913 | cam eye=(2707,372,5310) ...
scene=0x6e link=(-4932,120,5650) yaw=10913 | cam eye=(2551,413,4805) ...
...(~14s later)...
scene=0x6e link=(3515,218,5799) yaw=-28310 | cam eye=(4818,111,2524) ...
```

**e) No crash over the run** (~30s+ total, well past the 8-camera-segment
title cs's first act): `tools/zelda3d_game.sh status` reported the single
`soh.elf` pid still alive at the end; `scratch/logs/run.log` has no
`error`/`crash`/`assert`/`fatal` lines other than the pre-existing benign
`Failed add SDL game controller mappings from "./gamecontrollerdb.txt"`
warning (present on every launch, unrelated to this change).

## Parity/oracle compare — SKIPPED, harness not warm

`tools/title_ab.py calibrate 600` requires
`scratch/title_settled.state` (an Az save-state right before the title
cutscene, used as shared t=0 for both engines). That state file is not
present in this checkout's `scratch/` (gitignored, machine-local,
apparently not regenerated since the last clean). Regenerating it is a
separate multi-step harness task, not a cheap one-shot check, so per the
task instructions it was skipped rather than treated as a blocker. The
direct in-game verification above (scene id, spot99 asset loads, cs
cue counts, frame-to-frame camera/rider motion, no crash) is what
confirms the scene-table swap didn't regress the runtime path; it does
not by itself re-confirm the previously-measured pixel-level title
parity number (96%, per `2026-07-14`'s prior mat10/11 journal entry) —
that would need the oracle harness re-warmed.

## Residuals / follow-ups

- `scratch/title_settled.state` should be regenerated (or its generation
  script re-run) so `title_ab.py` is usable again for parity re-checks
  without a cold-start detour.
- `Zelda3D_Title_SceneName()` is now a dead `nullptr` stub kept only for
  link compatibility — safe to delete in a follow-up cleanup pass once
  nobody else has a stale build referencing it.
