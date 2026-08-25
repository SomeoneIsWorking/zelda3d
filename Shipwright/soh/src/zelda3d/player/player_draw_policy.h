// Runtime policy and tunables for the OoT3D player body.
#ifndef ZELDA3D_PLAYER_DRAW_POLICY_H
#define ZELDA3D_PLAYER_DRAW_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_LinkEnabled(void);
int Zelda3D_LinkAnimSrc(void);
const char* Zelda3D_LinkWalkRunGate(const char* csab, float speedXZ);

extern int gZelda3dLinkOn;
extern float gZelda3dLinkScale;
extern float gZelda3dLinkRotX;
extern float gZelda3dLinkRotY;
extern float gZelda3dLinkRotZ;
extern float gZelda3dLinkForceFrame;
extern char gZelda3dLinkForceCsab[64];
extern char gZelda3dLinkForceTwoLower[64];
extern char gZelda3dLinkForceTwoUpper[64];
extern int gZelda3dHeldAttach;
extern int gZelda3dFocusFix;
extern float gZelda3dLinkLocoGain;
extern int gZelda3dLinkAnimSrc;
extern const unsigned char kLinkUpperBodyMask[25];

#define ZELDA3D_LINK_IDLE_CSAB "nml_wait_typeA_20f"
#define ZELDA3D_LINK_WALKRUN_SPEED 3.6f

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_PLAYER_DRAW_POLICY_H
