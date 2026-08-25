#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_PLAYER_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_PLAYER_STATE_H

extern "C" {
int SohState_PlayerPos(float* px, float* py, float* pz, short* rx, short* ry, short* rz);
int SohState_PlayerWallInfo(unsigned int* outBgFlags, int* outWallYaw, int* outWallBgId, unsigned long* outWallPoly,
                            float* outSpeedXZ, float* outVelY);
int SohState_TeleportPlayer(float x, float y, float z);
int SohState_SetPlayerYaw(int yawS16);
int SohState_SetLinkAge(int age);
int SohState_GetLinkAge(void);
int SohState_DumpControlFlags(unsigned int* outStateFlags1, int* outCsState, unsigned int* outCsIndex,
                              unsigned int* outNextCsIndex, int* outTransTrigger, int* outCsAction);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_PLAYER_STATE_H
