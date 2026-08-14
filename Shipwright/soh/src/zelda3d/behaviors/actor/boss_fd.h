// Zelda3D behavior: Boss_Fd (Volvagia's flying multipart form).
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H

#include "../actor_behavior.h"

namespace Zelda3D {

class BossFdBehavior : public ActorBehavior {
public:
    s16 actorId() const override;
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#ifdef __cplusplus
extern "C" {
#endif
int Zelda3D_BossFdForceFly(Actor* actor);
int Zelda3D_BossFdForceDeath(Actor* actor, int liveSegments, int actionState);
int Zelda3D_BossFdHistoryInfo(Actor* actor, int* bodyLead, int* maneLead, Vec3f* minPos,
                             Vec3f* maxPos);
#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_H
