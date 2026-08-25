// Deterministic Cucco behavior controls and draw-state observations.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_CUCCO_CONTROL_H
#define ZELDA3D_BEHAVIORS_ACTOR_CUCCO_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dForceCuccoAgitate;
extern int gZelda3dCuccoState;
extern int gZelda3dCuccoDbgPhase;
extern short gZelda3dCuccoDbgWing[6];
extern int gZelda3dCuccoHeld;
void Zelda3D_CuccoAdvanceFrame(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_CUCCO_CONTROL_H
