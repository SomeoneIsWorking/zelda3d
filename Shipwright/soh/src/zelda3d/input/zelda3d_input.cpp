// See zelda3d_input.h for the module's scope. Bodies below are moved verbatim from zelda3d.c
// (Zelda3D_WalkInject + the walkhold/btnhold REPL bodies + the Xbox/input-device globals
// and accessors) and zelda3d_model.cpp (Zelda3D_InjectKey) — pure code motion, no behavior change.
#include "zelda3d_input.h"

#include "../zelda3d.h"
#include "../core/zelda3d_log.h"
#include "../player/zelda3d_link.h" // Zelda3D_LinkWalkInject — called from Zelda3D_WalkInject below
#include "ship/Context.h"                              // #20 keyboard-inject verification shim
#include "ship/controller/controldeck/ControlDeck.h"   // #20 ProcessKeyboardEvent path

#include <cstdlib>
#include <cstdio>

// ---------------------------------------------------------------------------------------------
// #20 headless keyboard-inject verification shim (moved from zelda3d_model.cpp, where it was
// orphaned next to the .3ds model loader it has nothing to do with). Feeds a raw KbScancode
// through the EXACT same path the SDL window handler uses (Fast3dWindow::KeyDown/KeyUp ->
// ControlDeck::ProcessKeyboardEvent), so the default keyboard->N64-button mapping
// (LUS::ControllerDefaultMappings) can be exercised and observed live with no physical keyboard.
// Only the SDL physical-key->scancode step is skipped. Used by the REPL `key` command (zelda3d.c).
int Zelda3D_InjectKey(int scancode, int down) {
    auto* ctx = Ship::Context::GetRawInstance();
    if (ctx == nullptr) {
        return -1;
    }
    auto controlDeck = ctx->GetControlDeck();
    if (controlDeck == nullptr) {
        return -1;
    }
    Ship::KbEventType ev = down ? Ship::KbEventType::LUS_KB_EVENT_KEY_DOWN
                                : Ship::KbEventType::LUS_KB_EVENT_KEY_UP;
    return controlDeck->ProcessKeyboardEvent(ev, static_cast<Ship::KbScancode>(scancode)) ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// #32 — show Xbox face-button glyphs (A/B/X/Y) in the in-game HUD button prompts instead of the
// shared N64 colored circle. -1 = uninit (read ZELDA3D_XBOXUI env, default on). The HUD
// (z_parameter.c) reads this and swaps the per-button texture; see Zelda3D_XboxGlyphTex. The Xbox
// glyph must REPLACE the N64 button UI cleanly (user 2026-06-19), not be stacked under the N64
// item icon / do-action label — see z_parameter.c draw sites. Declared `extern` in zelda3d.h; the
// "xboxui" REPL handler (zelda3d.c) writes it directly.
int gZelda3dXboxBtn = -1;
int Zelda3D_XboxBtnEnabled(void) {
    if (gZelda3dXboxBtn < 0) {
        const char* v = getenv("ZELDA3D_XBOXUI");
        gZelda3dXboxBtn = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dXboxBtn;
}

// #32 hotswap — last-used input device. 0 = gamepad (show Xbox glyphs), 1 = keyboard (show key
// labels). Updated by the C++ LUS input layer (Controller.cpp) on every key/gamepad event. The HUD
// glyph draw (Zelda3D_DrawHudBadges) reads this each frame and picks the glyph set. -1 = default
// (read ZELDA3D_INPUTDEV env; if absent, default to keyboard). REPL `inputdev <0|1>` overrides for
// testing (zelda3d.c). Declared `extern` in zelda3d.h.
int gZelda3dInputDevice = -1;
int Zelda3D_InputDevice(void) {
    if (gZelda3dInputDevice < 0) {
        const char* v = getenv("ZELDA3D_INPUTDEV");
        // Default to keyboard (1) when no env set so the headless game shows keyboard glyphs
        // without requiring a connected gamepad. (A real gamepad event flips it to 0 instantly.)
        gZelda3dInputDevice = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dInputDevice;
}

// ---------------------------------------------------------------------------------------------
// `walkhold` REPL: inject a held control-stick value for N frames so Link actually WALKS/RUNS via
// the real locomotion system (the `move` command only teleports, leaving SkelAnime in idle). Used
// to verify the N64-retarget walk cycle live and to capture a big-arm-motion jointTable for a
// better-conditioned per-bone correction. Applied in Zelda3D_WalkInject just before Play_Update.
// File-local: nothing outside this module (WalkInject + its own REPL handler, both here) reads
// these.
static int gZelda3dWalkHoldFrames = 0;
static s8 gZelda3dWalkStickX = 0;
static s8 gZelda3dWalkStickY = 0;

// `btnhold` REPL: inject a held button mask for N frames (verify equipment-state transitions, e.g.
// press B to draw the sword and confirm Link's mesh_id selection switches to sword-in-hand +
// shield-on-arm). Applied in Zelda3D_WalkInject alongside the stick injection. Edge bits are set
// on the first injected frame so a tap (e.g. B to draw/sheathe) registers, then held. File-local,
// same reasoning as the walkhold globals above.
static int gZelda3dBtnHoldFrames = 0;
static unsigned gZelda3dBtnHoldMask = 0;
static int gZelda3dBtnHoldFirst = 0;

// #16 first-person early-load crash repro harness state. Only read/written inside
// Zelda3D_WalkInject below (see its body); file-local.
static int gZelda3dFpRepro = -1; // -1 uninit, 0 off, 1 on
static int gZelda3dFpFrames = 0; // frames elapsed since first controllable

// `ztarget` REPL target (zelda3d.c owns the REPL handler that sets this — the native
// auto-lock-on is re-asserted every frame from Zelda3D_WalkInject below, since autoLockOnActor is
// a one-frame latch by design) and `pause` REPL nav target (zelda3d.c owns that REPL handler too).
// Both cross the zelda3d.c <-> this module boundary now that Zelda3D_WalkInject moved, so they
// stay DEFINED in zelda3d.c (unchanged ownership) and are just extern-declared here, matching the
// existing codebase convention of inline `extern` forward decls at the use site (e.g.
// Zelda3D_InjectKey was extern-declared inline in zelda3d.c's REPL `key` handler before this move).
extern Actor* gZelda3dZTargetActor;
extern int gZelda3dPauseTarget;

// `walkhold <frames> [stickX] [stickY]` — inject a held control stick for N frames so Link really
// WALKS/RUNS via the locomotion system (default stickY=+60 forward; stick range +-60 walk /
// ~+-127 run). For verifying the N64-retarget walk cycle and capturing big-motion jointTables.
// `walkhold 0` cancels. Moved from zelda3d.c's REPL else-if chain; zelda3d.c still does the
// `strcmp(cmd, "walkhold")` routing and calls this.
void Zelda3D_Input_HandleWalkHoldCmd(const char* line, const char* outPath) {
    int frames = 0, sx = 0, sy = 60;
    int nargs = sscanf(line, "%*s %d %d %d", &frames, &sx, &sy);
    if (nargs >= 1) {
        gZelda3dWalkHoldFrames = frames;
        gZelda3dWalkStickX = (s8)sx;
        gZelda3dWalkStickY = (s8)(nargs >= 3 ? sy : 60);
        Zelda3D_ReplReply(outPath, "walkhold frames=%d stick=(%d,%d)", gZelda3dWalkHoldFrames,
                        gZelda3dWalkStickX, gZelda3dWalkStickY);
    } else {
        Zelda3D_ReplReply(outPath, "usage: walkhold <frames> [stickX] [stickY]");
    }
}

// `btnhold <hexmask> <frames>` — inject a held button for N frames (rising edge on frame 1).
// Verify equipment-state transitions: e.g. `btnhold 0x4000 4` taps B to draw/sheathe the sword and
// confirm Link's mesh_id selection switches (sword-in-hand + shield-on-arm). Button bits:
// B=0x4000 A=0x8000 (see libultra controller.h). `btnhold 0 0` cancels. Moved from zelda3d.c's
// REPL else-if chain; zelda3d.c still does the `strcmp(cmd, "btnhold")` routing and calls this.
void Zelda3D_Input_HandleBtnHoldCmd(const char* line, const char* outPath) {
    unsigned mask = 0;
    int frames = 0;
    if (sscanf(line, "%*s %x %d", &mask, &frames) == 2) {
        gZelda3dBtnHoldMask = mask;
        gZelda3dBtnHoldFrames = frames;
        gZelda3dBtnHoldFirst = 1;
        Zelda3D_ReplReply(outPath, "btnhold mask=0x%x frames=%d", mask, frames);
    } else {
        Zelda3D_ReplReply(outPath, "usage: btnhold <hexmask> <frames>  (B=0x4000 A=0x8000)");
    }
}

// Inject the held `walkhold` control-stick value into player input. Called from Play_Main right
// BEFORE Play_Update (input is re-sampled each frame, so setting it later would be clobbered). No-op
// unless a walkhold is active. Drives the real locomotion so Link genuinely walks/runs (vs `move`'s
// teleport) — for verifying the live N64-retarget walk cycle and capturing big-motion jointTables.
void Zelda3D_WalkInject(PlayState* play) {
    if (play == NULL) {
        return;
    }
    if (gZelda3dWalkHoldFrames > 0) {
        play->state.input[0].cur.stick_x = gZelda3dWalkStickX;
        play->state.input[0].cur.stick_y = gZelda3dWalkStickY;
        play->state.input[0].rel.stick_x = gZelda3dWalkStickX;
        play->state.input[0].rel.stick_y = gZelda3dWalkStickY;
        gZelda3dWalkHoldFrames--;
    }
    if (gZelda3dBtnHoldFrames > 0) {
        play->state.input[0].cur.button |= (u16)gZelda3dBtnHoldMask;
        if (gZelda3dBtnHoldFirst) {
            play->state.input[0].press.button |= (u16)gZelda3dBtnHoldMask; // rising edge on frame 1
            gZelda3dBtnHoldFirst = 0;
        }
        gZelda3dBtnHoldFrames--;
    }

    // `ztarget` REPL: re-assert the native auto-lock-on every frame (autoLockOnActor is a
    // one-frame latch by design — see the REPL `ztarget` handler). Mirrors how e.g. En_Rd/En_Dh
    // call Player_SetAutoLockOnActor from their own per-frame Update while aggro holds.
    if (gZelda3dZTargetActor != NULL) {
        Player_SetAutoLockOnActor(play, gZelda3dZTargetActor);
    }

    // #71 pause-menu nav: open/switch-page/close via the real kaleido input path (see gZelda3dPauseTarget).
    if (gZelda3dPauseTarget != -1) {
        PauseContext* pc = &play->pauseCtx;
        if (gZelda3dPauseTarget == -2) { // close
            if (pc->state == 0) {
                gZelda3dPauseTarget = -1; // fully closed
            } else if (pc->state == 6 && pc->unk_1E4 == 0) {
                play->state.input[0].cur.button |= BTN_START;
                play->state.input[0].press.button |= BTN_START; // edge: trigger close
            }
        } else if (pc->state == 0) { // closed -> open
            play->state.input[0].cur.button |= BTN_START;
            play->state.input[0].press.button |= BTN_START; // edge: trigger open
        } else if (pc->state == 6 && pc->unk_1E4 == 0) { // open & settled (not mid-rotation)
            if (pc->pageIndex == (u16)gZelda3dPauseTarget) {
                gZelda3dPauseTarget = -1; // arrived
            } else {
                play->state.input[0].cur.button |= BTN_R;
                play->state.input[0].press.button |= BTN_R; // edge: rotate one page right
            }
        }
    }

    // #6/#9 linkgrab: hold the selected actor in front of Link + inject fresh A edges until grabbed.
    // The driver lives in zelda3d_link.cpp (Link policy); it's a no-op unless `linkgrab` armed it.
    Zelda3D_LinkWalkInject(play);

    // #16 FP_REPRO: deterministically engage first-person right as the cold scene-load settles.
    if (gZelda3dFpRepro < 0) {
        const char* e = getenv("ZELDA3D_FP_REPRO");
        gZelda3dFpRepro = (e != NULL && atoi(e) != 0) ? 1 : 0;
    }
    if (gZelda3dFpRepro) {
        Player* pl = GET_PLAYER(play);
        if (pl != NULL && !Player_InCsMode(play)) {
            enum { kWindow = 300, kPress = 3, kRelease = 3, kCycle = kPress + kRelease };
            if (gZelda3dFpFrames < kWindow) {
                int ph = gZelda3dFpFrames % kCycle;
                if (ph < kPress) {
                    play->state.input[0].cur.button |= BTN_CUP;
                    if (ph == 0) {
                        play->state.input[0].press.button |= BTN_CUP; // fresh rising edge each cycle
                    }
                }
                if (gZelda3dFpFrames == 0) {
                    fprintf(stderr, "SOH3D FP_REPRO: start injecting C-up edges (scene=0x%x dayTime=0x%x)\n",
                            play->sceneNum, gSaveContext.dayTime);
                    fflush(stderr);
                }
                // One-shot log when first-person actually engages, so we KNOW the harness works.
                {
                    static int sFpEngagedLogged = 0;
                    Camera* ac = GET_ACTIVE_CAM(play);
                    if (!sFpEngagedLogged && ac != NULL && ac->mode == CAM_MODE_FIRSTPERSON) {
                        sFpEngagedLogged = 1;
                        fprintf(stderr, "SOH3D FP_REPRO: first-person ENGAGED at fpFrame=%d\n", gZelda3dFpFrames);
                        fflush(stderr);
                    }
                }
                gZelda3dFpFrames++;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Consolidated input diagnostic gate (`log input 1`). Was a private static `Zelda3dDbgInputEnabled()`
// lambda duplicated verbatim in both
// Shipwright/libultraship/src/ship/controller/controldeck/ControlDeck.cpp (event-time) and
// Shipwright/libultraship/src/libultraship/controller/controldeck/ControlDeck.cpp (poll-time).
// Single source of truth now; both call sites forward-declare this extern "C" and call it instead
// (libultraship does not include soh headers — one-directional dependency, same as
// Zelda3D_MeasureResult / the native-HUD entry point in Gui.cpp).
int Zelda3D_DbgInputEnabled(void) {
    // Routed through the diagnostic-logger registry (`log input 1` / ZELDA3D_LOG=input) — this
    // extern stays because libultraship's ControlDeck call sites can't include soh headers.
    return Zelda3D_LogEnabled(Z3D_LOG_INPUT);
}
