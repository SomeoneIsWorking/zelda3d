// Zelda3D behavior: Boss_Goma (Queen Gohma). She renders her OoT3D model (zelda_goma.zar) via the
// generic auto-CSAB SKINNED path (driven by her live N64 jointTable), so her draw is NOT replaced
// here; this module supplies the faithful DRAW-SPACE transform offset that the generic world.pos
// anchor path drops.
//
// OoT3D BossGoma_Draw (decomp FUN_00221d70, oot3d-decomp/docs/boss_goma.md) does
// Matrix_Translate(0, -4000, 0) (MTXMODE_APPLY, in the rotated actor-scale frame) on top of
// Actor_Draw's world-Y lift by shape.yOffset*scale.y (= 4000*scale.y). Upright these cancel and the
// model sits at world.pos; once she tilts onto a climbing pillar (shape.rot.x → -0x4000) the local
// translate becomes a horizontal world displacement that no vertical groundOffset can match, so the
// Zelda3D model floats off the pillar (kanban #123). drawSpaceTransform replicates both offsets so the
// model lands exactly where OoT3D draws it on any surface. See boss_goma.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_GOMA_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_GOMA_H

#include "../actor_behavior.h"

namespace Zelda3D {

class BossGomaBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Faithful BossGoma_Draw placement: world lift = shape.yOffset*scale.y, local translate =
    // (0, -4000*scale.y, 0) applied after shape.rot, in the rotated world-unit frame.
    bool drawSpaceTransform(Actor* actor, float* worldLiftY, Vec3f* localOffset) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_GOMA_H
