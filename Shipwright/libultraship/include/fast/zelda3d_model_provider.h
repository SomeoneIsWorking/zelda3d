// On-demand model-data provider contract for native Zelda3D renderers.
#ifndef ZELDA3D_FAST_MODEL_PROVIDER_H
#define ZELDA3D_FAST_MODEL_PROVIDER_H

#include "zelda3d_model_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*Zelda3DModelProvider)(int modelId, const Zelda3DGlGroup** groups, int* groupCount,
                                    const Zelda3DGlTex** texs, int* texCount);

void Zelda3D_GL_SetModelProvider(Zelda3DModelProvider provider);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_FAST_MODEL_PROVIDER_H
