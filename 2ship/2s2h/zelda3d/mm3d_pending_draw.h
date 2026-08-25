#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

void Zelda3D_MM_SetPending(void* actor, int modelId, float worldScale, float groundOffset);
int Zelda3D_MM_SkelAnimeDrawRaw(struct PlayState* play, void** skeleton, void* jointTable, int limbCount);
void Zelda3D_MM_AfterActorDraw(void);
void Zelda3D_MM_OverridePending(float worldScale, float groundOffset);
int Zelda3D_MM_PendingModelId(void);

#ifdef __cplusplus
}

namespace Zelda3D::MM3D {

bool ResetPendingDraw();

} // namespace Zelda3D::MM3D
#endif
