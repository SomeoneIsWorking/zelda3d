// Shared OoT3D colored-rupee model drawing.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_RUPEE_DRAW_H
#define ZELDA3D_BEHAVIORS_ACTOR_RUPEE_DRAW_H

#include "z64.h"

namespace Zelda3D {

// Draw zelda_gi_rupy.cmb at worldScale with only colorIndex's mesh visible. The model bakes its five
// colors into mesh ids 0..4 (green, blue, red, gold, purple), so the visibility mask is the color
// channel shared by En_Ex_Ruppy and En_Item00. Returns false when the model is unavailable so the
// actor can retain its N64 draw path. Invalid colors select green.
bool drawRupeeColorMesh(PlayState* play, Actor* actor, int colorIndex, float worldScale);

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_RUPEE_DRAW_H
