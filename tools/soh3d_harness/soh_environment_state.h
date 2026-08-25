#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ENVIRONMENT_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ENVIRONMENT_STATE_H

extern "C" {
int SohState_ShrinkWindowVal(void);
int SohState_Zelda3DLive(float* ambient, float* light1Color, float* light2Color);
int SohState_DayTimeAndEnv(unsigned int* daytime, unsigned char* skybox1Idx, unsigned char* skybox2Idx,
                           float* skyboxBlend, unsigned char* liveAmbient, unsigned char* liveFogColor,
                           short* liveFogNear, short* liveFogFar);
int SohState_MoonDebug(float* sunPosY, float* color, float* scale, float* discScale);
int SohState_SetEnvSlot(unsigned char slot);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ENVIRONMENT_STATE_H
