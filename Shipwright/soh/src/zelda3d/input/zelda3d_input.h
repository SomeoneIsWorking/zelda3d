// Zelda3D input module — ONE home for the input-adjacent free functions/globals that used to be
// scattered across zelda3d.c (locomotion/button-hold REPL harness, Xbox/keyboard glyph-device
// state) and zelda3d_model.cpp (headless keyboard-inject verification shim). Pure
// code-motion pass (Phase 1 reorg, debug_journal/2026-07-15-phase1-input-consolidation.md) — no
// behavior change to any of the moved logic.
//
// This does NOT yet own the whole input path (event-time SDL->ControlDeck, poll-time
// WriteToOSContPad/UpdatePad still live in Shipwright/libultraship/ — see
// docs/lus_input_architecture.md). What it DOES centralize: the single input-diagnostics enabled
// check (previously duplicated as a private static lambda in both
// libultraship/src/ship/controller/controldeck/ControlDeck.cpp and
// libultraship/src/libultraship/controller/controldeck/ControlDeck.cpp) — libultraship
// forward-declares Zelda3D_DbgInputEnabled() locally (extern "C") rather than including this
// header, preserving the one-directional soh-depends-on-libultraship layering (same pattern as
// Zelda3D_MeasureResult / the native-HUD entry point in Gui.cpp).
#ifndef ZELDA3D_INPUT_ZELDA3D_INPUT_H
#define ZELDA3D_INPUT_ZELDA3D_INPUT_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// #20 — headless keyboard verification. Feed a raw KbScancode through the EXACT same path the SDL
// window handler uses (Fast3dWindow::KeyDown/KeyUp -> ControlDeck::ProcessKeyboardEvent). Returns
// 1 if the deck consumed the event, 0 if not, -1 if no control deck. Used by the REPL `key`
// command (zelda3d.c). Moved from zelda3d_model.cpp (was orphaned there, unrelated to the model
// loader it lived next to).
int Zelda3D_InjectKey(int scancode, int down);

// Inject the held `walkhold` control-stick value / `btnhold` button mask / ztarget / #71 pause-nav
// / #16 FP_REPRO edges into player input. Called from Play_Main right BEFORE Play_Update (z_play.c)
// — input is re-sampled each frame, so setting it later would be clobbered. No-op unless one of
// the REPL debug harnesses above is active. Moved verbatim from zelda3d.c.
void Zelda3D_WalkInject(PlayState* play);

// REPL command handlers for `walkhold <frames> [stickX] [stickY]` and `btnhold <hexmask> <frames>`
// — moved out of zelda3d.c's REPL else-if chain alongside the state they mutate. zelda3d.c's REPL
// dispatcher still does the `strcmp(cmd, "walkhold")` / `"btnhold"` routing (REPL command routing
// itself is a monolithic dispatcher, out of scope for this phase — see the empty
// soh/src/zelda3d/repl/ scaffold left by Phase 0 for where that consolidation belongs); these two
// functions are the actual command bodies.
void Zelda3D_Input_HandleWalkHoldCmd(const char* line, const char* outPath);
void Zelda3D_Input_HandleBtnHoldCmd(const char* line, const char* outPath);

// #32 — Xbox face-button HUD glyphs. -1=uninit (reads env ZELDA3D_XBOXUI, default on). z_parameter.c
// reads this to swap the per-button texture. Moved from zelda3d.c.
int Zelda3D_XboxBtnEnabled(void);

// #32 hotswap — last-used input device: 0=gamepad (Xbox glyphs), 1=keyboard (key labels). Updated
// by the C++ LUS input layer (Controller.cpp) on every key/gamepad event. Moved from zelda3d.c.
int Zelda3D_InputDevice(void);
extern int gZelda3dInputDevice;
extern int gZelda3dXboxBtn;

// Consolidated input diagnostic gate (`log input 1`) — the single source of truth for "is the headed
// keyboard-input diagnostic enabled" (debug_journal/2026-07-15-keyboard-headed-v2.md). Read once,
// cached. libultraship's two ControlDeck.cpp files (Ship:: event-time, LUS:: poll-time) each
// forward-declare this locally and call it instead of keeping their own private copy.
// Seed gZelda3dInputDevice from ZELDA3D_INPUTDEV. Called once from Zelda3D_RegisterHostHooks at
// startup, before any device event can write it.
void Zelda3D_InputDeviceInit(void);

int Zelda3D_DbgInputEnabled(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_INPUT_ZELDA3D_INPUT_H
