// Zelda3D actor-behavior framework — the structured home for ported OoT3D actor behaviors.
//
// Zelda3D renders OoT3D models in place of the N64 ones, but the OoT3D engine layers procedural,
// per-draw effects onto a skeletal actor that the raw CSAB pose does not carry: head/torso TRACKING
// (turn toward the player), facial material animation (blink/mouth), held-item DL / mesh swaps. In the
// original engine these live in each actor's OverrideLimbDraw/PostLimbDraw. We PORT them — one module
// per actor, structured like a real game (behaviors/actor/<actor>.cpp), dispatched by actor->id.
//
// Each behavior owns its actor's per-draw overrides. The registry (findActorBehavior) replaces the
// legacy zar-keyed tables in zelda3d_anim_override.cpp; migrate actors into modules incrementally and
// fall through to the legacy path for any not yet ported.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BEHAVIOR_H
#define ZELDA3D_BEHAVIORS_ACTOR_BEHAVIOR_H

#include "z64.h" // Actor, PlayState, Vec3f, s16

namespace Zelda3D {

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
    virtual void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {
    }

    // Advance state that must observe the actor at the start of its gameplay update. A pre-update
    // producer can resample a 20 Hz host actor onto OoT3D's 30 Hz cadence without using the already-
    // moved host endpoint to synthesize an earlier authored sample.
    virtual void preUpdate(PlayState* play, Actor* actor) {
    }

    // Advance actor-owned 3DS state after the underlying gameplay actor has updated. Both update
    // seams are independent of visibility and draw cadence.
    virtual void postUpdate(PlayState* play, Actor* actor) {
    }

    // Model-REPLACEMENT path: fully DRAW this actor's OoT3D replacement model (choosing the CMB,
    // scale and transform itself) and return true to SUPPRESS the N64 draw. Default false = this
    // behavior does not replace the model (it only layers overrides via applyDrawOverrides above).
    // Used for actors whose OoT3D asset is a distinct CMB chosen here — e.g. En_Door, which OoT3D
    // draws from the KEEP zar rather than as a standalone actor object. Called from
    // Zelda3D_TryDrawActor (via the Zelda3D_TryActorModelDraw C bridge) before the auto/table path.
    virtual bool tryDrawModel(PlayState* play, Actor* actor) {
        return false;
    }

    // Deferred skeletal replacement: select an OoT3D model, then let the actor's own Draw reach the
    // SkelAnime choke point so the live N64 animation/playhead can drive the authored CSAB. Return
    // true after preparing the global pending draw; the dispatcher still returns false to Actor_Draw
    // so the actor draw runs. This is distinct from tryDrawModel(), which draws immediately and
    // suppresses the actor draw entirely. Boss_Fd2 needs this because its body is skeletal while its
    // three fire-hair chains are a second multipart submission later in the same Draw.
    virtual bool prepareDeferredDraw(PlayState* play, Actor* actor) {
        return false;
    }

    // Faithful draw-space transform offset for an actor whose own Draw applies translate(s) the
    // generic world.pos anchor path omits. OoT3D Actor_Draw lifts the model matrix by
    // shape.yOffset*scale.y in WORLD Y; some actors' Draw then post-multiplies a LOCAL
    // Matrix_Translate (in the rotated, actor-scale frame) — e.g. BossGoma_Draw's
    // Matrix_Translate(0,-4000,0). When the actor rotates (Gohma tilting onto a climbing pillar) the
    // world lift and the local translate stop cancelling, so the model is displaced off its world.pos
    // anchor; the generic anchor path (a vertical groundOffset) cannot reproduce this. Fill
    // *worldLiftY (added to world.pos.y, world frame, like shape.yOffset*scale.y) and *localOffset
    // (applied AFTER shape.rot but BEFORE the model's worldScale, i.e. in the rotated WORLD-UNIT frame,
    // like the actor Draw's Matrix_Translate scaled by actor->scale). Return true to REPLACE the
    // generic groundOffset with this faithful placement (false = keep the generic anchor). Read
    // scale/yOffset from the live actor C struct (64-bit-safe) — never raw N64 byte offsets.
    virtual bool drawSpaceTransform(Actor* actor, float* worldLiftY, Vec3f* localOffset) {
        return false;
    }
};

// Look up the behavior that owns `actorId`, or nullptr if none is registered (caller falls through to
// the legacy zar-table path). Defined in actor_behavior.cpp (the explicit registry).
ActorBehavior* findActorBehavior(s16 actorId);

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_BEHAVIOR_H
