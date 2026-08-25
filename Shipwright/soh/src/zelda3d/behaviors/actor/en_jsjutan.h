// Zelda3D behavior: En_Jsjutan (Haunted Wasteland flying carpet) — model REPLACEMENT. OoT3D draws
// the same carpet from zelda_js.zar Model/carpetmaster.cmb. See en_jsjutan.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_JSJUTAN_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_JSJUTAN_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnJsjutanBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_JSJUTAN_H
