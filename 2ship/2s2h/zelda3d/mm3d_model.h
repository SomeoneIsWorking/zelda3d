// Public MM3D model catalog contract.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_EnsureModelProvider(void);
int Zelda3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset);
float Zelda3D_ModelScaleById(int modelId);
int Zelda3D_IsModelSkinned(int modelId);
int Zelda3D_MM_RoomModelId(const char* sceneName, int roomNum);

// Developer-facing catalog controls used by the model REPL.
void Zelda3D_SetObjectScale(int objectId, float scale);
void Zelda3D_ListModels(void (*emitLine)(const char* line, void* user), void* user);

#ifdef __cplusplus
}
#endif
