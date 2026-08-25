// Zelda3D process and per-run lifecycle.
#ifndef ZELDA3D_CORE_RUNTIME_H
#define ZELDA3D_CORE_RUNTIME_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_Enabled(void);
extern int gZelda3dEnabled;
void Zelda3D_FrameBegin(void);
void Zelda3D_FrameEndUpdate(PlayState* play);
void Zelda3D_RegisterHostHooks(void);
void Zelda3D_CoreRunBegin(void);
int Zelda3D_CoreRunEnd(void);

typedef struct {
    unsigned int epoch;
} Zelda3DOnce;

int Zelda3D_Once(Zelda3DOnce* once);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CORE_RUNTIME_H
