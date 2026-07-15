// Zelda3D actor draw-override glue that doesn't (yet) fit the per-actor ActorBehavior split.
//
// These are small, self-contained overrides ported out of core/zelda3d.c during the phase-3
// codebase reorg (2026-07): CSAB-name resolution for En_Ge1/En_Ko/En_Hy, and the cucco (En_Niw)
// procedural wing-flap replay. Each is called from a handful of fixed sites in
// core/zelda3d.c / render/zelda3d_render.cpp rather than dispatched through the ActorBehavior
// registry, so they are grouped here as one module instead of forced into separate
// en_ge1.cpp/en_ko.cpp/en_hy.cpp/en_niw.cpp files (see actor_behavior.h for the registry pattern
// used by fully-migrated actors).
//
// core/zelda3d.c is C, so every symbol here needs C linkage.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_ACTOR_OVERRIDES_H
#define ZELDA3D_BEHAVIORS_ACTOR_ACTOR_OVERRIDES_H

#include "z64.h" // Actor, PlayState, Vec3s, s16, s32

#ifdef __cplusplus
extern "C" {
#endif

// #87: per-ENKO_TYPE CSAB override for Kokiri kids (En_Ko), grounded in OoT3D oracle ground truth.
// Returns a CSAB base to force, or NULL to leave the normal N64-anim mapping in place. See the
// definition in actor_overrides.cpp for the full oracle-derivation notes per type.
const char* Zelda3D_EnKoCsabOverride(int modelId, Actor* actor);

// #73: per-ENHY_TYPE idle CSAB override for adult townsfolk (En_Hy), same oracle-grounded pattern
// as Zelda3D_EnKoCsabOverride.
const char* Zelda3D_EnHyCsabOverride(int modelId, Actor* actor);

// En_Ge1 (white Gerudo) N64-animation -> OoT3D CSAB resolver, used as the sModelTable resolver
// function pointer in render/zelda3d_render.cpp.
const char* Zelda3D_ResolveAnim_EnGe1(Actor* actor);

// En_Ge1 joints for the N64-animation port (see Zelda3D_ResolveAnim_EnGe1); used as the
// sModelTable joints function pointer in render/zelda3d_render.cpp.
int Zelda3D_Joints_EnGe1(Actor* actor, const s16** outJointRots, int* outLimbCount);

// Capture the procedural OverrideLimbDraw callback an N64 actor passed to SkelAnime_Draw* this
// frame (cucco/En_Niw wing-flap and similar limb-local additive rotations), for
// Zelda3D_ApplyProcOverride to probe. Declared here (definition also here); the public umbrella
// zelda3d.h re-declares it too since z_skelanime.c calls it directly. `kind`: 0 = 6-arg
// OverrideLimbDrawOpa, 1 = 7-arg OverrideLimbDraw.
void Zelda3D_SetLimbOverride(void* overrideFn, void* arg, int kind);

// Probe the captured override callback (see Zelda3D_SetLimbOverride) for each mapped limb of the
// current auto actor and push the resulting per-bone local-rotation delta onto the OoT3D model.
// No-op when no override was captured or this ZAR has no procedural-override rows. Called once per
// auto-replaced actor draw from core/zelda3d.c's retarget path.
void Zelda3D_ApplyProcOverride(PlayState* play, int modelId, Vec3s* jointTable, int limbCount);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_ACTOR_OVERRIDES_H
