// Zelda3D skeletal-actor draw-override entry point — see zelda3d_anim_override.h.
//
// OoT3D layers procedural per-draw effects onto skeletal actors that the raw CSAB pose drops:
// head/torso TRACKING, facial eye/mouth material-anim, held-item DL/mesh swaps. These are now ported
// per-actor in structured modules under behaviors/actor/<actor>.cpp (a base ActorBehavior + a registry
// keyed by actor->id). This file is the thin C-ABI entry called from the auto-replace draw path: it
// owns the live feature gates (REPL track/facial/faceframe), clears stale per-bone/material overrides
// each draw, and dispatches to the actor's behavior module.
#include "zelda3d_anim_override.h"
#include "z64.h"                         // Actor
#include "../behaviors/actor_behavior.h" // per-actor behavior modules (registry by actor->id)
#include "fast/zelda3d_material_overrides.h"

#include <cstdlib>

extern "C" {
void Zelda3D_ClearBonePostRots(int modelId);
}

int gZelda3dTrack = -1;  // -1 = uninit (env ZELDA3D_TRACK, default on)
int gZelda3dFacial = -1; // -1 = uninit (env ZELDA3D_FACIAL, default on)
// Debug/verification override (REPL `faceframe <n>`): when >= 0, force every facial actor's eye/mouth
// to this frame index, bypassing the live N64 index. Lets the channel be verified deterministically
// despite the headless actor-update throttle stalling natural blink. -1 = off (use the live index).
// Honored by Zelda3D::applyFacialFrame (behaviors/actor/npc_draw.cpp).
int gZelda3dFaceForce = -1;

extern "C" void Zelda3D_ApplyActorOverrides(int modelId, void* actorv) {
    if (gZelda3dTrack < 0) {
        const char* v = std::getenv("ZELDA3D_TRACK");
        gZelda3dTrack = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    if (gZelda3dFacial < 0) {
        const char* v = std::getenv("ZELDA3D_FACIAL");
        gZelda3dFacial = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    // Start from a clean slate each draw (no stale track pose / facial frame / body-color override).
    Zelda3D_ClearBonePostRots(modelId);
    Zelda3D_GL_ClearMatTexOverrides(modelId);
    Zelda3D_GL_ClearMatConstOverrides(modelId);
    if (actorv == nullptr) {
        return;
    }

    // Dispatch to the structured per-actor behavior module (behaviors/actor/<actor>.cpp). A registered
    // behavior fully owns its actor's draw-overrides, reading state via the C struct (correct in the
    // 64-bit build). Unregistered actors get only the clear above (no override).
    Actor* actor = static_cast<Actor*>(actorv);
    if (Zelda3D::ActorBehavior* behavior = Zelda3D::findActorBehavior(actor->id)) {
        behavior->applyDrawOverrides(modelId, actor, gZelda3dTrack != 0, gZelda3dFacial != 0);
    }
}
