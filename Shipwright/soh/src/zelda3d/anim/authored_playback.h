// Explicit authored-CSAB sampling and blend controls.
#ifndef ZELDA3D_ANIM_AUTHORED_PLAYBACK_H
#define ZELDA3D_ANIM_AUTHORED_PLAYBACK_H

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame);
void Zelda3D_UpdateAnimAuthoredMorph(int modelId, const char* inName, float inFrame, const char* outName,
                                     float outFrame, float weight);
void Zelda3D_UpdateAnimTwoSource(int modelId, const char* lowerAnim, float lowerRate, const char* upperAnim,
                                 float upperCurFrame, float upperAnimLength, const unsigned char* upperMask,
                                 int maskCount);
void Zelda3D_UpdateAnimWorldBones(int modelId, const char* animName, float frame, int firstBone,
                                  const float* worldMatrices3x4, int matrixCount);
void Zelda3D_SetAnimTransScale(int modelId, float scale);
void Zelda3D_SetAnimRootPin(int modelId, int boneId, unsigned mask);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_ANIM_AUTHORED_PLAYBACK_H
