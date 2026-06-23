// SoH3D PC HUD — a self-contained, immediate-mode 2D textured-quad renderer for the in-game HUD,
// drawn DIRECTLY through the Fast3D Vulkan backend (no N64 Fast3D path, no RmlUi).
//
// The user directive (2026-06-23): the in-game HUD must be a MODERN PC HUD rendered natively via
// Vulkan using the real 3DS/HD textures — not the N64 Interface_Draw path and not RmlUi CSS. This
// module is that native graphics layer: it owns a tiny Vulkan pipeline (premultiplied… no: straight
// alpha-blended textured quads, no depth) and records its quads into the backend's current command
// buffer + render pass via GfxRenderingAPIVulkan::BeginSoH3DPass — the same hook the SoH3D model
// pass and the RmlUi menu use, so the HUD composites on top of the game frame.
//
// The HUD *layout* (which texture goes where, sized from gSaveContext) lives soh-side in
// soh3d.c (SoH3D_HudFrame), which calls this C-ABI each frame: Begin → (Tex + Draw)* → End.
// Texture sources (hearts/items/digits/glyphs) are the existing SoH3D_*Tex accessors; this layer
// just uploads their RGBA32 once (cached by buffer pointer) and blits them as quads.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1 if the HUD VK layer can render this frame (Vulkan backend live + resources ready). soh-side
// gates the native HUD off only when this is true, so a GL build keeps the native HUD as fallback.
int SoH3D_Hud_Available(void);

// Open the HUD pass for this frame. Returns 1 and writes the framebuffer pixel size into
// *outW/*outH on success; 0 if the pass could not be opened (caller should skip HUD drawing).
int SoH3D_Hud_Begin(int* outW, int* outH);

// Upload an RGBA32 (w*h*4 bytes, top-down, straight alpha) texture, cached by `key` (a stable
// pointer — typically the RGBA buffer itself). Returns an opaque texture id (>=1), or 0 on failure.
// Re-uploads are skipped: the same key returns the cached id without touching `rgba`.
int SoH3D_Hud_Tex(const void* key, const void* rgba, int w, int h);

// Draw one textured quad in framebuffer pixel space (origin top-left, y down). (x,y) is the
// top-left corner, (w,h) the size. (u0,v0)-(u1,v1) is the source UV rectangle (0..1; pass 0,0,1,1
// for the whole texture). tintRGBA = 0xRRGGBBAA straight-alpha colour multiplied onto the texel
// (0xFFFFFFFF = untinted). Pass tex=0 to draw a solid tinted rectangle (1x1 white texture).
void SoH3D_Hud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                    unsigned int tintRGBA);

// Finish the HUD pass for this frame.
void SoH3D_Hud_End(void);

// Release all Vulkan resources (textures, pipeline, per-frame rings). Optional — process exit
// frees them anyway; provided for an orderly teardown path.
void SoH3D_Hud_Shutdown(void);

#ifdef __cplusplus
}
#endif
