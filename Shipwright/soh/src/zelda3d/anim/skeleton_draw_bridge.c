#include "skeleton_draw_bridge.h"

#include "../behaviors/actor_behavior_bridge.h"
#include "../behaviors/actor/cucco_wing_override.h"
#include "../behaviors/actor/boss_fd2_bridge.h"
#include "../behaviors/actor/kokiri_kid.h"
#include "../behaviors/actor/townsfolk.h"
#include "../player/zelda3d_link.h"
#include "../render/actor_model_submission.h"
#include "../render/actor_skin_mask_control.h"
#include "../render/model_queries.h"
#include "../render/replacement_calibration.h"
#include "authored_playback.h"
#include "automatic_playback.h"
#include <fast/zelda3d_material_overrides.h>
#include <fast/zelda3d_pose.h>
#include "zelda3d_anim_override.h"

#include "fast/zelda3d_material_overrides.h"

#include "overlays/actors/ovl_En_Ko/z_en_ko.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../tables/zelda3d_bonemap.inc"
#include "../tables/zelda3d_animmap.inc"

void Zelda3D_DumpModelBones(int modelId);

float gZelda3dAnimFrame = 0.0f;
float gZelda3dAnimRate = 1.0f;
int gZelda3dAnimLive = 1;
int gZelda3dAnimDebug = 0;
char gZelda3dForceCsab[64] = "";
int gZelda3dLastAutoModel = -1;
int gZelda3dN64Anim = -1;

Actor* gZelda3dPendingActor = NULL;
int gZelda3dPendingModel = -1;
float gZelda3dPendingScale = 1.0f;
float gZelda3dPendingGroundOff = 0.0f;
int gZelda3dPendingAuto = 0;
const Zelda3DBoneMap* gZelda3dPendingBoneMap = NULL;
const char* gZelda3dPendingAnimOtr = NULL;
float gZelda3dPendingN64CurFrame = 0.0f;
float gZelda3dPendingN64AnimLength = 0.0f;
float gZelda3dPendingMorphWeight = 0.0f;
int gZelda3dColliderPass = 0;

void Zelda3D_SkeletonDrawResetRunState(void) {
    gZelda3dPendingActor = NULL;
}

static const char* Zelda3D_ResolveAutoCsab(const char* n64AnimOtr, const char* modelZar) {
    const char* generic = NULL;
    s32 i;

    if (n64AnimOtr == NULL) {
        return NULL;
    }
    if (strncmp(n64AnimOtr, "__OTR__", 7) == 0) {
        n64AnimOtr += 7;
    }
    for (i = 0; i < (s32)ARRAY_COUNT(kZelda3dAnimMaps); i++) {
        const char* zar;
        if (strcmp(kZelda3dAnimMaps[i].n64otr, n64AnimOtr) != 0) {
            continue;
        }
        zar = kZelda3dAnimMaps[i].zar;
        if (zar == NULL) {
            if (generic == NULL) {
                generic = kZelda3dAnimMaps[i].csab;
            }
        } else if (modelZar != NULL && (strcmp(zar, modelZar) == 0 ||
                                        (strncmp(zar, modelZar, strlen(zar)) == 0 && modelZar[strlen(zar)] == '|'))) {
            return kZelda3dAnimMaps[i].csab;
        }
    }
    return generic;
}

void Zelda3D_SetCurAnim(void* animation, float curFrame, float animLength, float morphWeight) {
    if (gZelda3dAnimDebug) {
        static int debugCounter = 0;
        if ((debugCounter++ % 60) == 0) {
            fprintf(stderr, "[SetCurAnim] pendingModel=%d anim=%s frame=%.1f/%.1f\n", gZelda3dPendingModel,
                    animation ? (const char*)animation : "(null)", curFrame, animLength);
            fflush(stderr);
        }
    }
    if (gZelda3dPendingModel >= 0) {
        gZelda3dPendingAnimOtr = (const char*)animation;
        gZelda3dPendingN64CurFrame = curFrame;
        gZelda3dPendingN64AnimLength = animLength;
        gZelda3dPendingMorphWeight = morphWeight;
    }
}

void Zelda3D_WalkN64Skeleton(void** skeleton, int limbCap, Zelda3D_LimbCb callback, void* userData) {
    StandardLimb* root;
    int stack[128];
    int stackSize = 0;
    int visited = 0;

    if (skeleton == NULL || limbCap <= 0) {
        return;
    }
    root = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);
    if (root == NULL || root->child == LIMB_DONE) {
        return;
    }
    stack[stackSize++] = root->child;
    while (stackSize > 0 && visited <= limbCap) {
        int limbIndex = stack[--stackSize];
        StandardLimb* limb;
        if (limbIndex < 0 || limbIndex >= limbCap) {
            continue;
        }
        limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
        if (limb == NULL) {
            continue;
        }
        visited++;
        callback(limbIndex, limb, userData);
        if (limb->sibling != LIMB_DONE && stackSize < (int)ARRAY_COUNT(stack)) {
            stack[stackSize++] = limb->sibling;
        }
        if (limb->child != LIMB_DONE && stackSize < (int)ARRAY_COUNT(stack)) {
            stack[stackSize++] = limb->child;
        }
    }
}

static void Zelda3D_AccumBoneLen(int limbIndex, StandardLimb* limb, void* userData) {
    float x = limb->jointPos.x;
    float y = limb->jointPos.y;
    float z = limb->jointPos.z;
    (void)limbIndex;
    *(float*)userData += sqrtf(x * x + y * y + z * z);
}

static float Zelda3D_N64SkelBoneLenSum(void** skeleton, int limbCap) {
    float sum = 0.0f;
    Zelda3D_WalkN64Skeleton(skeleton, limbCap, Zelda3D_AccumBoneLen, &sum);
    return sum;
}

static void Zelda3D_MaxLimbCb(int limbIndex, StandardLimb* limb, void* userData) {
    (void)limb;
    if (limbIndex > *(int*)userData) {
        *(int*)userData = limbIndex;
    }
}

static int Zelda3D_CountN64Limbs(void** skeleton) {
    int maxIndex = 0;
    Zelda3D_WalkN64Skeleton(skeleton, 64, Zelda3D_MaxLimbCb, &maxIndex);
    return maxIndex + 1;
}

static void Zelda3D_DumpLimbCb(int limbIndex, StandardLimb* limb, void* userData) {
    Vec3s* jointTable = (Vec3s*)userData;
    Vec3s rotation = jointTable[limbIndex + 1];
    fprintf(stderr, "[SKELDUMP] N64 limb=%d jointPos=(%d,%d,%d) child=%d sibling=%d rot=(%d,%d,%d)\n", limbIndex,
            limb->jointPos.x, limb->jointPos.y, limb->jointPos.z, limb->child, limb->sibling, rotation.x, rotation.y,
            rotation.z);
}

#define ENKO_MID(n) (1ull << (n))
static unsigned long long Zelda3D_AutoActorMidMask(int modelId, Actor* actor, s32 sceneNum) {
    const char* zar;
    if (gZelda3dEnKoMaskOverrideSet) {
        return gZelda3dEnKoMaskOverride;
    }
    if (actor != NULL && actor->id == ACTOR_EN_SA) {
        unsigned long long mask = ~0ull;
        if (sceneNum == SCENE_SACRED_FOREST_MEADOW) {
            mask &= ~(1ull << 2);
        } else {
            mask &= ~(1ull << 5);
        }
        return mask;
    }
    if (actor == NULL || actor->id != ACTOR_EN_KO) {
        return ~0ull;
    }
    zar = Zelda3D_AutoModelZar(modelId);
    if (zar != NULL && strstr(zar, "zelda_kw1") != NULL) {
        int enkoType = actor->params & 0xFF;
        if (enkoType == ENKO_TYPE_CHILD_FADO) {
            return ENKO_MID(0) | ENKO_MID(1) | ENKO_MID(2) | ENKO_MID(3);
        }
        return ENKO_MID(0) | ENKO_MID(1) | ENKO_MID(4);
    }
    if (zar != NULL && strstr(zar, "zelda_km1") != NULL) {
        return ENKO_MID(0) | ENKO_MID(1) | ENKO_MID(3);
    }
    return ~0ull;
}

static void Zelda3D_DumpPendingSkeleton(void** skeleton, Vec3s* jointTable, int limbCount) {
    static int enabled = -1;
    static int dumped[64];
    static int dumpedCount = 0;
    int index;

    if (enabled < 0) {
        const char* value = getenv("ZELDA3D_SKELDUMP");
        enabled = (value != NULL && value[0] == '1') ? 1 : 0;
    }
    if (!enabled) {
        return;
    }
    for (index = 0; index < dumpedCount; index++) {
        if (dumped[index] == gZelda3dPendingModel) {
            return;
        }
    }
    if (dumpedCount < (int)ARRAY_COUNT(dumped)) {
        Vec3f scale = gZelda3dPendingActor->scale;
        dumped[dumpedCount++] = gZelda3dPendingModel;
        fprintf(stderr, "[SKELDUMP] N64 actor=0x%x model=%d limbCount=%d actorScale=(%.5f,%.5f,%.5f)\n",
                gZelda3dPendingActor->id, gZelda3dPendingModel, limbCount, scale.x, scale.y, scale.z);
        Zelda3D_WalkN64Skeleton(skeleton, limbCount, Zelda3D_DumpLimbCb, jointTable);
        fflush(stderr);
        Zelda3D_DumpModelBones(gZelda3dPendingModel);
    }
}

static void Zelda3D_ApplyAutoScale(void** skeleton, int limbCount) {
    float n64Sum = Zelda3D_N64SkelBoneLenSum(skeleton, limbCount);
    float oot3dSum = Zelda3D_AutoModelBoneLenSum(gZelda3dPendingModel, limbCount);
    const char* zar;

    if (n64Sum > 1e-3f && oot3dSum > 1e-3f) {
        gZelda3dPendingScale = gZelda3dPendingActor->scale.x * (n64Sum / oot3dSum);
    }
    zar = Zelda3D_AutoModelZar(gZelda3dPendingModel);
    if (zar != NULL && strstr(zar, "zelda_zl4") != NULL) {
        gZelda3dPendingScale *= 2.0f;
    }
    if (zar != NULL && strstr(zar, "zelda_box") != NULL) {
        gZelda3dPendingScale /= 1.80f;
    }
    if (gZelda3dAnimDebug) {
        static int debugCounter = 0;
        if ((debugCounter++ % 30) == 0) {
            fprintf(stderr,
                    "[SKELSCALE] model %d n64sum=%.1f oot3dsum=%.1f ratio=%.3f actorScale=%.5f -> "
                    "scale=%.5f\n",
                    gZelda3dPendingModel, n64Sum, oot3dSum, n64Sum / oot3dSum, gZelda3dPendingActor->scale.x,
                    gZelda3dPendingScale);
            fflush(stderr);
        }
    }
}

static const char* Zelda3D_SelectAutoCsab(void) {
    const char* mapped = Zelda3D_ResolveAutoCsab(gZelda3dPendingAnimOtr, Zelda3D_AutoModelZar(gZelda3dPendingModel));
    const char* csab;
    const char* override;

    if (mapped != NULL && !Zelda3D_AutoModelHasCsab(gZelda3dPendingModel, mapped)) {
        mapped = NULL;
    }
    csab = mapped != NULL ? mapped : Zelda3D_AutoModelDefaultAnim(gZelda3dPendingModel);

    override = Zelda3D_EnKoCsabOverride(gZelda3dPendingModel, gZelda3dPendingActor);
    if (override != NULL && Zelda3D_AutoModelHasCsab(gZelda3dPendingModel, override)) {
        csab = override;
    }
    override = Zelda3D_EnHyCsabOverride(gZelda3dPendingModel, gZelda3dPendingActor);
    if (override != NULL && Zelda3D_AutoModelHasCsab(gZelda3dPendingModel, override)) {
        csab = override;
    }
    gZelda3dLastAutoModel = gZelda3dPendingModel;
    if (gZelda3dForceCsab[0] != '\0') {
        csab = gZelda3dForceCsab;
    }
    if (gZelda3dAnimDebug) {
        static int debugCounter = 0;
        if ((debugCounter++ % 30) == 0) {
            const char* otr = gZelda3dPendingAnimOtr ? gZelda3dPendingAnimOtr : "(none)";
            int locked = gZelda3dPendingN64AnimLength > 4.0f;
            fprintf(stderr, "SOH3D ANIM: model %d n64=%s -> csab=%s%s scale=%.5f n64frame=%.1f/%.1f %s\n",
                    gZelda3dPendingModel, otr, csab ? csab : "(bind pose)", mapped ? "" : " [default-idle]",
                    gZelda3dPendingScale, gZelda3dPendingN64CurFrame, gZelda3dPendingN64AnimLength,
                    locked ? "[PHASE-LOCK]" : "[free-run]");
            fflush(stdout);
        }
    }
    return csab;
}

static void Zelda3D_UpdatePendingAnimation(PlayState* play, Vec3s* jointTable, int limbCount, const char* csab) {
    const char* actorCsab = NULL;
    float actorCsabFrame = 0.0f;
    const char* actorMorphCsab = NULL;
    float actorMorphFrame = 0.0f;
    float actorMorphWeight = 0.0f;

    Zelda3D_ApplyProcOverride(play, gZelda3dPendingModel, jointTable, limbCount);
    Zelda3D_ApplyActorOverrides(gZelda3dPendingModel, gZelda3dPendingActor);
    Zelda3D_SetLimbOverride(NULL, NULL, 0);
    if (Zelda3D_BossFd2ResolveAnim(play, gZelda3dPendingActor, &actorCsab, &actorCsabFrame, &actorMorphCsab,
                                   &actorMorphFrame, &actorMorphWeight)) {
        if (actorMorphCsab != NULL && actorMorphWeight > 0.0f) {
            Zelda3D_UpdateAnimAuthoredMorph(gZelda3dPendingModel, actorCsab, actorCsabFrame, actorMorphCsab,
                                            actorMorphFrame, actorMorphWeight);
        } else {
            Zelda3D_UpdateAnim(gZelda3dPendingModel, actorCsab, actorCsabFrame);
        }
        Zelda3D_RecordLastAuto(gZelda3dPendingModel, actorCsab, actorCsabFrame);
    } else {
        Zelda3D_UpdateAnimAuto(gZelda3dPendingModel, csab, gZelda3dAnimRate, gZelda3dPendingN64CurFrame,
                               gZelda3dPendingN64AnimLength, gZelda3dPendingMorphWeight);
    }
}

static int Zelda3D_DoRetarget(PlayState* play, void** skeleton, Vec3s* jointTable, int limbCount) {
    const Zelda3DBoneMap* boneMap = gZelda3dPendingBoneMap;

    Zelda3D_DumpPendingSkeleton(skeleton, jointTable, limbCount);
    if (gZelda3dPendingAuto) {
        const char* csab;
        Zelda3D_ApplyAutoScale(skeleton, limbCount);
        csab = Zelda3D_SelectAutoCsab();
        Zelda3D_UpdatePendingAnimation(play, jointTable, limbCount, csab);
        Zelda3D_GL_SetMidMask(gZelda3dPendingModel,
                              Zelda3D_AutoActorMidMask(gZelda3dPendingModel, gZelda3dPendingActor, play->sceneNum));
        Zelda3D_EmitModelDraw(play, gZelda3dPendingModel, gZelda3dPendingActor, gZelda3dPendingScale,
                              gZelda3dPendingGroundOff);
        gZelda3dPendingModel = -1;
        gZelda3dPendingBoneMap = NULL;
        return 1;
    }
    if (boneMap != NULL) {
        Zelda3D_UpdateAnimN64Mapped(gZelda3dPendingModel, (const s16*)&jointTable[1], limbCount, boneMap->boneToLimb,
                                    boneMap->boneCount);
    } else {
        Zelda3D_UpdateAnimN64(gZelda3dPendingModel, (const s16*)&jointTable[1], limbCount);
    }
    Zelda3D_EmitModelDraw(play, gZelda3dPendingModel, gZelda3dPendingActor, gZelda3dPendingScale,
                          gZelda3dPendingGroundOff);
    gZelda3dPendingModel = -1;
    gZelda3dPendingBoneMap = NULL;
    return 1;
}

int Zelda3D_SkelAnimeDraw(PlayState* play, SkelAnime* skelAnime) {
    if (gZelda3dColliderPass || gZelda3dPendingModel < 0 || gZelda3dPendingActor == NULL) {
        return 0;
    }
    if (skelAnime == NULL || skelAnime->jointTable == NULL || skelAnime->limbCount == 0) {
        return 0;
    }
    gZelda3dPendingAnimOtr = (const char*)skelAnime->animation;
    gZelda3dPendingN64CurFrame = skelAnime->curFrame;
    gZelda3dPendingN64AnimLength = skelAnime->animLength;
    gZelda3dPendingMorphWeight = skelAnime->morphWeight;
    return Zelda3D_DoRetarget(play, skelAnime->skeleton, skelAnime->jointTable, skelAnime->limbCount);
}

int Zelda3D_SkelAnimeDrawRaw(PlayState* play, void** skeleton, Vec3s* jointTable) {
    int limbCount;
    if (gZelda3dColliderPass || gZelda3dPendingModel < 0 || gZelda3dPendingActor == NULL) {
        return 0;
    }
    if (skeleton == NULL || jointTable == NULL) {
        return 0;
    }
    limbCount = Zelda3D_CountN64Limbs(skeleton);
    if (limbCount <= 0) {
        return 0;
    }
    return Zelda3D_DoRetarget(play, skeleton, jointTable, limbCount);
}

void Zelda3D_AfterActorDraw(PlayState* play, Actor* actor) {
    (void)actor;
    Zelda3D_EndReplacementMeasurement(play);
    gZelda3dPendingActor = NULL;
    gZelda3dPendingModel = -1;
    gZelda3dPendingBoneMap = NULL;
}
