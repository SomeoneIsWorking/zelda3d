#include "soh/OTRGlobals.h"
#include "soh/host/item_randomizer_bridge.h"
#include "draw.h"

#include "soh/host/math_constants.h"
#include "soh/cvar_prefixes.h"
#include "randomizerTypes.h"
#include "soh_assets.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/randomizer/randomizer.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "zelda3d/core/zelda3d_runtime.h"
#include "functions/animation.h"
#include "functions/math.h"
#include "functions/rendering.h"
#include "variables.h"
#include "dungeon.h"
#include "objects/object_box/object_box.h"
#include "objects/object_gi_key/object_gi_key.h"
#include "objects/object_gi_bosskey/object_gi_bosskey.h"
#include "objects/object_gi_bracelet/object_gi_bracelet.h"
#include "objects/object_gi_compass/object_gi_compass.h"
#include "objects/object_gi_map/object_gi_map.h"
#include "objects/object_gi_hearts/object_gi_hearts.h"
#include "objects/object_gi_scale/object_gi_scale.h"
#include "objects/object_gi_fire/object_gi_fire.h"
#include "objects/object_fish/object_fish.h"
#include "objects/object_toki_objects/object_toki_objects.h"
#include "objects/object_gi_bomb_2/object_gi_bomb_2.h"
#include "objects/object_goma/object_goma.h"
#include "objects/object_kingdodongo/object_kingdodongo.h"
#include "objects/object_bv/object_bv.h"
#include "objects/object_gnd/object_gnd.h"
#include "objects/object_fd/object_fd.h"
#include "objects/object_mamenoki/object_mamenoki.h"
#include "objects/object_mo/object_mo.h"
#include "objects/object_mori_objects/object_mori_objects.h"
#include "objects/object_sst/object_sst.h"
#include "overlays/actors/ovl_Boss_Goma/z_boss_goma.h"
#include "objects/object_tw/object_tw.h"
#include "objects/object_ganon2/object_ganon2.h"
#include "objects/object_gi_shield_1/object_gi_shield_1.h"
}

Gfx* GetEmptyDlist(GraphicsContext* gfxCtx) {
    Gfx* dListHead;
    Gfx* dList;

    dList = dListHead = (Gfx*)Graph_Alloc(gfxCtx, sizeof(Gfx) * 1);

    gSPEndDisplayList(dListHead++);

    return dList;
}

extern "C" s32 OverrideLimbDrawGohma(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    OPEN_DISPS(play->state.gfxCtx);

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 255, 170, 255);

    switch (limbIndex) {
        case BOSSGOMA_LIMB_EYE:
            gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 63);
            break;

        case BOSSGOMA_LIMB_IRIS:
            gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
            break;
    }

    CLOSE_DISPS(play->state.gfxCtx);
    return false;
}

#define LIMB_COUNT_GOHMA 86
extern "C" void DrawGohma(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_GOHMA];
    static Vec3s otherTable[LIMB_COUNT_GOHMA];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_Init(play, &skelAnime, (SkeletonHeader*)&gGohmaSkel, (AnimationHeader*)&gGohmaIdleCrouchedAnim,
                       jointTable, otherTable, LIMB_COUNT_GOHMA);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, -20.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.005f, 0.005f, 0.005f, MTXMODE_APPLY);

    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)GetEmptyDlist(play->state.gfxCtx));
    SkelAnime_DrawSkeletonOpa(play, &skelAnime, OverrideLimbDrawGohma, NULL, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

#define LIMB_COUNT_KING_DODONGO 49
extern "C" void DrawKingDodongo(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_KING_DODONGO];
    static Vec3s otherTable[LIMB_COUNT_KING_DODONGO];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_Init(play, &skelAnime, (SkeletonHeader*)&object_kingdodongo_Skel_01B310,
                       (AnimationHeader*)&object_kingdodongo_Anim_00F0D8, jointTable, otherTable,
                       LIMB_COUNT_KING_DODONGO);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, -20.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.003f, 0.003f, 0.003f, MTXMODE_APPLY);

    SkelAnime_DrawSkeletonOpa(play, &skelAnime, NULL, NULL, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" s32 OverrideLimbDrawBarinade(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                        void* thisx) {
    OPEN_DISPS(play->state.gfxCtx);

    s16 unk_1AC = play->gameplayFrames * 0xC31;
    f32 unk_1A0 = 0.0f;
    f32 unk_1A4 = 0.0f;

    if (limbIndex == 20) {
        gDPPipeSync(POLY_OPA_DISP++);
        gSPSegment(POLY_OPA_DISP++, 0x08,
                   (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 8, 16, 1, 0,
                                                 (play->gameplayFrames * -2) % 64, 16, 16, 0, 0, 0, -2));
        gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 200);
        Matrix_RotateX(-M_PIf / 2.0f, MTXMODE_APPLY);
    } else if ((limbIndex >= 10) && (limbIndex < 20)) {
        rot->x -= 0x4000;
        *dList = NULL;
    } else if (limbIndex == 6) {
        unk_1A4 = (Math_SinS(unk_1AC) * 0.05f) + 1.0f;
        Matrix_Scale(unk_1A4, unk_1A4, unk_1A4, MTXMODE_APPLY);
    } else if (limbIndex == 61) {
        unk_1A0 = (Math_CosS(unk_1AC) * 0.1f) + 1.0f;
        Matrix_Scale(unk_1A0, unk_1A0, unk_1A0, MTXMODE_APPLY);
    } else if (limbIndex == 7) {
        rot->x -= 0xCCC;
    }

    CLOSE_DISPS(play->state.gfxCtx);
    return false;
}

extern "C" void PostLimbDrawBarinade(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    OPEN_DISPS(play->state.gfxCtx);

    if (limbIndex == 25) {
        gSPSegment(POLY_XLU_DISP++, 0x09,
                   (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, (play->gameplayFrames * 10) % 128, 16, 32, 1,
                                                 0, (play->gameplayFrames * 5) % 128, 16, 32, 0, 10, 0, 5));
        gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gBarinadeDL_008D70);
    } else if ((limbIndex >= 10) && (limbIndex < 20)) {
        if (((limbIndex >= 16) || (limbIndex == 10))) {
            gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gBarinadeDL_008BB8);
        } else if ((limbIndex >= 11)) {
            gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gBarinadeDL_008BB8);
        }
    } else if ((*dList != NULL) && (limbIndex >= 29) && (limbIndex < 56)) {
        gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, *dList);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

#define LIMB_COUNT_BARINADE 64
extern "C" void DrawBarinade(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_BARINADE];
    static Vec3s otherTable[LIMB_COUNT_BARINADE];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_Init(play, &skelAnime, (SkeletonHeader*)&gBarinadeBodySkel, (AnimationHeader*)&gBarinadeBodyAnim,
                       jointTable, otherTable, LIMB_COUNT_BARINADE);

        // Freeze barniade on the last frame
        f32 lastFrame = Animation_GetLastFrame((AnimationHeader*)&gBarinadeBodyAnim);
        Animation_Change(&skelAnime, (AnimationHeader*)&gBarinadeBodyAnim, 1.0f, lastFrame, lastFrame, ANIMMODE_ONCE,
                         0.0f);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Matrix_Translate(0.0f, -25.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.03f, 0.03f, 0.03f, MTXMODE_APPLY);

    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 8, 16, 1, 0,
                                             (play->gameplayFrames * -10) % 16, 16, 16, 0, 0, 0, -10));
    gSPSegment(POLY_OPA_DISP++, 0x09,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, (play->gameplayFrames * -10) % 32, 16, 0x20, 1,
                                             0, (play->gameplayFrames * -5) % 32, 16, 32, 0, -10, 0, -5));

    SkelAnime_DrawSkeletonOpa(play, &skelAnime, OverrideLimbDrawBarinade, PostLimbDrawBarinade, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

#define LIMB_COUNT_PHANTOM_GANON 26
extern "C" void DrawPhantomGanon(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_PHANTOM_GANON];
    static Vec3s otherTable[LIMB_COUNT_PHANTOM_GANON];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_Init(play, &skelAnime, (SkeletonHeader*)&gPhantomGanonSkel,
                       (AnimationHeader*)&gPhantomGanonNeutralAnim, jointTable, otherTable, LIMB_COUNT_PHANTOM_GANON);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, 10.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.007f, 0.007f, 0.007f, MTXMODE_APPLY);

    // Eye color
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)GetEmptyDlist(play->state.gfxCtx));
    SkelAnime_DrawSkeletonOpa(play, &skelAnime, NULL, NULL, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

#define LIMB_COUNT_VOLVAGIA 7
extern "C" void DrawVolvagia(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_VOLVAGIA];
    static Vec3s otherTable[LIMB_COUNT_VOLVAGIA];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_Init(play, &skelAnime, (SkeletonHeader*)&gVolvagiaHeadSkel,
                       (AnimationHeader*)&gVolvagiaHeadEmergeAnim, jointTable, otherTable, LIMB_COUNT_VOLVAGIA);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Scale(0.007f, 0.007f, 0.007f, MTXMODE_APPLY);

    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)gVolvagiaEyeOpenTex);
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, play->state.frames * 4, 120, 0x20, 0x20, 1,
                                             play->state.frames * 3, play->state.frames * -2, 0x20, 0x20, 4, 0, 3, -2));

    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);

    SkelAnime_DrawSkeletonOpa(play, &skelAnime, NULL, NULL, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void DrawMorpha(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    Matrix_Scale(0.015f, 0.015f, 0.015f, MTXMODE_APPLY);

    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, play->state.frames * 3, play->state.frames * 3, 32,
                                             32, 1, play->state.frames * -3, play->state.frames * -3, 32, 32, 3, 3, -3,
                                             -3));

    gSPSegment(POLY_XLU_DISP++, 0x09,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, play->state.frames * 3, 0, 32, 32, 1, 0,
                                             play->state.frames * -5, 32, 32, 3, 0, 0, -5));

    Matrix_RotateX(play->state.frames * 0.1f, MTXMODE_APPLY);
    Matrix_RotateZ(play->state.frames * 0.16f, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 255, 255);

    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gMorphaCoreMembraneDL);

    gDPPipeSync(POLY_XLU_DISP++);

    gDPSetEnvColor(POLY_XLU_DISP++, 0, 220, 255, 128);
    gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 255, 255);

    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gMorphaCoreNucleusDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

#define LIMB_COUNT_BONGO_BONGO 27
extern "C" void DrawBongoBongo(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_BONGO_BONGO];
    static Vec3s otherTable[LIMB_COUNT_BONGO_BONGO];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_InitFlex(play, &skelAnime, (FlexSkeletonHeader*)&gBongoLeftHandSkel,
                           (AnimationHeader*)&gBongoLeftHandIdleAnim, jointTable, otherTable, LIMB_COUNT_BONGO_BONGO);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, -25.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.006f, 0.006f, 0.006f, MTXMODE_APPLY);

    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)GetEmptyDlist(play->state.gfxCtx));

    gDPSetPrimColor(POLY_OPA_DISP++, 0x80, 0x80, 255, 255, 255, 255);
    SkelAnime_DrawSkeletonOpa(play, &skelAnime, NULL, NULL, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" s32 OverrideLimbDrawKotake(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                      void* thisx) {
    if (limbIndex == 21) { // Head
        *dList = (Gfx*)gTwinrovaKotakeHeadDL;
    }

    return false;
}

extern "C" void PostLimbDrawKotake(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    OPEN_DISPS(play->state.gfxCtx);

    if (limbIndex == 21) { // Head
        gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gTwinrovaKotakeIceHairDL);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

#define LIMB_COUNT_KOTAKE 27
extern "C" void DrawKotake(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_KOTAKE];
    static Vec3s otherTable[LIMB_COUNT_KOTAKE];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_InitFlex(play, &skelAnime, (FlexSkeletonHeader*)&gTwinrovaKotakeSkel,
                           (AnimationHeader*)&gTwinrovaKotakeKoumeFlyAnim, jointTable, otherTable, LIMB_COUNT_KOTAKE);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Matrix_Translate(0.0f, -10.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);

    gSPSegment(POLY_OPA_DISP++, 10, (uintptr_t)gTwinrovaKotakeKoumeEyeOpenTex);
    gSPSegment(POLY_XLU_DISP++, 10, (uintptr_t)gTwinrovaKotakeKoumeEyeOpenTex);
    gSPSegment(POLY_XLU_DISP++, 8,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 & 0x7F, 0 & 0x7F, 0x20, 0x20, 1,
                                             play->state.frames & 0x7F, (play->state.frames * -7) & 0xFF, 0x20, 0x40, 0,
                                             0, 1, -7));

    gSPSegment(POLY_XLU_DISP++, 9,
               (uintptr_t)Gfx_TexScrollEx(play->state.gfxCtx, 0 & 0x7F, play->state.frames & 0xFF, 0x20, 0x40, 0, 1));

    SkelAnime_DrawSkeletonOpa(play, &skelAnime, OverrideLimbDrawKotake, PostLimbDrawKotake, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" s32 OverrideLimbDrawGanon(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    OPEN_DISPS(play->state.gfxCtx);

    if (limbIndex >= 42) { // Tail
        // Brighten up tail
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
    }

    CLOSE_DISPS(play->state.gfxCtx);
    return false;
}

#define LIMB_COUNT_GANON 47
extern "C" void DrawGanon(PlayState* play) {
    static Zelda3DOnce initialized;
    static SkelAnime skelAnime;
    static Vec3s jointTable[LIMB_COUNT_GANON];
    static Vec3s otherTable[LIMB_COUNT_GANON];
    static u32 lastUpdate = 0;

    if (Zelda3D_Once(&initialized)) {
        SkelAnime_InitFlex(play, &skelAnime, (FlexSkeletonHeader*)&gGanonSkel, (AnimationHeader*)&gGanonGuardIdleAnim,
                           jointTable, otherTable, LIMB_COUNT_GANON);
    }

    if (lastUpdate != play->state.frames) {
        lastUpdate = play->state.frames;
        SkelAnime_Update(&skelAnime);
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, -33.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.005f, 0.005f, 0.005f, MTXMODE_APPLY);

    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)gGanonEyeOpenTex);

    SkelAnime_DrawSkeletonOpa(play, &skelAnime, OverrideLimbDrawGanon, NULL, NULL);

    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void Randomizer_DrawBossSoul(PlayState* play, GetItemEntry* getItemEntry) {
    s16 slot;
    if (getItemEntry->getItemId != RG_ICE_TRAP) {
        slot = getItemEntry->getItemId - RG_GOHMA_SOUL;
    } else {
        slot = getItemEntry->drawItemId - RG_GOHMA_SOUL;
    }

    s16 flameColors[9][3] = {
        { 0, 255, 0 },     // Gohma
        { 255, 0, 100 },   // King Dodongo
        { 50, 255, 255 },  // Barinade
        { 4, 195, 46 },    // Phantom Ganon
        { 237, 95, 95 },   // Volvagia
        { 85, 180, 223 },  // Morpha
        { 126, 16, 177 },  // Bongo Bongo
        { 222, 158, 47 },  // Twinrova
        { 150, 150, 150 }, // Ganon/Dorf
    };

    // Draw the blue fire DL but coloured to the boss soul.
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 8,
               (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 0),
                                             0 * (play->state.frames * 0), 16, 32, 1, 1 * (play->state.frames * 1),
                                             -1 * (play->state.frames * 8), 16, 32, 0, 0, 1, -8));
    Matrix_Push();
    Matrix_Translate(0.0f, -70.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(5.0f, 5.0f, 5.0f, MTXMODE_APPLY);
    Matrix_ReplaceRotation(&play->billboardMtxF);
    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    gDPSetGrayscaleColor(POLY_XLU_DISP++, flameColors[slot][0], flameColors[slot][1], flameColors[slot][2], 255);
    gSPGrayscale(POLY_XLU_DISP++, true);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gGiBlueFireFlameDL);
    gSPGrayscale(POLY_XLU_DISP++, false);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);

    // Draw the generic boss soul model
    if (CVarGetInteger(CVAR_RANDOMIZER_ENHANCEMENT("SimplerBossSoulModels"), 0)) {
        OPEN_DISPS(play->state.gfxCtx);
        gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_MODELVIEW | G_MTX_LOAD);
        if (slot == 8) { // For Ganon only...
            gDPSetEnvColor(POLY_XLU_DISP++, 0, 0, 0, 255);
        } else {
            gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 255, 255);
        }
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gBossSoulSkullDL);
        CLOSE_DISPS(play->state.gfxCtx);
        // Draw the boss' skeleton
    } else {
        switch (slot) {
            case 0: // Gohma
                DrawGohma(play);
                break;
            case 1: // King Dodongo
                DrawKingDodongo(play);
                break;
            case 2: // Barinade
                DrawBarinade(play);
                break;
            case 3: // Phantom Ganon
                DrawPhantomGanon(play);
                break;
            case 4: // Volvagia
                DrawVolvagia(play);
                break;
            case 5: // Morpha
                DrawMorpha(play);
                break;
            case 6: // Bongo Bongo
                DrawBongoBongo(play);
                break;
            case 7: // Twinrova
                DrawKotake(play);
                break;
            case 8: // Ganon
                DrawGanon(play);
                break;
            default:
                break;
        }
    }
}
