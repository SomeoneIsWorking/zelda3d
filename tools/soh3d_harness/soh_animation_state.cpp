#include "soh_animation_state.h"

#include "global.h"
#include "z64player.h"

extern "C" int Zelda3D_GetAnimBonesLocal(int modelId, const char* animName, float frame, float* outRot3, int* outId,
                                         int* outParent, int maxBones, char* outCsab, int outCsabLen,
                                         float* outResolvedFrame);

extern "C" {

int SohState_ActorSkeleton(int category, int listIndex, short* jointsXYZ, int maxJoints, int* outJointCount,
                           int* outAnimFrame, int* outMorphFrame) {
    if (gPlayState == nullptr || category < 0 || category >= ACTORCAT_MAX) {
        return -1;
    }
    Actor* actor = gPlayState->actorCtx.actorLists[category].head;
    for (int index = 0; actor != nullptr && index < listIndex; ++index) {
        actor = actor->next;
    }
    if (actor == nullptr) {
        return -1;
    }
    if (actor->id != ACTOR_PLAYER) {
        return 0;
    }
    const SkelAnime& skeleton = reinterpret_cast<Player*>(actor)->skelAnime;
    if (skeleton.jointTable == nullptr || skeleton.limbCount <= 0 || skeleton.limbCount > 32) {
        return 0;
    }
    const int written = skeleton.limbCount < maxJoints ? skeleton.limbCount : maxJoints;
    for (int index = 0; index < written; ++index) {
        jointsXYZ[index * 3] = skeleton.jointTable[index].x;
        jointsXYZ[index * 3 + 1] = skeleton.jointTable[index].y;
        jointsXYZ[index * 3 + 2] = skeleton.jointTable[index].z;
    }
    if (outJointCount != nullptr) {
        *outJointCount = skeleton.limbCount;
    }
    if (outAnimFrame != nullptr) {
        *outAnimFrame = static_cast<int>(skeleton.curFrame);
    }
    if (outMorphFrame != nullptr) {
        *outMorphFrame = static_cast<int>(skeleton.morphWeight);
    }
    return written;
}

int SohState_AutoModelBonesLocal(int modelId, float* outRot3, int* outId, int* outParent, int maxBones, char* outCsab,
                                 int outCsabLen, float* outFrame) {
    return Zelda3D_GetAnimBonesLocal(modelId, nullptr, -1.0F, outRot3, outId, outParent, maxBones, outCsab, outCsabLen,
                                     outFrame);
}

} // extern "C"
