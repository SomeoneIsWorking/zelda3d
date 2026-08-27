// Boss_Fd2's ten-point mane chain, shared by the native fallback and Zelda3D CMB segments.
#include "boss_fd2_mane.h"

#include "boss_fd2_bridge.h"
#include "objects/object_fd2/object_fd2.h"
#include "overlays/actors/ovl_Boss_Fd2/z_boss_fd2.h"
#include "soh/frame_interpolation.h"

static void ApproachManePull(f32* value) {
    const f32 delta = -*value;
    // OoT3D FUN_00373500 scales both approach arguments by s16(global+0x110) / 3.
    const f32 tickScale = 2.0f / 3.0f;
    f32 step = fabsf(delta) < 0.00001f ? delta : delta * tickScale;

    step = CLAMP(step, -tickScale, tickScale);
    *value += step;
}

static void SimulateMane(Vec3f* head, Vec3f* pos, Vec3f* rot, Vec3f* pull) {
    static const f32 yAcceleration[10] = { 0.0f, 100.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    static const f32 yLimit[10] = { 0.0f, 5.0f, -10.0f, 500.0f, 500.0f, 500.0f, 500.0f, 500.0f, 500.0f, 500.0f };
    static const f32 segmentLengthScale[10] = { 0.4f, 0.6f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    pos[0] = *head;
    for (s16 i = 1; i < 10; ++i) {
        ApproachManePull(&pull[i].x);
        ApproachManePull(&pull[i].y);
        ApproachManePull(&pull[i].z);
    }

    for (s16 i = 1; i < 10; ++i) {
        Vec3f delta;
        delta.x = pos[i].x + pull[i].x - pos[i - 1].x;
        f32 nextY = pos[i].y + pull[i].y - 2.0f + yAcceleration[i];
        const f32 maximumY = pos[i - 1].y + yLimit[i];
        if (nextY > maximumY) {
            nextY = maximumY;
        }
        if (head->y >= -910.0f && nextY < 110.0f) {
            nextY = 110.0f;
        }
        delta.y = nextY - pos[i - 1].y;
        delta.z = pos[i].z + pull[i].z - pos[i - 1].z;

        const f32 angleY = Math_Atan2F(delta.z, delta.x);
        const f32 angleX = -Math_Atan2F(sqrtf(SQ(delta.x) + SQ(delta.z)), delta.y);
        rot[i - 1].y = angleY;
        rot[i - 1].x = angleX;

        Vec3f localStep = { 0.0f, 0.0f, segmentLengthScale[i] * 25.0f };
        Vec3f worldStep;
        Matrix_RotateY(angleY, MTXMODE_NEW);
        Matrix_RotateX(angleX, MTXMODE_APPLY);
        Matrix_MultVec3f(&localStep, &worldStep);

        const Vec3f previous = pos[i];
        pos[i].x = pos[i - 1].x + worldStep.x;
        pos[i].y = pos[i - 1].y + worldStep.y;
        pos[i].z = pos[i - 1].z + worldStep.z;
        pull[i].x = CLAMP(((pos[i].x - previous.x) * 88.0f) / 100.0f, -30.0f, 30.0f);
        pull[i].y = CLAMP(((pos[i].y - previous.y) * 88.0f) / 100.0f, -30.0f, 30.0f);
        pull[i].z = CLAMP(((pos[i].z - previous.z) * 88.0f) / 100.0f, -30.0f, 30.0f);
    }
}

void Zelda3D_BossFd2UpdateMane(Actor* actor, PlayState* play, s16 chain, Vec3f* head, Vec3f* pos, Vec3f* rot,
                               Vec3f* pull, f32* scale, s16 substeps) {
    static const f32 segmentLengthScale[10] = { 0.4f, 0.6f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    gDPPipeSync(POLY_OPA_DISP++);

    for (s16 step = 0; step < substeps; ++step) {
        SimulateMane(head, pos, rot, pull);
    }

    for (s16 i = 0; i < 9; ++i) {
        FrameInterpolation_RecordOpenChild(actor, ((BossFd2*)actor)->epoch + i * 25);
        Matrix_Translate(pos[i].x, pos[i].y, pos[i].z, MTXMODE_NEW);
        Matrix_RotateY(rot[i].y, MTXMODE_APPLY);
        Matrix_RotateX(rot[i].x, MTXMODE_APPLY);
        const f32 xyScale = (0.01f - i * 0.0009f) * segmentLengthScale[i] * scale[i];
        const Vec3f modelScale = { xyScale, xyScale, 0.01f * segmentLengthScale[i] };
        if (!Zelda3D_BossFd2DrawManeSegment(play, actor, chain, i, &pos[i], &rot[i], &modelScale)) {
            Matrix_Scale(modelScale.x, modelScale.y, modelScale.z, MTXMODE_APPLY);
            Matrix_RotateX(M_PI / 2.0f, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gHoleVolvagiaManeModelDL);
        }
        FrameInterpolation_RecordCloseChild();
    }

    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}
