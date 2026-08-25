#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Called after the MM game-state update and before draw. Commands therefore
// observe the completed update and mutations are visible to the same draw.
void Zelda3D_MmReplTick(void);
void Zelda3D_MmReplResetRunState(void);

#ifdef __cplusplus
}
#endif
