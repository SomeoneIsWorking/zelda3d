// Z3DInputDemo.h — Zelda3D headless input-demo driver for native MM (verification harness).
//
// Proves the SHARED libultraship scripted-input seam (ship/controller/scripted/ScriptedInput.h)
// actually drives MM's real input path: with the gate set, it runs a fixed timeline that walks
// Link forward, stops, then presses START to open the pause/subscreen menu — no physical
// controller. It is per-game glue only because it reads gPlayState for quantitative walk-proof
// (Link's world position); the INPUT itself flows through the game-agnostic ScriptedInput API,
// exactly as OoT (zelda3d) will. Off unless ZELDA3D_MM_INPUTDEMO is set (non-empty).
//
// Ticked once per frame from GameState_GetInput() BEFORE PadMgr reads the pad, so the synthetic
// state applies the same frame.
#ifndef Z3D_INPUT_DEMO_H
#define Z3D_INPUT_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

// Advance the demo one frame. No-op unless ZELDA3D_MM_INPUTDEMO is set.
void Z3D_InputDemo_Tick(void);

#ifdef __cplusplus
}
#endif

#endif // Z3D_INPUT_DEMO_H
