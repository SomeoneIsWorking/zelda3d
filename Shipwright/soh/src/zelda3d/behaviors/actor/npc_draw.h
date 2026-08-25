// Shared OoT3D NPC procedural draw channels: head/torso tracking and facial material animation.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_NPC_DRAW_H
#define ZELDA3D_BEHAVIORS_ACTOR_NPC_DRAW_H

#include "z64.h"

namespace Zelda3D {

// OoT3D's shared head/torso rotate helper (decomp FUN_0034e01c): post-multiply
// RotateX(rot.y) * RotateZ(rot.x) onto one OoT3D bone's local transform. There is no Y negation or
// pivot translation (oot3d-decomp/docs/enko_override_and_ensa_facial.md). Inputs are binang values
// read through the live C struct, never raw N64 byte offsets.
void applyTrackRot(int modelId, int bone, const Vec3s& rot);

// Apply the shared rotation to the head and torso values produced and clamped by Npc_TrackPoint.
void applyHeadTorsoTrack(int modelId, int headBone, int torsoBone, const NpcInteractInfo& interactInfo);

// Bind one facial material to the actor's live material-animation frame, honoring the REPL
// faceframe override. An out-of-range frame clears the override and restores the base sprite.
void applyFacialFrame(int modelId, int material, int liveIndex);

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_NPC_DRAW_H
