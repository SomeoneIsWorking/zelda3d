/**
 * The port ABI both games implement.
 *
 * These 29 functions are the surface the N64 decomp calls when it needs something only the PC port
 * can answer -- the real framebuffer size, the window's aspect ratio, where a HUD rectangle lands
 * after widescreen adjustment, the audio buffer, the frame pump. Ocarina of Time implements them in
 * soh/OTRGlobals.cpp and Majora's Mask in 2s2h/BenPort.cpp, and until this header existed each game
 * also DECLARED them itself, in its own port-shell header. Measured before extracting: of the 29
 * names both shells declared, 28 signatures were byte-identical and the 29th
 * (Gfx_TextureCacheDelete) differed only in a parameter NAME. So the interface was already shared in
 * fact; it just had two copies and nothing checking they agreed.
 *
 * Having one copy means the two games' DECLARATIONS can no longer drift apart. That alone would not
 * catch a definition drifting, though: this block is C-only (see below) while the functions are
 * DEFINED in C++ (`extern "C" uint32_t OTRGetGameRenderWidth() {` in OTRGlobals.cpp / BenPort.cpp),
 * so the defining translation unit would never see a prototype. Each shell's .cpp therefore includes
 * this header again under `extern "C"`, which is what makes a drifting definition a build error.
 *
 * That check was verified to FIRE, not just to exist: changing the return type here to uint64_t
 * fails the build with "conflicting declaration of C function 'uint32_t OTRGetGameRenderWidth()'"
 * at the definition site. If you extend this header, keep that property -- a check nobody has seen
 * fail is not known to work.
 *
 * WHY THIS IS NOT SELF-CONTAINED, and must not be made so. It names Gfx, s16, u32 and the other
 * ultra types, and those are DIFFERENT types in the two decomps -- the same situation as gu_pc.c
 * (claim C064). So it is included from inside each game's port-shell header, after that game's types
 * exist, and shared port code under port/ gets it the same way. Do not add includes here to make it
 * stand alone; there is no single definition of Gfx to include.
 *
 * WHY THERE IS NO extern "C" HERE. Both shells declared this block inside `#ifndef __cplusplus`, so
 * it has always been C-visible only; C++ callers use the shells' C++ API instead. This header is
 * included inside that same guard in both games, which keeps the visibility exactly as it was. That
 * is deliberate -- exposing these to C++ would risk colliding with the C++ declarations the shells
 * already carry, and this extraction is meant to change structure, not linkage.
 *
 * Anything NOT common to both games stays in that game's own port-shell header. This file is only
 * for what both actually implement.
 */

#ifndef ZELDA3D_PORT_API_H
#define ZELDA3D_PORT_API_H

/* Lifecycle ------------------------------------------------------------------------------------ */
/* Returns 0 on success, non-zero when the core cannot boot. A core must never exit() the process --
 * the launcher is still holding its dlopen handle and the chooser. See port/core_boot_error.h. */
int InitOTR(int argc, char* argv[]);
void DeinitOTR(void);
void OTRMessage_Init();

/* Frame pump ----------------------------------------------------------------------------------- */
void Graph_StartFrame();
void Graph_ProcessGfxCommands(Gfx* commands);
uint32_t Ship_GetInterpolationFrameCount();

/* Viewport, aspect ratio and HUD placement ------------------------------------------------------ */
float OTRGetAspectRatio(void);
uint32_t OTRGetGameRenderWidth();
uint32_t OTRGetGameRenderHeight();
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
int16_t OTRGetRectDimensionFromLeftEdge(float v);
int16_t OTRGetRectDimensionFromRightEdge(float v);

/* Framebuffer readback --------------------------------------------------------------------------*/
void OTRGetPixelDepthPrepare(float x, float y);
uint16_t OTRGetPixelDepth(float x, float y);

/* Textures --------------------------------------------------------------------------------------*/
void Gfx_RegisterBlendedTexture(const char* name, u8* mask, u8* replacement);
void Gfx_UnregisterBlendedTexture(const char* name);
void Gfx_TextureCacheDelete(const uint8_t* texAddr);

/* Audio -----------------------------------------------------------------------------------------*/
void AudioMgr_CreateNextAudioBuffer(s16* samples, u32 num_samples);
int AudioPlayer_GetDesiredBuffered(void);
void AudioPlayer_Play(const uint8_t* buf, uint32_t len);

/* Save files ------------------------------------------------------------------------------------*/
void Ctx_ReadSaveFile(uintptr_t addr, void* dramAddr, size_t size);
void Ctx_WriteSaveFile(uintptr_t addr, void* dramAddr, size_t size);

/* Input -----------------------------------------------------------------------------------------*/
int Controller_ShouldRumble(size_t slot);

/* Time ------------------------------------------------------------------------------------------*/
uint64_t GetPerfCounter();
uint64_t osGetTime(void);
uint32_t osGetCount(void);

/* Misc ------------------------------------------------------------------------------------------*/
void OTRGfxPrint(const char* str, void* printer, void (*printImpl)(void*, char));
void Messagebox_ShowErrorBox(char* title, char* body);

#endif /* ZELDA3D_PORT_API_H */
