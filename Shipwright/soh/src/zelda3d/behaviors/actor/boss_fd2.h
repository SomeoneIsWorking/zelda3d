// Zelda3D behavior: Boss_Fd2 (Volvagia's hole form).
//
// OoT3D draws this actor as the valbasiagnd skeletal CMB plus three independently simulated
// valbasia_firehair chains. The body is deferred through the SkelAnime choke point; each chain
// segment is replaced at the transform computed by the faithful actor simulation.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_H

#include "../actor_behavior.h"

namespace Zelda3D {

class BossFd2Behavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
    bool prepareDeferredDraw(PlayState* play, Actor* actor) override;
    bool drawSpaceTransform(Actor* actor, float* worldLiftY, Vec3f* localOffset) override;
};

} // namespace Zelda3D

#ifdef __cplusplus
extern "C" {
#endif

// Return the rendered OoT3D body model's posed limb-14 origin in engine world space. This is the
// typed counterpart of OoT3D FUN_001EC5B8 writing the live limb matrix origin to actor+0x328.
int Zelda3D_BossFd2RenderedHeadWorldPos(Actor* actor, float out[3]);

// Called from the focused mane owner after the shared chain simulation has produced the exact world
// transform. Returns 1 when the OoT3D fire-hair CMB was submitted, 0 to retain the N64 segment.
int Zelda3D_BossFd2DrawManeSegment(PlayState* play, Actor* actor, int chain, int segment, const Vec3f* pos,
                                   const Vec3f* rot, const Vec3f* scale);

// Diagnostic control: drive the selected waiting helper through the real parent->child handoff
// consumed by BossFd2_Wait. Returns 1 only for a live Boss_Fd2 with its Boss_Fd parent attached.
int Zelda3D_BossFd2ForceGround(Actor* actor);

// Diagnostic control: force or release the real idle state on the selected hole-form actor.
int Zelda3D_BossFd2ForceIdle(PlayState* play, Actor* actor, int hold);

// Diagnostic control: enter a real collision-result setup state on a selected Boss_Fd2.
// state: 0=knockout/vulnerable, 1=nonlethal damage, 2=death.
int Zelda3D_BossFd2ForceDamageState(PlayState* play, Actor* actor, int state);

// Resolve the OoT3D-authored CSAB and its independent OoT3D playhead for the hole-form actor.
// Returns 1 for Boss_Fd2; the generic N64 phase-lock path must not run when this succeeds.
int Zelda3D_BossFd2ResolveAnim(PlayState* play, Actor* actor, const char** outCsab, float* outFrame,
                               const char** outMorphCsab, float* outMorphFrame, float* outMorphWeight);

// Opaque diagnostic checkpoint for paired solver controls. Capture copies the complete actor-local
// controller; restore rewinds it so the next ordinary post-update reproduces the same next pose.
// Neither function changes controller behavior unless explicitly called.
int Zelda3D_BossFd2CaptureAnimController(Actor* actor);
int Zelda3D_BossFd2RestoreAnimController(Actor* actor);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_H
