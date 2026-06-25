// SoH3D behavior: En_Md Mido — head/torso track + eye material-anim. See mido.cpp.
#ifndef SOH3D_BEHAVIORS_ACTOR_MIDO_H
#define SOH3D_BEHAVIORS_ACTOR_MIDO_H

#include "../actor_behavior.h"

namespace SoH3D {

class MidoBehavior : public ActorBehavior {
public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace SoH3D

#endif // SOH3D_BEHAVIORS_ACTOR_MIDO_H
