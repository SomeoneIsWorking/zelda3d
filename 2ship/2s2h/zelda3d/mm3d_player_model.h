#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Resolve the retail MM3D body model for a 2S2H PlayerTransformation value.
// Returns zero when the form is invalid or its archive cannot be loaded.
int Zelda3D_MM_LookupPlayerModel(int playerForm, int* modelId, float* worldScale, float* groundOffset);

#ifdef __cplusplus
}
#endif
