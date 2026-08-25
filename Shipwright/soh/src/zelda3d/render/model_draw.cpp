#include "soh/frame_interpolation.h"
#include "model_draw.h"
#include "functions/math.h"
#include "functions/rendering.h"

#include "actor_model_submission.h"
#include "model_queries.h"
#include "scene_tint.h"

#include <fast/zelda3d_pose.h>
#include <fast/zelda3d_submission.h>

int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale) {
    Zelda3D_DrawModelGL(play, modelId, actor, worldScale, NULL, 0.0f, NULL, NULL);
    return 1;
}

int Zelda3D_DrawModelTransform(PlayState* play, int modelId, const Vec3f* pos, const Vec3f* rotYXZ, const Vec3f* scale,
                               float postRotX) {
    if (play == NULL || modelId < 0 || pos == NULL || rotYXZ == NULL || scale == NULL) {
        return 0;
    }

    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    Matrix_RotateY(rotYXZ->y, MTXMODE_APPLY);
    Matrix_RotateX(rotYXZ->x, MTXMODE_APPLY);
    Matrix_RotateZ(rotYXZ->z, MTXMODE_APPLY);
    Matrix_Scale(scale->x, scale->y, scale->z, MTXMODE_APPLY);
    if (postRotX != 0.0f) {
        Matrix_RotateX(postRotX, MTXMODE_APPLY);
    }

    const int xluPass = Zelda3D_AutoModelAllBlended(modelId);
    gSPMatrix(xluPass ? POLY_XLU_DISP++ : POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    Zelda3D_SceneTint(play, tint);
    Zelda3D_GL_EmitPose(modelId);
    gSPZelda3DDraw(xluPass ? POLY_XLU_DISP++ : POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

int Zelda3D_DrawModelBillboard(PlayState* play, int modelId, const Vec3f* pos, const Vec3f* scale) {
    if (play == NULL || modelId < 0 || pos == NULL || scale == NULL) {
        return 0;
    }

    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Scale(scale->x, scale->y, scale->z, MTXMODE_APPLY);

    const int xluPass = Zelda3D_AutoModelAllBlended(modelId);
    gSPMatrix(xluPass ? POLY_XLU_DISP++ : POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    Zelda3D_SceneTint(play, tint);
    Zelda3D_GL_EmitPose(modelId);
    gSPZelda3DDraw(xluPass ? POLY_XLU_DISP++ : POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

int Zelda3D_EmitActorBillboard(PlayState* play, int modelId, Actor* actor, float xOff, float yOff, float zOff,
                               float scale, u8 r, u8 g, u8 b, u8 a) {
    if (play == NULL || modelId < 0 || actor == NULL) {
        return 0;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Zelda3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x + xOff, actor->world.pos.y + yOff, actor->world.pos.z + zOff, MTXMODE_NEW);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    // High bit selects the flat-tint shader path, preserving the caller's per-draw color.
    gSPZelda3DDrawA(POLY_OPA_DISP++, modelId | (int)0x80000000, a, r, g, b);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}
