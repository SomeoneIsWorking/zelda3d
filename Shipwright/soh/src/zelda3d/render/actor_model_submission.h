// Internal actor-model submission contract shared with replacement policy.
#ifndef ZELDA3D_RENDER_ACTOR_MODEL_SUBMISSION_H
#define ZELDA3D_RENDER_ACTOR_MODEL_SUBMISSION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef const char* (*Zelda3D_AnimResolver)(Actor* actor);
typedef int (*Zelda3D_JointResolver)(Actor* actor, const s16** outJointRots, int* outLimbCount);

void Zelda3D_DrawModelGL(PlayState* play, int modelId, Actor* actor, float worldScale, const char* animName,
                         float groundOffset, Zelda3D_AnimResolver resolveAnim, Zelda3D_JointResolver resolveJoints);
void Zelda3D_EmitModelDraw(PlayState* play, int modelId, Actor* actor, float worldScale, float groundOffset);
int Zelda3D_N64AnimEnabled(void);
void Zelda3D_UpdateAnimN64(int modelId, const s16* jointRots, int rotCount);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_ACTOR_MODEL_SUBMISSION_H
