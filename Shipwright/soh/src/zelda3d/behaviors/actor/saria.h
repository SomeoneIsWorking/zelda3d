// Zelda3D behavior: En_Sa Saria — head/torso track + eye/mouth material-anim, ported from OoT3D
// EnSa_OverrideLimbDraw (decomp @ 0x23bca4) + EnSa_Draw. See saria.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_SARIA_H
#define ZELDA3D_BEHAVIORS_ACTOR_SARIA_H

#include "../actor_behavior.h"

namespace Zelda3D {

class SariaBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_SARIA_H
