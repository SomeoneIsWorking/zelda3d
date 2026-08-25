// Zelda3D En_Niw procedural wing override: captures the N64 limb callback and replays its wing
// rotation on the OoT3D cucco rig.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_CUCCO_WING_OVERRIDE_H
#define ZELDA3D_BEHAVIORS_ACTOR_CUCCO_WING_OVERRIDE_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_SetLimbOverride(void* overrideFn, void* arg, int kind);
void Zelda3D_ApplyProcOverride(PlayState* play, int modelId, Vec3s* jointTable, int limbCount);

extern int gZelda3dProcOverride;
extern int gZelda3dWingForce;
extern int gZelda3dWingProbeActive;
extern int gZelda3dWingProbe[3];
extern int gZelda3dDbgBone;
extern int gZelda3dDbgBoneRot[3];
extern int gZelda3dWingMapSrc[3];
extern int gZelda3dWingMapSign[3];
extern int gZelda3dChickFlap;
extern int gZelda3dChickAxis;
extern int gZelda3dChickCenter;
extern int gZelda3dChickAmp;
extern float gZelda3dChickFreq;
extern int gZelda3dChickBone2Sign;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_CUCCO_WING_OVERRIDE_H
