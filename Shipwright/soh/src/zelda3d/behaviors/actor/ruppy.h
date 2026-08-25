// Zelda3D behavior: En_Ex_Ruppy (diving-game / thrown colored rupee) — model REPLACEMENT with a
// per-color mesh selection. OoT3D draws the rupee from the get-item rupee CMB
// (zelda_gi_rupy.zar Model/zelda_gi_rupy.cmb), which packs all five rupee colors as five distinct
// mesh_ids (0=green .. 4=purple); we select the one matching the actor's live colorIdx via the
// mesh_id visibility mask. See ruppy.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_RUPPY_H
#define ZELDA3D_BEHAVIORS_ACTOR_RUPPY_H

#include "../actor_behavior.h"

namespace Zelda3D {

class EnExRuppyBehavior : public ActorBehavior {
  public:
    s16 actorId() const override;
    // Draws only the colorIdx-matching mesh of the OoT3D rupee CMB at the actor's world transform,
    // suppressing the N64 rupee. Honors the actor's `invisible` flag.
    bool tryDrawModel(PlayState* play, Actor* actor) override;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_RUPPY_H
