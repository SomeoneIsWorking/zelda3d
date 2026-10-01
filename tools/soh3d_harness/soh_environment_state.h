#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ENVIRONMENT_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ENVIRONMENT_STATE_H

extern "C" {
int SohState_ShrinkWindowVal(void);
// Reports the SUBMITTED scene ambient, plus gZelda3dWorldAmbColor separately.
//
// The two have each been mistaken for the other, in opposite directions, and both mistakes look like
// a major ambient-hue bug:
//
// * Reading `gZelda3dWorldAmbColor` (what the shader reads) reports its init value (0,0,1) forever,
//   because it is only written when `gZelda3dWorldAmbOverride` is 0 and that defaults to 1.
// * Reading only an N64-sourced field reported a neutral (26,26,31) where the 3DS renders a
//   saturated blue.
//
// The submitted value is `gZelda3dAmbient`, which Zelda3D_GL_SetLightParams writes; at the title that
// is the 3DS title-palette blend, verified against the oracle.
// The out-param is named `n64Ambient` for continuity but carries gZelda3dWorldAmbColor; see the
// implementation comment.
int SohState_Zelda3DLive(float* ambient, float* light1Color, float* light2Color, float* n64Ambient);
int SohState_DayTimeAndEnv(unsigned int* daytime, unsigned char* skybox1Idx, unsigned char* skybox2Idx,
                           float* skyboxBlend, unsigned char* liveAmbient, unsigned char* liveFogColor,
                           short* liveFogNear, short* liveFogFar);
int SohState_MoonDebug(float* sunPosY, float* color, float* scale, float* discScale);
int SohState_SetEnvSlot(unsigned char slot);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ENVIRONMENT_STATE_H
