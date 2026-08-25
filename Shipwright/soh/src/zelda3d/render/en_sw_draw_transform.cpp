#include "en_sw_draw_transform.h"
#include "functions/math.h"

void Zelda3D_ApplyEnSwDrawTransform(Actor* actor) {
    if (!gZelda3dSwTilt || actor == NULL || actor->id != ACTOR_EN_SW || ((actor->params & 0xE000) >> 0xD) == 0) {
        return;
    }

    // EnSw_Draw applies this local transform after the engine's Actor::shape.rot transform.
    Matrix_RotateX(DEGF_TO_RADF(-80.0f), MTXMODE_APPLY);
    if (actor->colChkInfo.health != 0) {
        Matrix_Translate(0.0f, 0.0f, 200.0f * actor->scale.z, MTXMODE_APPLY);
    }
}
