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
#include <cstdlib> // getenv
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

// #32/#203 hotswap — keyboard-key HUD badge, composited at RUNTIME from the live binding.
//
// This used to be four PNGs baked from assets/zelda3d/key_{b,cleft,cdown,cright}.svg with the key
// LETTERS drawn into the SVGs. That is why the B-button badge still read "C" long after the default
// binding became F, and why rebinding in the input editor changed nothing on screen (kanban #203).
// Now the badge is a blank keycap plus glyphs blitted from a monospaced alphabet atlas, with the
// label coming from Zelda3D_KeyLabelForButton — so it cannot drift from the binding again.
//
// Multi-character labels ("LSHFT", "SPACE") widen the cap instead of shrinking the text into
// illegibility: the cap is 9-sliced horizontally, repeating one column of its uniform middle, which
// keeps the rounded corners and the vertical gradient intact. The text only scales down once even a
// 3x-wide cap can't hold it.
#include "../assets/key_glyphs_png.h"
#include <unordered_map>

struct KeyCapArt {
    std::vector<uint8_t> cap;    // kCapW x kCapH RGBA, blank
    std::vector<uint8_t> atlas;  // (cells * kKeyGlyphCellW) x kKeyGlyphCellH RGBA
    int atlasW = 0, atlasH = 0;
    bool ok = false;
};

constexpr int kCapW = 64;         // blank cap dims (assets/zelda3d/key_cap.svg)
constexpr int kCapH = 64;
constexpr int kCapEdge = 24;      // 9-slice: columns [0,24) and [40,64) are corners, 32 is the seam
constexpr int kCapInnerW = 46;    // usable label width on an un-widened cap
constexpr int kCapMaxW = 192;     // 3x — past this a label scales down instead of widening further
constexpr int kCapFaceCenterY = 30; // cap face spans y 4..56

static const KeyCapArt& keyCapArt() {
    static KeyCapArt art = [] {
        KeyCapArt a;
        int w = 0, h = 0, n = 0;
        if (stbi_uc* px = stbi_load_from_memory(kKeyCapPng, (int)kKeyCapPngLen, &w, &h, &n, 4)) {
            if (w == kCapW && h == kCapH) {
                a.cap.assign(px, px + (size_t)w * h * 4);
            } else {
                fprintf(stderr, "[Zelda3D] keycap: expected %dx%d, got %dx%d\n", kCapW, kCapH, w, h);
            }
            stbi_image_free(px);
        }
        if (stbi_uc* px = stbi_load_from_memory(kKeyGlyphAtlasPng, (int)kKeyGlyphAtlasPngLen, &w, &h, &n, 4)) {
            a.atlas.assign(px, px + (size_t)w * h * 4);
            a.atlasW = w;
            a.atlasH = h;
            stbi_image_free(px);
        }
        a.ok = !a.cap.empty() && !a.atlas.empty() && a.atlasH == kKeyGlyphCellH;
        if (!a.ok) {
            fprintf(stderr, "[Zelda3D] keycap art: decode failed (cap=%zu atlas=%dx%d)\n", a.cap.size(),
                    a.atlasW, a.atlasH);
        }
        return a;
    }();
    return art;
}

// Atlas cell index for a label character, or -1 when the alphabet has no such glyph.
static int glyphCell(char c) {
    for (int i = 0; kKeyGlyphChars[i] != '\0'; i++) {
        if (kKeyGlyphChars[i] == c) {
            return i;
        }
    }
    return -1;
}

// Source-over composite of one RGBA pixel onto another.
static void blendPixel(uint8_t* dst, const uint8_t* src) {
    const int sa = src[3];
    if (sa == 0) {
        return;
    }
    if (sa == 255) {
        std::memcpy(dst, src, 4);
        return;
    }
    for (int k = 0; k < 3; k++) {
        dst[k] = (uint8_t)((src[k] * sa + dst[k] * (255 - sa)) / 255);
    }
    dst[3] = (uint8_t)(sa + dst[3] * (255 - sa) / 255);
}

// Widen the blank cap to `outW` by repeating its uniform middle column (9-slice, horizontal only —
// the cap's gradient runs vertically and its top highlight is flat between the rounded corners, so
// one column reproduces the middle exactly).
static std::vector<uint8_t> stretchCap(const std::vector<uint8_t>& cap, int outW) {
    std::vector<uint8_t> out((size_t)outW * kCapH * 4, 0);
    for (int y = 0; y < kCapH; y++) {
        for (int x = 0; x < outW; x++) {
            int sx;
            if (x < kCapEdge) {
                sx = x;
            } else if (x >= outW - kCapEdge) {
                sx = kCapW - (outW - x);
            } else {
                sx = kCapW / 2; // the repeated seam column
            }
            std::memcpy(&out[((size_t)y * outW + x) * 4], &cap[((size_t)y * kCapW + sx) * 4], 4);
        }
    }
    return out;
}

const void* Zelda3D_KeyCapTex(const char* label, int* w, int* h) {
    if (label == nullptr || label[0] == '\0') {
        if (w) *w = 0;
        if (h) *h = 0;
        return nullptr;
    }
    const KeyCapArt& art = keyCapArt();
    if (!art.ok) {
        if (w) *w = 0;
        if (h) *h = 0;
        return nullptr;
    }

    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static std::unordered_map<std::string, Tex> cache;
    auto it = cache.find(label);
    if (it == cache.end()) {
        const int n = (int)std::strlen(label);
        const int textW = n * kKeyGlyphCellW;
        // Widen the cap just enough to hold the run at native glyph size, capped at kCapMaxW; only
        // then fall back to scaling the glyphs down.
        int outW = kCapW;
        if (textW > kCapInnerW) {
            outW = std::min(kCapMaxW, kCapW + (textW - kCapInnerW));
        }
        const int innerW = outW - (kCapW - kCapInnerW);
        const float scale = textW > innerW ? (float)innerW / (float)textW : 1.0f;

        Tex tex;
        tex.rgba = stretchCap(art.cap, outW);
        tex.w = outW;
        tex.hh = kCapH;

        const int drawW = (int)(textW * scale + 0.5f);
        const int drawH = (int)(kKeyGlyphCellH * scale + 0.5f);
        const int x0 = (outW - drawW) / 2;
        const int y0 = kCapFaceCenterY - drawH / 2;
        const int cellDrawW = drawW / (n > 0 ? n : 1);
        for (int i = 0; i < n; i++) {
            const int cell = glyphCell(label[i]);
            if (cell < 0) {
                continue; // sanitizer should have folded this away; skip rather than draw garbage
            }
            const int srcX0 = cell * kKeyGlyphCellW;
            for (int dy = 0; dy < drawH; dy++) {
                const int ty = y0 + dy;
                if (ty < 0 || ty >= kCapH) {
                    continue;
                }
                const int sy = dy * kKeyGlyphCellH / (drawH > 0 ? drawH : 1);
                for (int dx = 0; dx < cellDrawW; dx++) {
                    const int tx = x0 + i * cellDrawW + dx;
                    if (tx < 0 || tx >= outW) {
                        continue;
                    }
                    const int sx = srcX0 + dx * kKeyGlyphCellW / (cellDrawW > 0 ? cellDrawW : 1);
                    blendPixel(&tex.rgba[((size_t)ty * outW + tx) * 4],
                               &art.atlas[((size_t)sy * art.atlasW + sx) * 4]);
                }
            }
        }
        it = cache.emplace(label, std::move(tex)).first;
        // #21: evict any stale prior-tenant Fast3D cache entry at this brand-new buffer's address.
        Zelda3D_HudTexClaim(it->second.rgba.data());
    }
    if (w) *w = it->second.w;
    if (h) *h = it->second.hh;
    return it->second.rgba.data();
}

const char* Zelda3D_KeyCapAlphabet(void) {
    return kKeyGlyphChars;
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


// ---------------------------------------------------------------------------------------------
// #207 — the REAL OoT3D HUD sprites, cropped out of the 3DS ROM's own menu atlases instead of the
// hand-drawn embedded PNGs. The art was always there; the HUD was simply never wired to it.
//
// WHERE IT LIVES (this cost a search — hud_all00.ctxb does NOT hold these despite the name; it has
// the A/B button rings, the reticle and the localized action words):
//   /menu/<LANG>/menu_top_parts00.ctxb  512x256 — rupee gem, small key, hearts, tokens
//   /menu/<LANG>/num_all00.ctxb         256x128 — the counter digit font, four colour variants
// Layout is identical across all nine language dirs, so US English is a safe default.
//
// GRAYSCALE, DELIBERATELY. These HUD elements draw MODULATEIA_PRIM: PRIM carries the colour and the
// texture carries only shape. The rupee icon in particular is tinted by WALLET (z_parameter.c passes
// rColor), so pasting the ROM's green gem in as-is would multiply green by green and freeze the
// wallet colour. Taking luminance keeps the authentic artwork and leaves the tint behaviour exactly
// as it was. The black outline in the digit font survives this (it is opaque black, luminance 0).
static bool Zelda3dRomSprite(const char* romfsPath, int sx, int sy, int sw, int sh,
                             std::vector<uint8_t>& out, int& ow, int& oh) {
    int aw = 0, ah = 0;
    const void* atlas = Zelda3D_OoT3dAtlas(romfsPath, 0, &aw, &ah);
    if (atlas == nullptr || aw <= 0 || ah <= 0)
        return false;
    if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 || sx + sw > aw || sy + sh > ah) {
        fprintf(stderr, "[Zelda3D] ROM sprite %s (%d,%d,%d,%d) out of bounds for %dx%d atlas\n",
                romfsPath, sx, sy, sw, sh, aw, ah);
        return false;
    }
    const uint8_t* src = (const uint8_t*)atlas;
    out.assign((size_t)sw * sh * 4, 0);
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            const uint8_t* p = src + (((size_t)(sy + y) * aw) + (sx + x)) * 4;
            uint8_t* q = out.data() + (((size_t)y * sw) + x) * 4;
            // Rec.601 luminance; alpha is the sprite's own coverage.
            const int L = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
            q[0] = q[1] = q[2] = (uint8_t)L;
            q[3] = p[3];
        }
    }
    ow = sw; oh = sh;
    return true;
}

static const char* kOoT3dTopParts = "/menu/01_US_ENGLISH/menu_top_parts00.ctxb";
static const char* kOoT3dNumAll   = "/menu/01_US_ENGLISH/num_all00.ctxb";

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
        // Rects verified against the decoded atlas before wiring (scratch/hudport/rects_check.png).
        static const struct { int x, y, w, h; } kRomRect[3] = {
            { 128, 96, 18, 18 }, // rupee gem
            { 153, 96, 16, 18 }, // small key
            { 0, 0, 0, 0 },      // clock: lives in hud_all00, not ported here
        };
        for (int i = 0; i < 3; i++) {
            if (kRomRect[i].w > 0 &&
                Zelda3dRomSprite(kOoT3dTopParts, kRomRect[i].x, kRomRect[i].y, kRomRect[i].w,
                                 kRomRect[i].h, t[i].rgba, t[i].w, t[i].hh)) {
                continue;
            }
            if (kRomRect[i].w > 0) {
                // Loud, not silent: falling back to the hand-drawn stand-in is the thing #207 exists
                // to remove, so it must never look like success.
                fprintf(stderr, "[Zelda3D] counter icon %d: ROM art unavailable, using embedded PNG\n", i);
            }
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
            // num_all00's white digit row: 11x15 cells on a 12px pitch from x=1,y=63; the colon is
            // its own 6x12 cell. (The '1' glyph is drawn narrower inside its cell, which is the
            // font's own metric -- cropping the full cell keeps the digits on a common baseline.)
            const int rx = (i < 10) ? (1 + 12 * i) : 121;
            const int ry = (i < 10) ? 63 : 65;
            const int rw = (i < 10) ? 11 : 6;
            const int rh = (i < 10) ? 15 : 12;
            if (Zelda3dRomSprite(kOoT3dNumAll, rx, ry, rw, rh, t[i].rgba, t[i].w, t[i].hh))
                continue;
            fprintf(stderr, "[Zelda3D] digit tex %d: ROM art unavailable, using embedded PNG\n", i);
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

// #31 — substitute crisp higher-res HUD textures (hearts, button disc, counter icons, digits) for the
// blocky 16x16 N64 ones in the native Fast3D HUD. -1 = uninit (reads ZELDA3D_HUDTEX, default on).
// z_lifemeter.c / z_parameter.c gate their texture/load-size/texcoord swap on this.
int gZelda3dHudTex = -1;
int Zelda3D_HudTexEnabled(void) {
    if (gZelda3dHudTex < 0) {
        const char* v = getenv("ZELDA3D_HUDTEX");
        gZelda3dHudTex = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dHudTex;
}

} // extern "C"
