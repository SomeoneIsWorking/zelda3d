// Zelda3D behavior: En_Hy Hylian townsfolk — head/torso track + eye material-anim. The body skeleton
// (and thus head/torso bones + eye material) varies per EnHyType / body archetype; resolved by the
// OoT3D body zar. See townsfolk.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_TOWNSFOLK_H
#define ZELDA3D_BEHAVIORS_ACTOR_TOWNSFOLK_H

#include "../actor_behavior.h"

namespace Zelda3D {

class TownsfolkBehavior : public ActorBehavior {
public:
    s16 actorId() const override;
    void applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_TOWNSFOLK_H
