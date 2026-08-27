#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Resolve the retail MM3D body model for a 2S2H PlayerTransformation value.
// Returns zero when the form is invalid or its archive cannot be loaded.
int Zelda3D_MM_LookupPlayerModel(int playerForm, int* modelId, float* worldScale, float* groundOffset);

// Retail Player_Draw's form-specific base mesh visibility. Equipment/hand
// variants are selected by later helpers and are not part of this mask yet.
unsigned long long Zelda3D_MM_PlayerBaseMeshMask(int playerForm);

#ifdef __cplusplus
}
#endif
