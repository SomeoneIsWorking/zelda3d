// Automatic CSAB playhead and transition policy.
#ifndef ZELDA3D_ANIM_AUTOMATIC_PLAYBACK_H
#define ZELDA3D_ANIM_AUTOMATIC_PLAYBACK_H

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dMorph;
extern int gZelda3dLogicFrame;

void Zelda3D_RecordLastAuto(int modelId, const char* csab, float frame);
int Zelda3D_LastAutoAnim(int modelId, const char** outCsab, float* outFrame);
void Zelda3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame, float n64AnimLength,
                            float morphWeight);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_ANIM_AUTOMATIC_PLAYBACK_H
