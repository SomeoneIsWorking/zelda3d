// SoH3D behavior: Boss_Goma (Queen Gohma) — faithful draw-space transform offset. See boss_goma.h.
#include "z64.h"
#include "boss_goma.h"

namespace SoH3D {

s16 BossGomaBehavior::actorId() const {
    return ACTOR_BOSS_GOMA;
}

// OoT3D ground truth (BossGoma_Draw / Actor_Draw, decomp 00221d70 + N64 z_boss_goma.c:2168):
//   render origin = world.pos + (0, shape.yOffset*scale.y, 0)        [Actor_Draw, WORLD frame]
//   then, after shape.rot, Matrix_Translate(0, -4000, 0) in the actor-scale frame
//     = a local translate of (0, -4000*scale.y, 0)                   [BossGoma_Draw, ROTATED frame]
// shape.yOffset is 4000 (ActorShape_Init), so upright the two cancel and the model sits at world.pos;
// rotated (climbing) they don't, displacing the model — which OoT3D WANTS (the model hugs the pillar
// while her collider/world.pos sits off it). The generic SoH3D anchor draws the model at world.pos,
// so it floats off the pillar. We read shape.yOffset/scale.y live from the C struct (64-bit-safe) and
// keep the -4000 as the decomp-confirmed N64 constant (not a fitted magic offset).
bool BossGomaBehavior::drawSpaceTransform(Actor* actor, float* worldLiftY, Vec3f* localOffset) {
    const float sy = actor->scale.y;
    *worldLiftY = actor->shape.yOffset * sy;       // Actor_Draw world-Y lift
    localOffset->x = 0.0f;
    localOffset->y = -4000.0f * sy;                // BossGoma_Draw Matrix_Translate(0,-4000,0), scaled
    localOffset->z = 0.0f;
    return true;
}

} // namespace SoH3D
