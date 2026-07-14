# Boot: remove the N64-logo screen — bake in, no toggle (2026-07-14)

## Rationale

OoT3D (ground truth) has no N64/console-logo screen at boot — the 3DS title flow goes straight
into the attract sequence (the SCENE_TITLE/spot99 cs, see
`debug_journal/2026-07-14-title-spot99-first-class-scene.md`). SoH3D's boot chain still ran the
N64-era `ovl_title` "ConsoleLogo" gamestate first
(`title_setup.c:TitleSetup_Init` → `z_title.c:Title_Init` → `Title_Main`/`Title_Draw` fade-hold →
`z_opening.c:Opening_Init` → `Play_Init`). This is a divergence from ground truth, not an
enhancement to keep behind a CVar.

## What actually drew the logo

SoH's `Enhancements/cosmetics/CustomLogoTitle.cpp` unconditionally hooks `OnZTitleInit`
(`COND_HOOK(OnZTitleInit, true, ...)`) and replaces `titleContext->state.main` with
`CustomLogoTitle_Main`, which — depending on `CVAR_SETTING("BootSequence")`
(`gBootSequence`, default `BOOTSEQUENCE_DEFAULT` = 0) — draws the LUS ship logo, then the N64
logo, each with its own ~230-frame fade-in/hold/fade-out (`Title_Calc`: 255→0 over ~85 frames at
`addAlpha=-3`, hold `visibleDuration=0x3C`=60 frames, fade back 0→255). The plain N64-original
`z_title.c:Title_Main`/`Title_Draw` were themselves already dead in practice (this hook always
wins), just never noticed because both draw *some* logo.

## Side-effect audit (what Title_Init/Title_Main/Title_Destroy do beyond drawing)

- `TitleSetup_InitImpl` (unchanged, upstream of Title): `SaveContext_Init()`.
- `Title_Init`: `gSaveContext.fileNum = 0xFF`; `GameInteractor_ExecuteOnZTitleInit(this)` hook
  point (only consumer in this fork is `CustomLogoTitle.cpp`'s main-swap + the dead
  `BootSequence` CVar handling — see below); `R_UPDATE_RATE=1`/`Matrix_Init`/`View_Init` (title's
  own view setup, redundant — `Opening_Init` sets `R_UPDATE_RATE=1`+`Matrix_Init`+`View_Init`
  itself).
- `Title_Main`'s exit branch: `gSaveContext.seqId = NA_BGM_DISABLED`;
  `gSaveContext.natureAmbienceId = 0xFF`; `gSaveContext.gameMode = GAMEMODE_TITLE_SCREEN`
  (redundant — `Opening_SetupTitleScreen` also sets `gameMode = GAMEMODE_TITLE_SCREEN`).
- `Title_Destroy` (run by the engine's `GameState_Destroy` teardown, `graph.c`): `Sram_InitSram`
  → `Save_Init()` + `func_800F6700(gSaveContext.audioSetting)` (audio output mode setup) — the one
  side effect with no equivalent anywhere else in the boot chain. Must be preserved.

## The seam

`z_title.c:Title_Init` now performs the non-visual side effects (`fileNum`, the hook call, the
exit-branch save-context writes) and hands off immediately, mirroring the exact zero-tick pattern
`title_setup.c:TitleSetup_Init` already uses for a pure-setup state: never assign `state.main`,
set `state.running = false`, `SET_NEXT_GAMESTATE(..., Opening_Init, OpeningContext)`, and still
assign `state.destroy = Title_Destroy` so the engine's teardown still runs `Sram_InitSram`.
`Shipwright/soh/src/code/graph.c`'s per-gamestate loop
(`GameState_Init` sets `running=1` *before* calling `init()`, so `init()` can override it; the
`while (GameState_IsRunning(...))` loop then never executes, and `GameState_Destroy` fires
right after) makes this a zero-rendered-frame handoff — not a fast fade, an actual skip.

This is hardwired, not a new toggle: no CVar/env var gates it, and no existing toggle was
repurposed. `Title_Main`/`Title_Draw`/`Title_Calc`/`Title_SetupView` are left in place (still
referenced by `CustomLogoTitle.cpp` at link time) but are now unreachable — nothing ever assigns
`state.main` to trigger them, from any of the three call sites (`title_setup.c`, `z_select.c`
return-to-title, `zelda3d.c`'s debug reload-to-title command).

## Now-dead code (noted, not removed — see reasoning)

`Enhancements/cosmetics/CustomLogoTitle.cpp` (logo draw + `BootSequence`-gated ship/N64-logo
choice + skip-button hook) and `Enhancements/Warping.cpp`'s
`BOOTSEQUENCE_DEBUGWARPSCREEN`/`BOOTSEQUENCE_WARPPOINT` `OnZTitleUpdate` hooks are now fully
unreachable: they all depend on `titleContext->state.main` ticking, which no longer happens.
Left in place rather than ripped out — deleting them touches `SohMenuSettings.cpp`'s `BootSequence`
dropdown and `enhancementTypes.h`'s `BootSequenceType` enum, which is broader surface than this
task (and this project's actual dev-warp mechanism is the unrelated `ZELDA3D_WARP` env var /
`Zelda3D_AutoWarpEnabled()` bypass in `graph.c`, which still works — it overrides `nextOvl`
*before* `TitleSetup_Init` even runs). Flagging for a future dedicated SoH-enhancement cleanup
pass rather than folding it into this fix.

## Verification (headless, `ZELDA3D_HEADLESS=1 ZELDA3D_WARP= tools/zelda3d_game.sh start`)

Boot-to-SCENE_TITLE wall time (`z_play_otr.cpp:72 Scene Init - sceneNum: 0x6e` timestamp minus
process-start timestamp), same build tree, before (`git stash`) vs after (working tree):

| | start | SCENE_TITLE (0x6e) reached | elapsed |
|---|---|---|---|
| before (logo shown) | 13:57:30.398 | 13:57:35.346 | ~4.95s |
| after (logo skipped) | 13:58:07.933 | 13:58:09.042 | ~1.11s |

~3.8s faster — matches the removed logo's fade-in+hold+fade-out (~230 frames @ 60fps ≈ 3.83s).

REPL confirmation once booted: `titlecam` → `scene=110 csState=2` (SCENE_TITLE, title cs
running); `titlecs` → `frame=N end=2400 ...` advancing normally. Pressed START once the cs was
well into its `Display` phase (`titlecs frame=600`, past `fadeIn=345`) via
`btnhold 0x1000 3`; the REPL subsequently stopped replying (`(no reply)`) while
`tools/zelda3d_game.sh status` still reported the process alive — the documented signal that the
game left `Play` for file select (the skip-to-file-select state machine in
`zelda3d/behaviors/title/title_logo.cpp` fired `fireSkipTransition` → `TRANS_TRIGGER_START` →
`GAMEMODE_FILE_SELECT`). No crash, no hang.
