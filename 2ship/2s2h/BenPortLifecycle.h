#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Release the previous run's game-owned OTRGlobals object before constructing the next one.
void Zelda3D_FreePreviousOTRGlobals(void);

#ifdef __cplusplus
}
#endif
