#include "render_lifecycle.h"

#include "actor_motion_observation.h"
#include "terrain_alignment_render.h"

void Zelda3D_RenderRefsResetRunState(void) {
    Zelda3D_ActorMotionObservationResetRunState();
    Zelda3D_TerrainAlignmentResetRunState();
}
