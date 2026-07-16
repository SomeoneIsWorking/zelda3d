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
int Zelda3D_TitleCsCamera(float frame, float eye[3], float at[3],
                          float up[3], float* fovDeg);

// Title cs frame cursor, advanced once per applied title frame by the
// caller; wraps at end_frame. Exposed for the parity harness so A/B runs
// can read/pin the SoH-side frame.
// Active rider (player) cue for a cs frame — op-0x0a records (N64
// CsCmdActorAction shape). p0/p1 = segment start/end world pos, yaw =
// authored facing (binang). outAction (may be NULL) receives the cue's raw
// action field (0x40/0x41/0x24 per the 15-cue itinerary,
// oot3d-decomp/docs/2026-07-07-rider-cue-port.md) for CSAB selection
// (title_rider_port_spec.md step 4). Returns 0 when no cue covers the frame.
int Zelda3D_TitleCsRiderCue(int frame, int* cueIndex,
                            float p0[3], float p1[3],
                            int* startF, int* endF, int16_t* yawBinang,
                            uint16_t* outAction);

// Time-of-day from op-0x8c cues (latched; title = 4:01 AM). Returns 0
// before the first cue frame.
// Fractional frame (f + Zelda3D_TitleCsSubframe()): cue latch is integer, the +6/cs-frame drift
// advances fractionally so dayTime-derived state moves every engine frame (#149).
int Zelda3D_TitleCsTimeOfDay(float frame, uint16_t* outDayTime);

// spot99's scene light settings (raw ZSI cmd-0x0F 28-byte entries).
int Zelda3D_TitleCsLightSlotsRaw(const uint8_t** outSlots, int* outCount);

// 3DS time-based light schedule (config 0): daytime -> runtime slot pair
// + blend weight (palette entry = slot + 1).
int Zelda3D_TitleCsLightBlend(uint16_t daytime, int* slotFrom, int* slotTo,
                              float* weight);

// Title palette blended at a dayTime per the 3DS schedule (colors + dirs).
int Zelda3D_TitleCsBlendedLight(uint16_t daytime,
                                uint8_t amb[3], int8_t l1dir[3], uint8_t l1col[3],
                                int8_t l2dir[3], uint8_t l2col[3], uint8_t fogCol[3]);

// Title palette FOG params blended at a dayTime (same schedule/weights as BlendedLight).
// Units RE'd 2026-07-10 (oot3d-decomp title_env_lighting.md §13, live-verified against the
// oracle's FogResUpdater inputs at three dayTimes): fogNear = the per-slot u16&0x3ff field,
// ALREADY in eye units (night 40 / sunrise 200 / day 800 / sunset 200; blended 48/92/138 at
// dayTime 0x2bbb/0x3197/0x37b5 == the oracle's live LUT-object near); fogFar = the blended
// per-slot drawDist f32 (blended 40414/42612/44906 at the same frames == the oracle's live
// far); fogEnd = the per-slot fogEnd f32 (32000 all slots) = the 3DS camera far plane the fog
// LUT's depth mapping is built against.
int Zelda3D_TitleCsBlendedFog(uint16_t daytime, float* fogNear, float* fogFar, float* fogEnd);

// 3DS sky-DOME schedule (config 0, "fine"/kind=1): daytime -> skybox1Index /
// skybox2Index + blend weight (0..1). This is `FUN_002e47c8`'s OWN table at
// VA 0x0053200a (oot3d-decomp/docs/title_sky_dome.md §9.2) — a SEPARATE,
// purpose-built table from kTitleLightSchedule (VA 0x00531efc) even though
// its 9 rows are byte-identical in boundary/idx values (the two tables are
// contiguous in the ROM's data blob). Kept as its own table/function per
// title_sky_dome.md §9.5's explicit instruction not to silently alias the
// light schedule's index field for the dome consumer.
int Zelda3D_TitleCsDomeBlend(uint16_t daytime, int* skybox1Index,
                             int* skybox2Index, float* blendWeight);

// Misc / transition / loop triggers — op-0x03 (misc) sub-op 0x1e = logo
// FADE_IN trigger, sub-op 0x1f = logo FADE_OUT trigger; op-0x7c =
// screen-level fade window; op-0x3e8 = loop-restart frame. All byte-confirmed
// in spot99's " BDQ" stream (see `oot3d-decomp/docs/title_gamestate_driver.md`
// §3). Returns -1 / 0 when the cs lacks the requested trigger.
int Zelda3D_TitleCsMiscTriggerFrame(uint16_t sub);
int Zelda3D_TitleCsScreenFade(int* start, int* end);
int Zelda3D_TitleCsLoopFrame(void);

int  Zelda3D_TitleCsFrame(void);
// Sub-frame fraction (0.0 or 0.5) of the current engine tick within the cs frame — 60fps interp.
float Zelda3D_TitleCsSubframe(void);
void Zelda3D_TitleCsSetFrame(int frame);
int  Zelda3D_TitleCsAdvance(void);   // returns new frame

// Whether the most recent Zelda3D_TitleCsAdvance() call actually incremented sFrame (including
// the loop wrap 2399->0) vs. held it (sTickParity's off-tick). Consumers with STATEFUL per-tick
// integrators (e.g. the title rider's PathFollow, which advances position every call it's given)
// must gate their own step on this — the cursor now advances at half the engine's tick rate
// (sTickParity), so a consumer stepping unconditionally every engine tick runs at 2x the
// authored cue speed. Pure functions of Zelda3D_TitleCsFrame() (camera, dayTime, dome) don't
// need this: they re-evaluate from the frame value itself, which is unchanged on a hold tick.
int  Zelda3D_TitleCsDidAdvance(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CUTSCENE_H
