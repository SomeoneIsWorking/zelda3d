// SoH3D skeletal-actor draw-override entry point — see soh3d_anim_override.h.
//
// OoT3D layers procedural per-draw effects onto skeletal actors that the raw CSAB pose drops:
// head/torso TRACKING, facial eye/mouth material-anim, held-item DL/mesh swaps. These are now ported
// per-actor in structured modules under behaviors/actor/<actor>.cpp (a base ActorBehavior + a registry
// keyed by actor->id). This file is the thin C-ABI entry called from the auto-replace draw path: it
// owns the live feature gates (REPL track/facial/faceframe), clears stale per-bone/material overrides
// each draw, and dispatches to the actor's behavior module.
#include "soh3d_anim_override.h"
#include "z64.h"                      // Actor
#include "behaviors/actor_behavior.h" // per-actor behavior modules (registry by actor->id)

#include <cstdlib>

extern "C" {
void SoH3D_ClearBonePostRots(int modelId);
void SoH3D_GL_ClearMatTexOverrides(int modelId);
}

int gSoH3dTrack = -1;  // -1 = uninit (env SOH3D_TRACK, default on)
int gSoH3dFacial = -1; // -1 = uninit (env SOH3D_FACIAL, default on)
// Debug/verification override (REPL `faceframe <n>`): when >= 0, force every facial actor's eye/mouth
// to this frame index, bypassing the live N64 index. Lets the channel be verified deterministically
// despite the headless actor-update throttle stalling natural blink. -1 = off (use the live index).
// Honored by SoH3D::applyFacialFrame (behaviors/actor_behavior.cpp).
int gSoH3dFaceForce = -1;

extern "C" void SoH3D_ApplyActorOverrides(int modelId, void* actorv) {
    if (gSoH3dTrack < 0) {
        const char* v = std::getenv("SOH3D_TRACK");
        gSoH3dTrack = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    if (gSoH3dFacial < 0) {
        const char* v = std::getenv("SOH3D_FACIAL");
        gSoH3dFacial = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    // Start from a clean slate each draw (no stale track pose / facial frame).
    SoH3D_ClearBonePostRots(modelId);
    SoH3D_GL_ClearMatTexOverrides(modelId);
    if (actorv == nullptr) {
        return;
    }

    // Dispatch to the structured per-actor behavior module (behaviors/actor/<actor>.cpp). A registered
    // behavior fully owns its actor's draw-overrides, reading state via the C struct (correct in the
    // 64-bit build). Unregistered actors get only the clear above (no override).
    Actor* actor = static_cast<Actor*>(actorv);
    if (SoH3D::ActorBehavior* behavior = SoH3D::findActorBehavior(actor->id)) {
        behavior->applyDrawOverrides(modelId, actor, gSoH3dTrack != 0, gSoH3dFacial != 0);
    }
}
