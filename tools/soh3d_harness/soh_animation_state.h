#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ANIMATION_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ANIMATION_STATE_H

extern "C" {
int SohState_ActorSkeleton(int cat, int listIndex, short* jointsXYZ, int maxJoints, int* outJointCount,
                           int* outAnimFrame, int* outMorphFrame);
int SohState_AutoModelBonesLocal(int modelId, float* outRot3, int* outId, int* outParent, int maxBones, char* outCsab,
                                 int outCsabLen, float* outFrame);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ANIMATION_STATE_H
