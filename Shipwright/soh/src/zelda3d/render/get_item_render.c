#include "get_item_render.h"
#include "functions/math.h"
#include "functions/rendering.h"

#include "../core/zelda3d_runtime.h"
#include "../lighting/zelda3d_lighting.h"
#include "model_queries.h"
#include <fast/zelda3d_pose.h>

#include <stdlib.h>

float gZelda3dGiScaleMul = 1.0f;
float gZelda3dGiRotX = 0.0f;
float gZelda3dGiRotY = 0.0f;
float gZelda3dGiRotZ = 0.0f;

static int sItemsEnabled = -1;

typedef struct {
    s16 drawId;
    const char* zar;
    float scale;
} Zelda3dGetItemModel;

#define ZELDA3D_GI_SCALE 1.0f
static const Zelda3dGetItemModel kGetItemModels[] = {
    { GID_KOKIRI_EMERALD, "/actor/zelda_gi_jade.zar", ZELDA3D_GI_SCALE },
    { GID_GORON_RUBY, "/actor/zelda_gi_ruby.zar", ZELDA3D_GI_SCALE },
    { GID_ZORA_SAPPHIRE, "/actor/zelda_gi_sapphire.zar", ZELDA3D_GI_SCALE },
    { GID_ARROW_FIRE, "/actor/zelda_gi_fire_arrow.zar", ZELDA3D_GI_SCALE },
    { GID_ARROW_ICE, "/actor/zelda_gi_ice_arrow.zar", ZELDA3D_GI_SCALE },
    { GID_ARROW_LIGHT, "/actor/zelda_gi_light_arrow.zar", ZELDA3D_GI_SCALE },
};

static int Zelda3D_ItemsEnabled(void) {
    if (sItemsEnabled < 0) {
        const char* value = getenv("ZELDA3D_ITEMS");
        sItemsEnabled = (value == NULL || value[0] != '0') ? 1 : 0;
    }
    return sItemsEnabled;
}

static void Zelda3D_EmitGetItem(PlayState* play, int modelId, float scale) {
    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);
    Zelda3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    if (gZelda3dGiRotX != 0.0f) {
        Matrix_RotateX(gZelda3dGiRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    }
    if (gZelda3dGiRotY != 0.0f) {
        Matrix_RotateY(gZelda3dGiRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    }
    if (gZelda3dGiRotZ != 0.0f) {
        Matrix_RotateZ(gZelda3dGiRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    }
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    Zelda3D_SceneTint(play, tint);
    Zelda3D_GL_EmitPose(modelId);
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

int Zelda3D_TryDrawGetItem(PlayState* play, s16 drawId) {
    const Zelda3dGetItemModel* model = NULL;
    int modelId;
    size_t i;

    if (!Zelda3D_Enabled() || !Zelda3D_ItemsEnabled()) {
        return 0;
    }
    for (i = 0; i < ARRAY_COUNT(kGetItemModels); i++) {
        if (kGetItemModels[i].drawId == drawId) {
            model = &kGetItemModels[i];
            break;
        }
    }
    if (model == NULL) {
        return 0;
    }
    modelId = Zelda3D_AutoModelId(model->zar);
    if (modelId < 0) {
        return 0;
    }
    Zelda3D_EmitGetItem(play, modelId, model->scale * gZelda3dGiScaleMul);
    return 1;
}
