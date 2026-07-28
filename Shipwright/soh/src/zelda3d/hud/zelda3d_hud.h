// Zelda3D native HUD — the seam that takes the in-game HUD OFF the Fast3D interpreter.
//
// USER DIRECTIVE (kanban #205, 2026-07-28): the HUD must not be driven by the interpreter. It was
// emitted as N64 display lists (`gDPLoadTextureBlock` + `gSPWideTextureRectangle` on OVERLAY_DISP)
// and rendered by the emulated N64 graphics pipeline — which is also where the reported black-bars
// corruption on the item-button discs comes from. The instruction was explicitly NOT to patch that
// corruption but to change the architecture, so the HUD is a real UI layer instead.
//
// SHAPE — record here, draw natively later. The N64 HUD code KEEPS its layout: every Interface_*
// function still computes its rect in the same 320x240-based, widescreen-extended virtual space
// (`OTRGetDimensionFrom{Left,Right}Edge`), still honours the interfaceCtx fade alphas, the cosmetic
// CVars and the visibility rules. Only the final "emit a texrect" step changes: instead of appending
// to the display list, the site calls Zelda3D_HudQuad*, which records the resolved quad. Gui::EndFrame
// then calls Zelda3D_HudFrame(), which maps those rects to framebuffer pixels and draws them through
// the SDL3-GPU HUD quad renderer (libultraship `Zelda3D_Hud_*`), as ordinary ops in the one unified
// render pass.
//
// Re-deriving the layout in a new module was the other option and is what the DELETED custom HUD did
// (#202) — it is also why that HUD was rejected: a redesigned item bar that suppressed the native
// C-button cluster and clobbered `gSaveContext.equips.buttonItems`. Keeping the engine's layout and
// moving only the draw avoids repeating that, and cannot silently lose a HUD feature.
//
// ORDERING CONSEQUENCE, and why elements convert as a GROUP: the native pass runs after the whole
// interpreter frame, so anything recorded here lands on top of everything the interpreter drew. An
// element must therefore be converted with its whole stack (background, icon, counter, badge) or the
// layering inverts. That is what Zelda3D_HudOwns() gates.
#ifndef ZELDA3D_HUD_ZELDA3D_HUD_H
#define ZELDA3D_HUD_ZELDA3D_HUD_H

#ifdef __cplusplus
extern "C" {
#endif

// Elements the native HUD has taken over. A site whose element returns 1 must NOT emit its display
// list; a site whose element returns 0 is still drawn by the interpreter and must be left alone.
enum {
    // B / C-Left / C-Down / C-Right / D-pad: the button disc, the item icon, the ammo count and the
    // key-or-gamepad badge. This is the cluster the user photographed.
    ZELDA3D_HUD_ITEM_BUTTONS = 0,
};
int Zelda3D_HudOwns(int element);

// Record one HUD quad. `x`,`y`,`w`,`h` are in the N64 HUD virtual space the caller already works in
// (Y down, height 240, X extended for widescreen — exactly what OTRGetDimensionFrom*Edge returns).
// `tex` must be a persistent RGBA32 buffer; its address doubles as the upload cache key, so pass the
// same pointer each frame. `primRGBA` is the N64 PRIM colour, alpha included (the interfaceCtx fade).
//
// The `Uv` variant takes a sub-rect of an atlas texture, in atlas pixels.
void Zelda3D_HudQuad(const void* tex, int texW, int texH, float x, float y, float w, float h,
                     unsigned int primRGBA);
void Zelda3D_HudQuadUv(const void* tex, int texW, int texH, int sx, int sy, int sw, int sh, float x,
                       float y, float w, float h, unsigned int primRGBA);
// PRIM/ENV lerp variant — the N64 `(PRIM - ENV) * TEXEL0 + ENV` combine that z_lifemeter.c's hearts
// use. `tex` supplies the lerp factor in .r and the silhouette in .a.
void Zelda3D_HudQuadLerp(const void* tex, int texW, int texH, float x, float y, float w, float h,
                         unsigned int primRGBA, unsigned int envRGB);

// Draws (and clears) everything recorded this frame. Called from libultraship's Gui::EndFrame, i.e.
// after the interpreter has composited the game frame and before the RmlUi menu, so the HUD sits
// over the world and under an open ESC menu.
void Zelda3D_HudFrame(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_HUD_ZELDA3D_HUD_H
