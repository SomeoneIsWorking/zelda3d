#pragma once

// Shipping title-presentation observations and controls used for title
// lockstep and presentation comparisons.
extern "C" {

int Zelda3D_TitleCsFrame(void);
void Zelda3D_TitleCsSetFrame(int frame);
int Zelda3D_TitleCsEndFrame(void);
int Zelda3D_TitleCsCamera(float frame, float eye[3], float at[3], float up[3], float* fovDeg);

// Returns 0 when the title is inactive, 1 when the horse is mounted, and 2
// when only the computed path state is available.
int Zelda3D_Title_RiderState(float* outPos, int* outComputedYaw, int* outHorseWorldYaw, int* outHorseShapeYaw);

// Returns 0 when inactive, 1 for the live spline, and 2 for a held camera.
int Zelda3D_Title_CameraState(float* outEye, float* outAt, float* outUp, float* outFov);

} // extern "C"
