// SoH3D skeletal-actor draw-override framework (the OoT3D OverrideLimbDraw port).
//
// OoT3D draws skeletal actors through a shared framework that, per limb, layers procedural effects
// the base CSAB pose does not carry: head/torso TRACKING (turn toward the player), facial material
// animation (eye-blink / mouth), and per-limb held-item DL swaps. In the original engine these live
// in each actor's OverrideLimbDraw/PostLimbDraw callbacks, applied to the limb MATRIX (MTXMODE_APPLY)
// or as gSPSegment/DL swaps. SoH3D's CSAB auto-draw path samples a raw (anim, frame) pose and drops
// all of it — so Kokiri/Saria stare straight ahead, never blink, and miss the ocarina DL.
//
// This module PORTS those overrides onto the OoT3D rig directly (not by retargeting N64 limb math):
// it reads the live interactInfo the faithful actor logic already computes each frame and applies the
// OoT3D-native rotation to the OoT3D head/torso BONE via the CSAB skinner's post-rotation channel
// (SoH3D_SetBonePostRot — a matrix post-multiply, matching the actor's MTXMODE_APPLY, propagating to
// child bones). Faithful by construction; no N64<->OoT3D axis derivation.
#ifndef SOH3D_ANIM_OVERRIDE_H
#define SOH3D_ANIM_OVERRIDE_H

#ifdef __cplusplus
extern "C" {
#endif

// Apply the draw-overrides for the auto-replaced skinned actor currently being drawn onto its OoT3D
// model. `actor` is the live Actor* (passed as void* to keep this header dependency-free). Clears any
// stale per-bone overrides first, so it is safe to call for every auto actor (no-op for actors with
// no override row). Currently: head/torso tracking. (Facial/material overrides will extend this.)
void SoH3D_ApplyActorOverrides(int modelId, void* actor);

// Feature gate (REPL `track`, env SOH3D_TRACK, default on) — lets the override be A/B'd live.
extern int gSoH3dTrack;

// Facial eye/mouth material-anim gate (REPL `facial`, env SOH3D_FACIAL, default on). Keystone #3.
extern int gSoH3dFacial;

#ifdef __cplusplus
}
#endif

#endif // SOH3D_ANIM_OVERRIDE_H
