#ifndef ZELDA3D_ANIM_SKELETON_DRAW_BRIDGE_H
#define ZELDA3D_ANIM_SKELETON_DRAW_BRIDGE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Zelda3DBoneMap;

int Zelda3D_SkelAnimeDraw(PlayState* play, SkelAnime* skelAnime);
int Zelda3D_SkelAnimeDrawRaw(PlayState* play, void** skeleton, Vec3s* jointTable);
void Zelda3D_AfterActorDraw(PlayState* play, Actor* actor);
void Zelda3D_SetCurAnim(void* animation, float curFrame, float animLength, float morphWeight);
void Zelda3D_SkeletonDrawResetRunState(void);

typedef void (*Zelda3D_LimbCb)(int limbIndex, StandardLimb* limb, void* userData);
void Zelda3D_WalkN64Skeleton(void** skeleton, int limbCap, Zelda3D_LimbCb callback, void* userData);

extern Actor* gZelda3dPendingActor;
extern int gZelda3dPendingModel;
extern float gZelda3dPendingScale;
extern float gZelda3dPendingGroundOff;
extern int gZelda3dPendingAuto;
extern const struct Zelda3DBoneMap* gZelda3dPendingBoneMap;
extern const char* gZelda3dPendingAnimOtr;
extern float gZelda3dPendingN64CurFrame;
extern float gZelda3dPendingN64AnimLength;
extern float gZelda3dPendingMorphWeight;

extern float gZelda3dAnimFrame;
extern float gZelda3dAnimRate;
extern int gZelda3dAnimLive;
extern int gZelda3dAnimDebug;
extern char gZelda3dForceCsab[64];
extern int gZelda3dLastAutoModel;
extern int gZelda3dN64Anim;
extern int gZelda3dColliderPass;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_ANIM_SKELETON_DRAW_BRIDGE_H
