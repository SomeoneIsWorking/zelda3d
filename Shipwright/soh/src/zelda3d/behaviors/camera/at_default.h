// OoT3D Camera_CalcAtDefault Y-bias producer and consumer seam.
#ifndef ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_H
#define ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Advance the OoT3D-only Y-bias producer. Returns nonzero when Zelda3D owns Player::unk_6C4 for
// this update; zero leaves the stock slope path in control. The action flags must be typed
// `Player_Action_80842180` (walk/run) and `Player_Action_8084E6D4` (get-item) comparisons from
// z_player.c; the latter is OoT3D's explicit exception on slope floor types 4, 7, and 12.
int Zelda3D_CameraAtDefaultUpdatePlayer(PlayState* play, Player* player, s32 floorType, s32 isWalkRunAction,
                                        s32 isGetItemAction);

// Return the extra Camera_CalcAtDefault at.y term. This is non-inserting: a player with no active
// producer state reads as zero.
f32 Zelda3D_CameraAtDefaultYBias(const Player* player);

// One update's worth of producer state, as the recovered producer saw it. This is the observation
// surface for the threshold-timing discriminator: every field here is an input or a decision the
// producer already made, so a measurement never has to re-derive the rule from engine state.
typedef struct Zelda3D_CameraAtDefaultSample {
    s32 active;          // the recovered active latch (OoT3D `player + 0x29B8 & 0x100`)
    f32 accumulator;     // Player::unk_6C4, in accumulator units (100 per world unit of rise)
    s32 branchOwned;     // 1 when the extra-Y branch, not the stock slope path, owns unk_6C4
    f32 rise;            // world.pos.y - prevPos.y as this update measured it
    s32 walkRunAction;   // the typed Player_Action_80842180 comparison the producer received
    s32 getItemAction;   // the typed Player_Action_8084E6D4 comparison the producer received
    s32 floorType;       // the sFloorType value the producer received
    s32 staticFloor;     // 1 when DynaPoly_GetActor(&colCtx, floorBgId) == NULL
    s32 authoredUpdates; // authored (30 Hz) updates this 20 Hz host update advanced
    f32 yBias;           // the extraAtY term Camera_CalcAtDefault adds to atTarget.y
} Zelda3D_CameraAtDefaultSample;

// Read one producer sample. Returns 0 (and leaves `sample` zeroed) when this player has never been
// advanced by the producer, so a caller can tell "no state" from "state that happens to be zero".
int Zelda3D_CameraAtDefaultReadSample(const Player* player, Zelda3D_CameraAtDefaultSample* sample);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_CAMERA_AT_DEFAULT_H
