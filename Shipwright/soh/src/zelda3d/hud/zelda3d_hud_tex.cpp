// Zelda3D HUD texture generation — crisp runtime-built HUD textures (button glyphs, hearts, counter
// icons, digits) packed from embedded PNGs. Split out of zelda3d_model.cpp (pure move, no behavior
// change) so the model loader stays focused. Declarations live in zelda3d.h; the renderer/HUD path
// calls these by name. See #18/#21/#31/#32.
#include "../zelda3d.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <stb_image.h>
#include "asset/texpack.h"     // Zelda3D::TexPackLookup (HD texture-pack atlas source)
#include "../assets/xbox_glyphs_png.h"   // assets/zelda3d/xbox_{a,b,x,y}.svg (HUD button glyphs, #32)
#include "../assets/heart_tex_png.h"     // crisp HUD heart textures (#31)
#include "../assets/digit_tex_png.h"     // crisp HUD counter font (#31)
#include "../assets/button_tex_png.h"    // crisp HUD button-background disc (#31)
#include "../assets/counter_icon_png.h"  // crisp HUD counter icons (#31)

extern "C" {

// #18 — crop a rectangle out of a top-down RGBA32 atlas (e.g. one returned by TexPackLookup) and
// box-downsample it (area-averaging, incl. alpha-weighted RGB so transparent edge pixels don't
// bleed dark color into the silhouette) to dst x dst. Returns the dst*dst*4 RGBA buffer. The HUD
// texrect path has NO mipmaps, so a large atlas crop must be pre-shrunk to a modest size or it
// shatters when minified on-screen (the #18 digit work hit this exact aliasing).
static std::vector<uint8_t> cropAndBoxDownsample(const std::vector<uint8_t>& atlas, int aw, int ah,
                                                 int cx, int cy, int cw, int ch, int dst) {
    std::vector<uint8_t> out((size_t)dst * dst * 4, 0);
    if (cw <= 0 || ch <= 0 || dst <= 0) return out;
    for (int dy = 0; dy < dst; dy++) {
        // Source row span [sy0, sy1) for this destination row.
        int sy0 = cy + dy * ch / dst;
        int sy1 = cy + (dy + 1) * ch / dst;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int dx = 0; dx < dst; dx++) {
            int sx0 = cx + dx * cw / dst;
            int sx1 = cx + (dx + 1) * cw / dst;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            double ar = 0, ag = 0, ab = 0, aa = 0, wsum = 0, asum = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                if (sy < 0 || sy >= ah) continue;
                for (int sx = sx0; sx < sx1; sx++) {
                    if (sx < 0 || sx >= aw) continue;
                    const uint8_t* p = &atlas[((size_t)sy * aw + sx) * 4];
                    double a = p[3];
                    ar += p[0] * a; ag += p[1] * a; ab += p[2] * a; // alpha-weighted RGB
                    aa += a; asum += a; wsum += 1.0;
                }
            }
            uint8_t* d = &out[((size_t)dy * dst + dx) * 4];
            if (wsum > 0) {
                d[3] = (uint8_t)std::lround(aa / wsum);                  // mean alpha (coverage)
                double wa = asum > 0 ? asum : 1.0;
                d[0] = (uint8_t)std::lround(std::min(255.0, ar / wa));   // alpha-weighted mean RGB
                d[1] = (uint8_t)std::lround(std::min(255.0, ag / wa));
                d[2] = (uint8_t)std::lround(std::min(255.0, ab / wa));
            }
        }
    }
    return out;
}

// #18 — Xbox face-button HUD glyphs, now sourced as 3DS-style GRAY STONE buttons from the OoT3D
// texture pack (user approved 2026-06-20, overriding the #32 Xbox style). The pack's UI glyph atlas
// (hash 439913BD09FA2671, 4096x2048) carries a row of pre-composited circular gray buttons — a gray
// stone disc with the black letter already centered (A/B/X/Y, at atlas x=1165/1288/1411/1534, y=1280,
// 124px pitch). These are drawn UNTINTED (Zelda3D_DrawXboxBtn: out.rgb=TEXEL0, out.a=TEXEL0.a*PRIM.a),
// so we crop+box-downsample each disc to 64x64 full-colour RGBA and hand it over directly — no
// compositing needed (the letter is baked into the atlas). Falls back to the embedded Xbox SVG PNGs
// when the pack is absent. Same 64x64 dims as the SVG glyphs, so the HUD layout is unchanged.
// #21 FIX — port the HUD texture identity from the N64 model to PC reality.
//
// Fast3D's texture cache (libultraship interpreter.cpp) keys textures by their raw SOURCE ADDRESS
// and is invalidated MANUALLY (Gfx_TextureCacheDelete is only called by the few actors that reuse a
// texture's memory, e.g. Boss Dodongo's animated lava). That is the N64 model: a texture lives at a
// stable, engine-managed DRAM/segment address that uniquely identifies it for the session.
//
// These Zelda3D HUD textures (heart row, rupee/counter icons, button disc, digits, glyphs) are PC heap
// buffers (std::vector) decoded at runtime. Their address is NOT an engine-managed identity — malloc
// can hand us an address that a PRIOR texture occupied, was cached under, then freed WITHOUT a
// Gfx_TextureCacheDelete (most textures never call it). When that happens the very first HUD draw's
// cache lookup HITS the stale prior-tenant entry and renders its GPU texture instead of ours — garbled
// HUD that persists the whole session and clears only on restart (#21; nondeterministic because the
// heap address, and thus the collision, varies per launch).
//
// Port: a PC-allocated texture cannot trust the N64 "address == fresh identity" assumption, so when we
// first take ownership of a buffer we explicitly evict any stale cache entry left at that address. The
// buffer is allocated once and lives for the session, so a single purge at first use is sufficient and
// the next draw uploads our real pixels. (Purge can only remove a stale/wrong entry or nothing — the
// HUD buffer's own entry does not exist yet on first draw — so it never harms correct rendering.)
extern "C" void Gfx_TextureCacheDelete(const uint8_t* texAddr);
static inline void Zelda3D_HudTexClaim(const void* addr) {
    if (addr != nullptr) {
        Gfx_TextureCacheDelete((const uint8_t*)addr);
    }
}

const void* Zelda3D_XboxGlyphTex(char which, int* w, int* h) {
    struct Glyph { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Glyph g[4];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        // Pack disc crop boxes in the glyph atlas (x, y, w, h), order A,B,X,Y. Square 124px discs.
        static const int kDiscX[4] = { 1165, 1288, 1411, 1534 };
        const int discY = 1280, discW = 124, discH = 124, kDst = 64;
        std::vector<uint8_t> atlas;
        int aw = 0, ah = 0;
        bool havePack = Zelda3D::TexPackLookup(0x439913BD09FA2671ULL, aw, ah, atlas) && aw > 0 && ah > 0;
        const unsigned char* png[4] = { kXboxGlyphAPng, kXboxGlyphBPng, kXboxGlyphXPng, kXboxGlyphYPng };
        unsigned int len[4] = { kXboxGlyphAPngLen, kXboxGlyphBPngLen, kXboxGlyphXPngLen, kXboxGlyphYPngLen };
        for (int i = 0; i < 4; i++) {
            if (havePack) {
                g[i].rgba = cropAndBoxDownsample(atlas, aw, ah, kDiscX[i], discY, discW, discH, kDst);
                g[i].w = kDst; g[i].hh = kDst;
                continue;
            }
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                g[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                g[i].w = sw; g[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[Zelda3D] xbox glyph %d: PNG decode failed\n", i);
            }
        }
    }
    int idx;
    switch (which) {
        case 'A': case 'a': idx = 0; break;
        case 'B': case 'b': idx = 1; break;
        case 'X': case 'x': idx = 2; break;
        case 'Y': case 'y': idx = 3; break;
        default: if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    if (g[idx].rgba.empty()) { if (w) *w = 0; if (h) *h = 0; return nullptr; }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each glyph buffer's address
        for (int k = 0; k < 4; k++) {
            if (!g[k].rgba.empty()) Zelda3D_HudTexClaim(g[k].rgba.data());
        }
    }
    if (w) *w = g[idx].w;
    if (h) *h = g[idx].hh;
    return g[idx].rgba.data();
}

// #32 hotswap — keyboard-key HUD glyphs. One glyph per HUD slot (B, C-Left, C-Down, C-Right).
// Gray disc + white key label, matching the default keyboard bindings (C / ← / ↓ / →).
// Same 64x64 RGBA layout as Zelda3D_XboxGlyphTex — the draw path is identical, only the texture
// changes. Decoded lazily once. See also gZelda3dInputDevice / Zelda3D_InputDevice().
#include "../assets/kbd_glyphs_png.h"

const void* Zelda3D_KbdGlyphTex(char which, int* w, int* h) {
    // Slots: 0=B, 1=C-Left, 2=C-Down, 3=C-Right (mirrors Zelda3D_RecordHudBtn glyph chars)
    struct Glyph { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Glyph g[4];
    static bool tried = false;
    if (!tried) {
        tried = true;
        struct { const unsigned char* png; unsigned int len; } src[4] = {
            { kKbdGlyph_BPng,      kKbdGlyph_BPngLen      },
            { kKbdGlyph_CleftPng,  kKbdGlyph_CleftPngLen  },
            { kKbdGlyph_CdownPng,  kKbdGlyph_CdownPngLen  },
            { kKbdGlyph_CrightPng, kKbdGlyph_CrightPngLen },
        };
        for (int i = 0; i < 4; i++) {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(src[i].png, (int)src[i].len, &sw, &sh, &n, 4);
            if (px) {
                g[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                g[i].w = sw; g[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[Zelda3D] kbd glyph %d: PNG decode failed\n", i);
            }
        }
    }
    // 'B'=B slot, 'X'=C-Left, 'Y'=C-Down, 'A'=C-Right — matches the Zelda3D_RecordHudBtn calls
    int idx;
    switch (which) {
        case 'B': case 'b': idx = 0; break;
        case 'X': case 'x': idx = 1; break;
        case 'Y': case 'y': idx = 2; break;
        case 'A': case 'a': idx = 3; break;
        default: if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    if (g[idx].rgba.empty()) { if (w) *w = 0; if (h) *h = 0; return nullptr; }
    static bool reg = false;
    if (!reg) {
        reg = true;
        for (int k = 0; k < 4; k++) {
            if (!g[k].rgba.empty()) Zelda3D_HudTexClaim(g[k].rgba.data());
        }
    }
    if (w) *w = g[idx].w;
    if (h) *h = g[idx].hh;
    return g[idx].rgba.data();
}

// Hotbar number-key glyphs 1-6 (keycap style, matching the approved keycap aesthetic).
// Loaded lazily from num_glyphs_png.h; returns RGBA32 pointer + dims.
// `which` = '1'..'6'. Returns nullptr on failure.
#include "../assets/num_glyphs_png.h"

const void* Zelda3D_NumGlyphTex(char which, int* w, int* h) {
    struct Glyph { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Glyph g[6];
    static bool tried = false;
    if (!tried) {
        tried = true;
        struct { const unsigned char* png; unsigned int len; } src[6] = {
            { kNumGlyph_1Png, kNumGlyph_1PngLen },
            { kNumGlyph_2Png, kNumGlyph_2PngLen },
            { kNumGlyph_3Png, kNumGlyph_3PngLen },
            { kNumGlyph_4Png, kNumGlyph_4PngLen },
            { kNumGlyph_5Png, kNumGlyph_5PngLen },
            { kNumGlyph_6Png, kNumGlyph_6PngLen },
        };
        for (int i = 0; i < 6; i++) {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(src[i].png, (int)src[i].len, &sw, &sh, &n, 4);
            if (px) {
                g[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                g[i].w = sw; g[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[Zelda3D] num glyph %d: PNG decode failed\n", i + 1);
            }
        }
        // Register with HudTexClaim so the GC doesn't free them.
        for (int k = 0; k < 6; k++) {
            if (!g[k].rgba.empty()) Zelda3D_HudTexClaim(g[k].rgba.data());
        }
    }
    int idx = (int)(which - '1');
    if (idx < 0 || idx > 5) { if (w) *w = 0; if (h) *h = 0; return nullptr; }
    if (g[idx].rgba.empty()) { if (w) *w = 0; if (h) *h = 0; return nullptr; }
    if (w) *w = g[idx].w;
    if (h) *h = g[idx].hh;
    return g[idx].rgba.data();
}

// #18 — derive the FULL / EMPTY HUD heart from the OoT3D item atlas (user approved 2026-06-20).
// The clean red heart icon lives in the pack item atlas (hash CF461E58E637A97A, 4096x4096) at
// x=2018..2338, y=3020..3352 (a red heart with a light rim). The lifemeter combine is
// out=(PRIM-ENV)*TEXEL0+ENV on rgb and reads TEXEL0.rgb as the PRIM<->ENV lerp factor, alpha as the
// silhouette (see z_lifemeter.c HealthMeter_Draw). So the heart's rgb must be an INTENSITY (bright
// body -> tints to PRIM red, dark -> ENV). The saturated-red core has low luminance but high VALUE,
// so we use value = max(r,g,b) as the intensity (luminance would make the red body dark -> wrong);
// alpha stays the silhouette. EMPTY uses the same silhouette with rgb pinned low (0x20, the SVG
// empty-heart level) so it lerps toward ENV (dark). Both box-downsampled to 64x64 (same as the SVG
// hearts) so they align in the row and render crisp under HUD minification.
static void heartPackVariant(const std::vector<uint8_t>& atlas, int aw, int ah, bool empty,
                             std::vector<uint8_t>& outRgba, int& ow, int& oh) {
    const int hx = 2018, hy = 3020, hw = 320, hh = 332, kDst = 64;
    std::vector<uint8_t> crop = cropAndBoxDownsample(atlas, aw, ah, hx, hy, hw, hh, kDst);
    for (size_t i = 0; i < (size_t)kDst * kDst; i++) {
        uint8_t* p = &crop[i * 4];
        if (empty) {
            p[0] = p[1] = p[2] = 0x20; // dark -> lerps toward ENV, matching the SVG empty heart
        } else {
            uint8_t v = std::max(p[0], std::max(p[1], p[2])); // value = bright body & rim highlight
            p[0] = p[1] = p[2] = v;
        }
    }
    outRgba.swap(crop);
    ow = kDst; oh = kDst;
}

// #31/#18 — crisp higher-res HUD heart textures. Kinds 0 (full) and 4 (empty) are derived from the
// OoT3D item-atlas heart in the texture pack (grayscale-value rgb=intensity, a=silhouette; see
// heartPackVariant); kinds 1/2/3 (3-4, 1/2, 1-4) stay the embedded SVG hearts (the pack has no
// fractional hearts). Falls back entirely to the embedded SVG PNGs when the pack is absent. `kind`
// is ZELDA3D_HEART_* (0..4). Returns the buffer + dims, or NULL on failure. The N64 heart combine
// reads TEXEL0.rgb as the PRIM<->ENV lerp factor, so the grayscale heart tints exactly like IA8.
const void* Zelda3D_HeartTex(int kind, int* w, int* h) {
    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Tex t[5];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        const unsigned char* png[5] = { kHeartFullPng, kHeartThreeQuarterPng, kHeartHalfPng,
                                        kHeartQuarterPng, kHeartEmptyPng };
        unsigned int len[5] = { kHeartFullPngLen, kHeartThreeQuarterPngLen, kHeartHalfPngLen,
                                kHeartQuarterPngLen, kHeartEmptyPngLen };
        std::vector<uint8_t> atlas;
        int aw = 0, ah = 0;
        bool havePack = Zelda3D::TexPackLookup(0xCF461E58E637A97AULL, aw, ah, atlas) && aw > 0 && ah > 0;
        for (int i = 0; i < 5; i++) {
            if (havePack && (i == 0 || i == 4)) {
                heartPackVariant(atlas, aw, ah, /*empty=*/i == 4, t[i].rgba, t[i].w, t[i].hh);
                continue;
            }
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                t[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                t[i].w = sw; t[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[Zelda3D] heart tex %d: PNG decode failed\n", i);
            }
        }
    }
    if (kind < 0 || kind >= 5 || t[kind].rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each heart buffer's address
        for (int k = 0; k < 5; k++) {
            if (!t[k].rgba.empty()) Zelda3D_HudTexClaim(t[k].rgba.data());
        }
    }
    if (w) *w = t[kind].w;
    if (h) *h = t[kind].hh;
    return t[kind].rgba.data();
}

// Zelda3D PC HUD — colour-baked heart texture for the native Vulkan HUD path (zelda3d_hud_vk.cpp).
// Zelda3D_HeartTex returns a GRAYSCALE intensity map (rgb = the PRIM<->ENV lerp factor, a = the heart
// silhouette) meant to be tinted by the N64 Fast3D combiner. The PC HUD draws straight RGBA with no
// combiner, so we bake the same lerp here: out.rgb = ENV + (PRIM-ENV) * intensity, out.a = silhouette.
// Colours are OoT's life-meter PRIM/ENV (z64interface.h: HEARTS_PRIM = {255,70,50}, HEARTS_ENV =
// {50,40,60}), kept in sync with z_lifemeter.c's normal (non-DD) heart draw. Cached per kind.
const void* Zelda3D_HudHeartRGBA(int kind, int* w, int* h) {
    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Tex t[5];
    static int tried = 0;
    // OoT life-meter normal-heart colours (z64interface.h HEARTS_PRIM_* / HEARTS_ENV_*).
    const int PRIM[3] = { 255, 70, 50 };
    const int ENV[3] = { 50, 40, 60 };
    if (!tried) {
        tried = 1;
        for (int k = 0; k < 5; k++) {
            int gw = 0, gh = 0;
            const uint8_t* gray = (const uint8_t*)Zelda3D_HeartTex(k, &gw, &gh);
            if (!gray || gw <= 0 || gh <= 0) {
                continue;
            }
            t[k].rgba.resize((size_t)gw * gh * 4);
            for (size_t i = 0; i < (size_t)gw * gh; i++) {
                int inten = gray[i * 4]; // rgb are equal (intensity); take r
                int a = gray[i * 4 + 3]; // silhouette
                for (int c = 0; c < 3; c++) {
                    t[k].rgba[i * 4 + c] = (uint8_t)(ENV[c] + (PRIM[c] - ENV[c]) * inten / 255);
                }
                t[k].rgba[i * 4 + 3] = (uint8_t)a;
            }
            t[k].w = gw;
            t[k].hh = gh;
        }
    }
    if (kind < 0 || kind >= 5 || t[kind].rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    if (w) *w = t[kind].w;
    if (h) *h = t[kind].hh;
    return t[kind].rgba.data();
}

// #31 — crisp HUD button-background disc (round beveled circle behind the B / C / A buttons).
// Decode the embedded PNG once into persistent RGBA32 (grayscale, a=coverage). Returns the buffer
// + dims, or NULL on failure. The button combine is G_CC_MODULATEIA_PRIM, so the grayscale disc
// tints to each button's PRIM colour exactly like the original 32x32 IA8 gButtonBackgroundTex.
const void* Zelda3D_ButtonBgTex(int* w, int* h) {
    static std::vector<uint8_t> rgba;
    static int bw = 0, bh = 0;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        // #18 — prefer the OoT3D texture pack's HD button-bg disc (filename hash 08F40E3D6D548398),
        // a grayscale white beveled disc on transparency that tints via MODULATEIA_PRIM exactly like
        // the SVG (white center -> PRIM colour, dark rim stays dark). Loaded at runtime from the
        // gitignored pack (never embedded/committed — it is a game asset). Falls back to the embedded
        // SVG disc when the pack isn't present. (Pack returns top-down RGBA32; this pack is flip=0.)
        std::vector<uint8_t> pk;
        int pw = 0, ph = 0;
        if (Zelda3D::TexPackLookup(0x08F40E3D6D548398ULL, pw, ph, pk) && pw > 0 && ph > 0) {
            rgba.swap(pk);
            bw = pw; bh = ph;
        } else {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(kButtonBgPng, (int)kButtonBgPngLen, &sw, &sh, &n, 4);
            if (px) {
                rgba.assign(px, px + (size_t)sw * sh * 4);
                bw = sw; bh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[Zelda3D] button bg tex: PNG decode failed\n");
            }
        }
    }
    if (rgba.empty()) { if (w) *w = 0; if (h) *h = 0; return nullptr; }
    static bool reg = false;
    if (!reg) { reg = true; Zelda3D_HudTexClaim(rgba.data()); } // #21: evict stale cache entry at this addr
    if (w) *w = bw;
    if (h) *h = bh;
    return rgba.data();
}

// #31 — crisp HUD counter icons (0=rupee gem, 1=small key, 2=clock). Decode the embedded PNGs once
// into persistent RGBA32 (grayscale, a=coverage). Returns the buffer + dims, or NULL on failure.
// Rupee/key draw MODULATEIA_PRIM (PRIM tints the grayscale facet shading); the clock draws
// MODULATERGBA_PRIM with PRIM white (grayscale shown directly). All three are 16x16 IA8 in N64.
const void* Zelda3D_CounterIconTex(int kind, int* w, int* h) {
    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Tex t[3];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        const unsigned char* png[3] = { kRupeeIconPng, kSmallKeyIconPng, kClockIconPng };
        unsigned int len[3] = { kRupeeIconPngLen, kSmallKeyIconPngLen, kClockIconPngLen };
        for (int i = 0; i < 3; i++) {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                t[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                t[i].w = sw; t[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[Zelda3D] counter icon %d: PNG decode failed\n", i);
            }
        }
    }
    if (kind < 0 || kind >= 3 || t[kind].rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each counter-icon buffer's address
        for (int k = 0; k < 3; k++) {
            if (!t[k].rgba.empty()) Zelda3D_HudTexClaim(t[k].rgba.data());
        }
    }
    if (w) *w = t[kind].w;
    if (h) *h = t[kind].hh;
    return t[kind].rgba.data();
}

// #31 — crisp HUD counter font (0..9 = digit, 10 = ':'). Decode the embedded PNGs once into
// persistent RGBA32 (grayscale, a=coverage). Returns the buffer + dims, or NULL on failure.
const void* Zelda3D_DigitTex(int glyph, int* w, int* h) {
    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Tex t[11];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        const unsigned char* png[11] = { kDigit0Png, kDigit1Png, kDigit2Png, kDigit3Png, kDigit4Png,
                                         kDigit5Png, kDigit6Png, kDigit7Png, kDigit8Png, kDigit9Png,
                                         kDigitColonPng };
        unsigned int len[11] = { kDigit0PngLen, kDigit1PngLen, kDigit2PngLen, kDigit3PngLen, kDigit4PngLen,
                                 kDigit5PngLen, kDigit6PngLen, kDigit7PngLen, kDigit8PngLen, kDigit9PngLen,
                                 kDigitColonPngLen };
        for (int i = 0; i < 11; i++) {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                t[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                t[i].w = sw; t[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[Zelda3D] digit tex %d: PNG decode failed\n", i);
            }
        }
    }
    if (glyph < 0 || glyph >= 11 || t[glyph].rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each digit buffer's address
        for (int k = 0; k < 11; k++) {
            if (!t[k].rgba.empty()) Zelda3D_HudTexClaim(t[k].rgba.data());
        }
    }
    if (w) *w = t[glyph].w;
    if (h) *h = t[glyph].hh;
    return t[glyph].rgba.data();
}

} // extern "C"
