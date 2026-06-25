// SoH3D PC HUD — SDL3 GPU implementation (unified op model). See soh3d_hud_sdl3gpu.cpp.
//
// The public C-ABI (SoH3D_Hud_*) lives in soh3d_hud_vk.cpp; it delegates to these Fast:: functions
// when the SDL3 GPU backend is the live one. Internal to libultraship.
#pragma once
#ifdef ENABLE_SDL3GPU

namespace Fast {
int SgHud_Available();
int SgHud_Begin(int* outW, int* outH);
int SgHud_Tex(const void* key, const void* rgba, int w, int h);
void SgHud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                unsigned int tintRGBA);
void SgHud_End();
} // namespace Fast

#endif
