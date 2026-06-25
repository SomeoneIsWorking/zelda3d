// SoH3D actor-behavior framework — the structured home for ported OoT3D actor behaviors.
//
// SoH3D renders OoT3D models in place of the N64 ones, but the OoT3D engine layers procedural,
// per-draw effects onto a skeletal actor that the raw CSAB pose does not carry: head/torso TRACKING
// (turn toward the player), facial material animation (blink/mouth), held-item DL / mesh swaps. In the
// original engine these live in each actor's OverrideLimbDraw/PostLimbDraw. We PORT them — one module
// per actor, structured like a real game (behaviors/actor/<actor>.cpp), dispatched by actor->id.
//
// Each behavior owns its actor's per-draw overrides. The registry (findActorBehavior) replaces the
// legacy zar-keyed tables in soh3d_anim_override.cpp; migrate actors into modules incrementally and
// fall through to the legacy path for any not yet ported.
#ifndef SOH3D_BEHAVIORS_ACTOR_BEHAVIOR_H
#define SOH3D_BEHAVIORS_ACTOR_BEHAVIOR_H

#include "z64.h" // Actor, Vec3s, s16 (include after global.h is available)

namespace SoH3D {

// Base class for a ported OoT3D actor behavior. The registry dispatches by actor->id; a registered
// behavior FULLY owns that actor's draw-overrides for the frame.
class ActorBehavior {
public:
    virtual ~ActorBehavior() = default;

    // The N64 actor id this behavior owns (ACTOR_EN_KO, ...).
    virtual s16 actorId() const = 0;

    // Apply this actor's per-draw overrides onto its OoT3D model. The caller has already cleared any
    // stale bone post-rotations and material-texture overrides for `modelId`, so the behavior only
    // SETS what it needs. `track`/`facial` are the live A/B feature gates (REPL `track` / `facial`).
    // `actor` is the live, faithfully-updated N64 actor — read its state through the C struct.
    // Default no-op: a model-REPLACEMENT behavior (tryDrawModel) need not layer per-draw overrides.
    virtual void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {}

    // Model-REPLACEMENT path: fully DRAW this actor's OoT3D replacement model (choosing the CMB,
    // scale and transform itself) and return true to SUPPRESS the N64 draw. Default false = this
    // behavior does not replace the model (it only layers overrides via applyDrawOverrides above).
    // Used for actors whose OoT3D asset is a distinct CMB chosen here — e.g. En_Door, which OoT3D
    // draws from the KEEP zar rather than as a standalone actor object. Called from
    // SoH3D_TryDrawActor (via the SoH3D_TryActorModelDraw C bridge) before the auto/table path.
    virtual bool tryDrawModel(PlayState* play, Actor* actor) { return false; }
};

// Look up the behavior that owns `actorId`, or nullptr if none is registered (caller falls through to
// the legacy zar-table path). Defined in actor_behavior.cpp (the explicit registry).
ActorBehavior* findActorBehavior(s16 actorId);

// --- Shared port utilities (reused across NPC behaviors) -----------------------------------------

// OoT3D's shared head/torso rotate helper (decomp FUN_0034e01c): post-multiply
//   RotateX(rot.y) · RotateZ(rot.x)
// onto the OoT3D head/torso BONE's local transform — the MTXMODE_APPLY equivalent, propagating to
// child bones. NO Y negation and NO pivot translate (decomp-confirmed:
// oot3d-decomp/docs/enko_override_and_ensa_facial.md). Units: binang (s16) × 2π/65536. The values
// come from the running actor's interactInfo, which Npc_TrackPoint clamps each Update (≤ ~60°), so no
// plausibility guard is needed when the value is read from the correct (C-struct) field.
void applyTrackRot(int modelId, int bone, const Vec3s& rot);

// Apply both head and torso track from a live NpcInteractInfo (the common NPC case): post-multiply the
// shared rotate helper onto the head bone (from headRot) and the torso bone (from torsoRot). Read `ii`
// through the actor's C struct, never a raw N64 byte offset (64-bit build — see kokiri_kid.cpp).
void applyHeadTorsoTrack(int modelId, int headBone, int torsoBone, const NpcInteractInfo& ii);

// Bind material `material`'s eye/mouth frame texture to `liveIdx` (the index the running N64 logic
// computed), honoring the REPL `faceframe` verification override. An out-of-range index clears the
// override (falls back to the base sprite). This is the OoT3D facial mechanism: a material-anim frame
// swap, not an N64 segment swap.
void applyFacialFrame(int modelId, int material, int liveIdx);

} // namespace SoH3D

#endif // SOH3D_BEHAVIORS_ACTOR_BEHAVIOR_H
