// Model-provider registration for the native Zelda3D backend.

#include "fast/zelda3d_model_provider.h"

#ifdef ENABLE_SDL3GPU
#include "fast/zelda3d_sdl3gpu.h"
#endif

extern "C" void Zelda3D_GL_SetModelProvider(Zelda3DModelProvider provider) {
#ifdef ENABLE_SDL3GPU
    Zelda3D_Sg_SetProvider(provider);
#else
    (void)provider;
#endif
}
