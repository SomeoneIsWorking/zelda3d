// Zelda3D behavior: En_Ko Kokiri kids (boy km1 / girl kw1 / Fado). Ported from OoT3D
// EnKo_OverrideLimbDraw (decomp @ 0x2335b4) + EnKo_Draw eye material-anim. See kokiri_kid.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_KOKIRI_KID_H
#define ZELDA3D_BEHAVIORS_ACTOR_KOKIRI_KID_H

#include "../actor_behavior.h"

namespace Zelda3D {

class KokiriKidBehavior : public ActorBehavior {
public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_KOKIRI_KID_H
