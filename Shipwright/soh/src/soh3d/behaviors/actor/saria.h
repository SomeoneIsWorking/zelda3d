// SoH3D behavior: En_Sa Saria — head/torso track + eye/mouth material-anim, ported from OoT3D
// EnSa_OverrideLimbDraw (decomp @ 0x23bca4) + EnSa_Draw. See saria.cpp.
#ifndef SOH3D_BEHAVIORS_ACTOR_SARIA_H
#define SOH3D_BEHAVIORS_ACTOR_SARIA_H

#include "../actor_behavior.h"

namespace SoH3D {

class SariaBehavior : public ActorBehavior {
public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace SoH3D

#endif // SOH3D_BEHAVIORS_ACTOR_SARIA_H
