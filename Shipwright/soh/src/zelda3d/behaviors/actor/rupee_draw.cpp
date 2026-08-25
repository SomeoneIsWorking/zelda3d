// Shared OoT3D colored-rupee model drawing. See rupee_draw.h.
#include "rupee_draw.h"
#include "fast/zelda3d_material_overrides.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"

namespace Zelda3D {

bool drawRupeeColorMesh(PlayState* play, Actor* actor, int colorIndex, float worldScale) {
    static int sModelId = 0;
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId("/actor/zelda_gi_rupy.zar|Model/zelda_gi_rupy.cmb");
    }
    if (sModelId < 0) {
        return false;
    }
    if (colorIndex < 0 || colorIndex > 4) {
        colorIndex = 0;
    }
    Zelda3D_GL_SetMidMask(sModelId, 1ULL << colorIndex);
    Zelda3D_DrawActorModel(play, sModelId, actor, worldScale);
    return true;
}

} // namespace Zelda3D
