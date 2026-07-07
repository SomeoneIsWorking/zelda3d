// OoT3D " BDQ" cutscene playback for SoH3D — title-demo scripted path.
//
// Ground truth: OoT3D FUN_002c5ba0 (cs interpreter) + FUN_0033cb90 (OP97
// camera spline evaluator) + FUN_003087a4 (Grezzo keyframe curve).
// Format + field roles verified byte-exact against the live Az oracle
// (|d_eye|=0.00 over 300 frames) — see debug_journal/
// 2026-07-07-op97-camera-decode-verified.md and tools/oot3d_cs_camera.py
// (the reference Python implementation this is a port of).
#ifndef ZELDA3D_CUTSCENE_H
#define ZELDA3D_CUTSCENE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Loads the title cutscene from /scene/spot99_info.zsi (scene cmd 0x18
// alt-header entry[0] -> cmd 0x17 -> " BDQ" stream). Idempotent; returns
// 1 when the cs is available, 0 on failure (missing ROM etc).
int Zelda3D_TitleCsLoad(void);

// Total loop length in frames (end_frame from the " BDQ" header; 2400).
int Zelda3D_TitleCsEndFrame(void);

// Evaluate the OP97 camera spline at a cs frame (0..end_frame).
// Outputs world-space eye/at, vertical FOV in degrees, and the camera up
// vector (roll applied around the view direction, sign verified vs Az).
// Returns 1 if a spline segment covers the frame, 0 otherwise (caller
// should hold the previous camera).
int Zelda3D_TitleCsCamera(int frame, float eye[3], float at[3],
                          float up[3], float* fovDeg);

// Title cs frame cursor, advanced once per applied title frame by the
// caller; wraps at end_frame. Exposed for the parity harness so A/B runs
// can read/pin the SoH-side frame.
// Active rider (player) cue for a cs frame — op-0x0a records (N64
// CsCmdActorAction shape). p0/p1 = segment start/end world pos, yaw =
// authored facing (binang). Returns 0 when no cue covers the frame.
int Zelda3D_TitleCsRiderCue(int frame, int* cueIndex,
                            float p0[3], float p1[3],
                            int* startF, int* endF, int16_t* yawBinang);

int  Zelda3D_TitleCsFrame(void);
void Zelda3D_TitleCsSetFrame(int frame);
int  Zelda3D_TitleCsAdvance(void);   // returns new frame

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CUTSCENE_H
