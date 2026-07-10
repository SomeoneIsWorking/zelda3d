# Title-demo lifetime ownership — TitlePresentation owns the full 2400-frame loop (2026-07-10)

Closes residual 1 of `2026-07-10-title-arc-closing-measurement.md`: SoH's title-cs cursor froze
at cs frame 811 (~soh_step 1854) and the game exited into N64 attract gameplay (HUD visible in
fsweep_1522/1700/1900), making the ported fade-out (cs 1930), screen-level loop fade (2310–2460)
and loop restart (2400) unreachable in the real flow despite each being verified in isolation.

## Root cause of the 811 freeze / attract exit

The N64-authored title/attract cutscene running in `play->csCtx` (the underlying
SCENE_HYRULE_FIELD title-demo cs, layer picked by `gSaveContext.cutsceneIndex >= 0xFFF0`) has its
OWN, much shorter authored timeline, ending in a **command 0x3E8 terminator**
(`Cutscene_Command_Terminator`, `Shipwright/soh/src/code/z_demo.c`). Unlike every other branch in
that function — which explicitly excludes `GAMEMODE_TITLE_SCREEN` — the `playCutscene` trigger
fires purely on `cmd->startFrame == csCtx->frames` with **no gameMode gate at all**. At the N64
script's own terminator frame it runs the case-switch (`play->nextEntranceIndex` +
`play->transitionTrigger = TRANS_TRIGGER_START`), tearing down the title PlayState into the next
N64 attract-demo scene.

That teardown is what both symptoms measure:

- the "cursor freeze at 811": the ported cursor (`Zelda3D_TitleCsAdvance`) is advanced by
  `TitlePresentation::update()`, which is gated on `shouldBeActive()` →
  `play->sceneNum == SCENE_HYRULE_FIELD`. Once the terminator's transition loads the next attract
  scene, `update()` stops running and the cursor holds its last value (811 was simply where the
  ported cs cursor stood when the N64 terminator's transition completed).
- the "attract gameplay with HUD": the N64 flow's next demo scene, exactly as on N64.

Ground truth (`<oot3d-decomp>/docs/title_gamestate_driver.md`): OoT3D's title is a self-owned
Play-state playing its single 2400-frame cs (spot99's " BDQ" script, end_frame=2400, op-0x3e8
destination = loop restart at 2400, op-0x7c screen fade 2310–2460) in a loop **forever until a
confirm press**. It never exits on its own.

## Fix — lifetime ownership, not a timer constant

`Cutscene_Command_Terminator` now returns immediately while `Zelda3D_Title_IsActive()` — the
same suppression pattern `EnMag_Update` got in 332e1868 for its own instant press-START
transition, applied to the cutscene engine's exit path. With the N64 terminator suppressed:

- `play->sceneNum` never leaves SCENE_HYRULE_FIELD, so `TitlePresentation::update()` keeps
  ticking and `Zelda3D_TitleCsAdvance()` (which already wraps at end_frame=2400) drives the full
  loop. The N64 `csCtx.frames` free-runs past its own script's last command — harmless idle, all
  visible state (camera/dayTime/lighting/rider/sky/overlay) is driven by the ported cursor.
- The loop restart needed **no new re-prime logic**: camera/dayTime/lighting/logo-phase are all
  pure functions of the wrapped cursor, and `TitleRider::step()` already handles the wrap as a
  cue-index discontinuity (teleports to the fresh cue's p0 and reports
  `riderCueDiscontinuity` — the same mechanism authored shot cuts use).
- The already-ported op-0x7c screen fade (`applyScreenFade`, both window halves incl. the
  post-wrap tail remap) and press-START skip (`Zelda3D_TitleLogoStepSkip` → SoH's existing
  title→file-select transition) now fire in their natural windows. The skip's
  `fireSkipTransition` writes `gSaveContext.gameMode = GAMEMODE_FILE_SELECT` before the
  transition, which flips `Zelda3D_Title_IsActive()` off at the scene change — so the suppression
  does not block the *intended* exit, only the N64-authored one.

Files: `Shipwright/soh/src/code/z_demo.c` (suppression + extern decl).

## Verification — free-run, NO pinned cursors (pinning is what masked this)

Boot `ZELDA3D_WARP= ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh start`, poll
`tools/zelda3d_repl.py cmd "titlecs"` / `"titlecam"` while it free-runs from boot:

- cursor passed 811 and kept the title scene: frames 947 / 1149 / 1351 / 1658 / 1809 all with
  `scene=81` (SCENE_HYRULE_FIELD) and the OP97 camera still driving `view.eye` (before the fix
  the game was in attract gameplay by soh_step ~1854 ≈ cs 811).
- logo fade-out fired: shot at cs 1281 shows the full logo overlay; shot at cs 1960 (past
  fadeOut=1930) shows the logo gone, flyover continues.
- loop wrap: 2113 → 2264 → **15** → 168 (wrap at 2400 confirmed, cursor free-running), screen
  fade [2310,2460) straddling it — luminance measured across the wrap in a second free-run:
  mean grayscale 26.0 at cs 2164 (pre-window) → **17.6 at cs 2331 (inside the fade window)** →
  31.1 at cs 15 post-wrap (fade released). Shots title_prewrap_2164 / title_wrapfade_2331 /
  title_loop2_15.
- second full loop ran: 15 → 168 → 487 → 671 → 891 → … → past 811 again → 1960 → 2264 → wrapped
  a second time — the title loops indefinitely, matching the OoT3D-observed behavior.
- press-START skip still works, in TWO decomp-faithful respects:
  - a press at cs **527 (during FADE-IN)** was seen but NOT latched
    (`pressed=1 pressCsFrame=-1`) — correct per §7.2 (latch only in DISPLAY/DONE or natural
    fade-out);
  - a press at cs **1040 (DISPLAY)** latched, and at exactly `elapsed=25` the trace flips
    `transitionTrigger=20 (TRANS_TRIGGER_START)` + `gameMode=2 (GAMEMODE_FILE_SELECT)`:
    `[TITLESKIP] csFrame=1065 pressCsFrame=1040 elapsed=25 transitionTrigger=20 gameMode=2`.
    The title PlayState tears down (REPL goes silent by design — it is polled from
    Play_Update) and a direct X screenshot of the headless display shows the **file-select
    screen ("Please select a file.")**: title_skip_fileselect.png. The suppression does not
    block this intended exit because `gameMode=GAMEMODE_FILE_SELECT` + the scene change flip
    `Zelda3D_Title_IsActive()` off.

Shots (scratch/screenshots/, machine-local, never committed): title_loop1_fadeout.png (cs 1281,
logo up), title_fadeout_1960.png (logo gone), title_prewrap_2164 / title_wrapfade_2331 /
title_loop2_15 (wrap fade dip), title_loop2_start_15.png, title_skip_fileselect.png.
