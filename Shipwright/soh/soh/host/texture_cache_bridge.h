#pragma once

#include <stdint.h>

#ifdef __cplusplus
void Zelda3D_InitializeAltAssets();
void Zelda3D_RefreshAltAssets();

extern "C" {
#endif

void Gfx_RegisterBlendedTexture(const char* name, uint8_t* mask, uint8_t* replacement);
void Gfx_UnregisterBlendedTexture(const char* name);
void Gfx_TextureCacheDelete(const uint8_t* textureAddress);

#ifdef __cplusplus
}
#endif
