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
// ORDERING, and why elements currently convert as a GROUP: anything recorded here lands on top of
// everything the interpreter drew, so an element must be converted with its whole stack (background,
// icon, counter, badge) or the layering inverts. That is what Zelda3D_HudOwns() gates.
//
// The rule is liftable, but NOT the obvious way. The quads do go into the SDL3-GPU backend's SAME
// deferred op list as the N64 triangles, and that list replays in order — but flushing them earlier
// does not interleave them, it buries them: everything here is recorded while the DISPLAY LIST IS
// BEING BUILT, and the interpreter does not execute (and so does not append its own ops) until
// Graph_ProcessGfxCommands runs afterwards. A record-time flush would put the whole HUD UNDER the
// world.
//
// Interleaving needs a custom Gfx OPCODE marker — the mechanism this codebase already uses for CMB
// model draws (OTR_G_ZELDA3D_DRAW, lus_gbi.h): the call site emits a marker where it would have
// drawn, and the interpreter flushes the pending HUD batch when it reaches it. See docs/issues/0005-*;
// that is what the minimap needs, because its compass arrows are 3D meshes no quad can honestly
// represent.
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
    // The do-action prompt: the A-button disc and its label ("Open" / "Speak" / "Put Away"). One
    // group because they share the flip animation and the label draws over the disc. This is what
    // the black-bar stack was (docs/issues/0004-*): unlike every other element these are not
    // texrects but flip-animated quads whose texcoords are baked for a 32-texel tile, and the HD
    // disc rescales those baked coords by discW/32 — a ratio the N64 tile cannot express, so the
    // row stride breaks into horizontal bands.
    ZELDA3D_HUD_DO_ACTION = 1,
    // The heart row. Also ortho-matrix quads rather than texrects, and the one element that needs
    // the PRIM/ENV lerp combine (Zelda3D_HudQuadLerp) — that is what gives a heart its body/rim
    // shading and its beating / low-health / double-defense colour sets.
    ZELDA3D_HUD_HEALTH = 2,
    // The magic meter: its IA8 frame (left cap, tiled middle, mirrored right cap) and its I4 fill.
    // The tiled middle is the reason the quad renderer grew a repeat sampler.
    ZELDA3D_HUD_MAGIC = 3,
    // The rupee and small-key counters: their icon and their digits.
    ZELDA3D_HUD_COUNTERS = 4,
    // The C-Up button and its "Navi" label (drawn together — the label overlaps the disc).
    ZELDA3D_HUD_C_UP = 5,
    // The event timer: clock icon + digits. And the Horseback-Archery score digits.
    ZELDA3D_HUD_TIMERS = 6,
    // The minimap IMAGE only. Its compass/position arrows are 3D meshes (gCompassArrowDL under a
    // scale + RotateX + RotateY) that no quad can honestly represent, so they stay on the
    // interpreter — which works because the map emits a gSPZelda3DHudFlush marker straight after
    // itself, compositing the map quad at that point so everything drawn later still lands on top.
    ZELDA3D_HUD_MINIMAP = 7,
    // Passed by a Gfx_Texture* call site that has NOT been converted yet, so the shared helper keeps
    // emitting its display list for that caller while other callers of the same helper go native.
    ZELDA3D_HUD_NONE = -1,
};
int Zelda3D_HudOwns(int element);

// Which N64 combine a quad reproduces.
enum {
    // TEXEL0 * PRIM. Item icons, digits, glyphs, keycaps — and the MODULATEIA_PRIM cases (button
    // disc, counter icons, magic-bar frame), since an intensity-in-rgb texture modulates the same.
    ZELDA3D_HUD_MODULATE = 0,
    // (PRIM - ENV) * TEXEL0 + ENV on rgb, TEXEL0.a * PRIM.a on alpha. z_lifemeter.c's hearts.
    ZELDA3D_HUD_LERP = 1,
    // As LERP but alpha = PRIM.a, ignoring the texel. The magic-bar fill assigns PRIMITIVE straight
    // to alpha, so its intensity must not double as coverage.
    ZELDA3D_HUD_LERP_OPAQUE = 2,
};

// Record one HUD quad. `x`,`y`,`w`,`h` are in the N64 HUD virtual space the caller already works in
// (Y down, height 240, X extended for widescreen — exactly what OTRGetDimensionFrom*Edge returns).
// `tex` must be a persistent RGBA32 buffer; its address doubles as the upload cache key, so pass the
// same pointer each frame. `primRGBA` is the N64 PRIM colour, alpha included (the interfaceCtx fade).
void Zelda3D_HudQuad(const void* tex, int texW, int texH, float x, float y, float w, float h, unsigned int primRGBA);

// The full form: a source sub-rect in texture pixels plus the combine.
//
// The source rect is what makes an N64 texrect translate faithfully. A texrect with dsdx/dtdy of
// 1:1 samples ONE TEXEL PER PIXEL rather than stretching its texture over the rect — so the magic
// bar's 7-pixel-tall fill takes the first 7 rows of a 16-row texture, and its middle section repeats
// a 24px strip across the whole bar. Pass the rect the N64 call actually samples; a source rect
// larger than the texture TILES it (the renderer switches to a repeat sampler when UVs leave [0,1]),
// and sw or sh may be negative to mirror, which is how the bar's right cap is drawn.
void Zelda3D_HudQuadEx(const void* tex, int texW, int texH, int sx, int sy, int sw, int sh, float x, float y, float w,
                       float h, unsigned int primRGBA, unsigned int envRGB, int mode);

// Decode an N64-format HUD texture to a persistent RGBA32 buffer the Quad* calls can draw, or NULL
// if it cannot be decoded. Most HUD sources are neither RGBA32 nor one of our own runtime buffers:
// the do-action label is IA4, the magic-bar frame IA8, its fill I4. OTR path strings are resolved
// first, so call sites can pass the engine's symbol directly.
//
// Decodes are cached by CONTENT HASH, not by pointer. Several of these buffers are rewritten in
// place — the do-action label changes from "Open" to "Speak" at the same address — so a
// pointer-keyed cache would pin whichever content was decoded first, and the GPU-side upload cache
// (which keys on the returned buffer's address) would go on reusing a stale upload.
enum {
    ZELDA3D_HUD_FMT_IA4 = 0, // 3 bits intensity + 1 bit alpha
    ZELDA3D_HUD_FMT_IA8 = 1, // 4 bits intensity + 4 bits alpha
    ZELDA3D_HUD_FMT_I4 = 2,  // 4 bits intensity, opaque (see below)
    ZELDA3D_HUD_FMT_I8 = 3,  // 8 bits intensity, opaque
};
// I-format textures decode with alpha 255 rather than alpha = intensity. On N64 an I texel feeds
// both channels, but every I-format HUD combiner here takes its alpha from PRIMITIVE instead, so
// carrying intensity into alpha would double-apply it as coverage. Anything that genuinely wants
// intensity-as-alpha uses an IA format.
const void* Zelda3D_HudDecode(int fmt, const void* tex, int texW, int texH);

// Force the native HUD on (1) or off (0) at runtime — REPL `nativehud`. With it off every converted
// site falls back to its display-list emission, which makes a same-frame A/B of the native and
// interpreter renders possible; a colour mask over two separately-captured frames cannot do that,
// because the scene behind the HUD moves between them. Defaults to on (env ZELDA3D_HUD=0 disables).
void Zelda3D_HudSetEnabled(int on);
int Zelda3D_HudGetEnabled(void);

// Called by the interpreter at a gSPZelda3DHudFlush marker: composites the quads recorded up to that
// point, so an element converts ALONE and anything drawn after it still lands on top.
void Zelda3D_HudFlushPoint(void);

// Draws (and clears) everything recorded this frame. Called from libultraship's Gui::EndFrame, i.e.
// after the interpreter has composited the game frame and before the RmlUi menu, so the HUD sits
// over the world and under an open ESC menu.
void Zelda3D_HudFrame(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_HUD_ZELDA3D_HUD_H
