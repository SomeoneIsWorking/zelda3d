// See zelda3d_hud.h for the design and the user directive behind it (#205).
#include "zelda3d_hud.h"

#include "../zelda3d.h"
#include "../behaviors/title/title_presentation.h" // Zelda3D_Title_IsActive — no HUD over the title demo

#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>

// The SDL3-GPU HUD quad renderer (libultraship/src/fast/zelda3d_hud_sdl3gpu.cpp). Declared here
// rather than included so the soh-side HUD keeps depending on libultraship only through a C ABI,
// the same one-directional layering the rest of the zelda3d layer uses.
extern "C" {
int Zelda3D_Hud_Available(void);
int Zelda3D_Hud_Begin(int* outW, int* outH);
int Zelda3D_Hud_Tex(const void* key, const void* rgba, int w, int h);
void Zelda3D_Hud_DrawEnv(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                         unsigned int tintRGBA, unsigned int envRGB, int mode);
void Zelda3D_Hud_End(void);

// SoH stores most HUD "textures" as OTR PATH STRINGS ("__OTR__textures/icon_item_static/...") and the
// Fast3D interpreter resolves them to pixels when it executes gDPLoadTextureBlock. A native HUD gets
// no such step, so it has to resolve them itself — this is why the first native item-icon draw came
// out blank: the recorded pointer was the path, not the image.
int ResourceMgr_OTRSigCheck(char* imgData);
char* ResourceMgr_GetResourceDataByNameHandlingMQ(const char* path);
}

namespace {

// One recorded quad, still in HUD virtual coordinates — the pixel mapping needs the framebuffer
// size, which is only known at draw time (Zelda3D_Hud_Begin).
struct HudQuad {
    const void* tex;
    int texW, texH;
    float u0, v0, u1, v1;
    float x, y, w, h;
    unsigned int prim;
    unsigned int env;
    int mode;
};

std::vector<HudQuad> sQuads;

// -1 = uninitialised. Off when the quad renderer is unavailable (then the interpreter HUD must stay,
// so Zelda3D_HudOwns() reports 0 and every converted site falls back to its display-list emission).
int sEnabled = -1;

bool hudEnabled() {
    if (sEnabled < 0) {
        const char* v = getenv("ZELDA3D_HUD");
        sEnabled = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    return sEnabled != 0 && Zelda3D_Hud_Available() != 0;
}

// Resolve an OTR path to its loaded pixel data; pass through anything that is already a raw buffer
// (our own runtime-built HUD textures). The resource manager caches, so the resolved address is
// stable across frames and remains a valid upload-cache key.
const void* resolveTex(const void* tex) {
    if (tex != nullptr && ResourceMgr_OTRSigCheck((char*)tex)) {
        return ResourceMgr_GetResourceDataByNameHandlingMQ((const char*)tex);
    }
    return tex;
}

void record(const void* tex, int texW, int texH, float u0, float v0, float u1, float v1, float x, float y,
            float w, float h, unsigned int prim, unsigned int env, int mode) {
    if (!hudEnabled() || tex == nullptr || texW <= 0 || texH <= 0 || w <= 0.0f || h <= 0.0f) {
        return;
    }
    if ((prim & 0xFFu) == 0u) {
        return; // fully faded out — the N64 path would draw nothing visible either
    }
    const void* pixels = resolveTex(tex);
    if (pixels == nullptr) {
        return;
    }
    sQuads.push_back({ pixels, texW, texH, u0, v0, u1, v1, x, y, w, h, prim, env, mode });
}

// Decode an IA4 (4bpp: 3 bits intensity + 1 bit alpha) texture to RGBA32.
//
// Cached by CONTENT hash rather than by source address: the do-action label rewrites the same
// `doActionSegment` buffer whenever the prompt changes, so a pointer-keyed cache would pin whichever
// label happened to be showing first. The set of distinct labels is small and bounded, and keeping
// one buffer per hash also keeps the GPU-side upload cache correct — it keys on the buffer address,
// so a changed label naturally becomes a new key instead of silently reusing a stale upload.
const std::vector<uint8_t>* decodeIA4(const void* src, int w, int h) {
    const size_t nibbles = (size_t)w * h;
    const size_t bytes = (nibbles + 1) / 2;
    const uint8_t* in = (const uint8_t*)src;

    uint64_t hash = 1469598103934665603ull; // FNV-1a over the encoded bytes plus the dims
    for (size_t i = 0; i < bytes; i++) {
        hash = (hash ^ in[i]) * 1099511628211ull;
    }
    hash = (hash ^ (uint64_t)w) * 1099511628211ull;
    hash = (hash ^ (uint64_t)h) * 1099511628211ull;

    static std::unordered_map<uint64_t, std::vector<uint8_t>> cache;
    auto it = cache.find(hash);
    if (it != cache.end()) {
        return &it->second;
    }

    std::vector<uint8_t> out(nibbles * 4);
    for (size_t i = 0; i < nibbles; i++) {
        const uint8_t byte = in[i >> 1];
        const uint8_t v = (i & 1) ? (uint8_t)(byte & 0x0F) : (uint8_t)(byte >> 4);
        const uint8_t intensity = (uint8_t)(((v >> 1) & 0x07) * 255 / 7);
        out[i * 4 + 0] = intensity;
        out[i * 4 + 1] = intensity;
        out[i * 4 + 2] = intensity;
        out[i * 4 + 3] = (v & 1) ? 255 : 0;
    }
    return &cache.emplace(hash, std::move(out)).first->second;
}

} // namespace

extern "C" int Zelda3D_HudOwns(int element) {
    (void)element; // every converted group shares one gate; see zelda3d_hud.h
    return hudEnabled() ? 1 : 0;
}

extern "C" void Zelda3D_HudQuad(const void* tex, int texW, int texH, float x, float y, float w, float h,
                                unsigned int primRGBA) {
    record(tex, texW, texH, 0.0f, 0.0f, 1.0f, 1.0f, x, y, w, h, primRGBA, 0u, 0);
}

extern "C" void Zelda3D_HudQuadUv(const void* tex, int texW, int texH, int sx, int sy, int sw, int sh, float x,
                                  float y, float w, float h, unsigned int primRGBA) {
    if (texW <= 0 || texH <= 0) {
        return;
    }
    record(tex, texW, texH, (float)sx / texW, (float)sy / texH, (float)(sx + sw) / texW,
           (float)(sy + sh) / texH, x, y, w, h, primRGBA, 0u, 0);
}

extern "C" void Zelda3D_HudQuadIA4(const void* tex, int texW, int texH, float x, float y, float w, float h,
                                   unsigned int primRGBA) {
    if (!hudEnabled() || tex == nullptr || texW <= 0 || texH <= 0) {
        return;
    }
    const void* pixels = resolveTex(tex);
    if (pixels == nullptr) {
        return;
    }
    const std::vector<uint8_t>* rgba = decodeIA4(pixels, texW, texH);
    record(rgba->data(), texW, texH, 0.0f, 0.0f, 1.0f, 1.0f, x, y, w, h, primRGBA, 0u, 0);
}

extern "C" void Zelda3D_HudQuadLerp(const void* tex, int texW, int texH, float x, float y, float w, float h,
                                    unsigned int primRGBA, unsigned int envRGB) {
    record(tex, texW, texH, 0.0f, 0.0f, 1.0f, 1.0f, x, y, w, h, primRGBA, envRGB, 1);
}

extern "C" void Zelda3D_HudFrame(void) {
    if (sQuads.empty()) {
        return;
    }
    // The title demo shows no HUD; anything recorded before that was known is dropped rather than
    // drawn (same rule Interface_Draw applies on the N64 path).
    if (Zelda3D_Title_IsActive()) {
        sQuads.clear();
        return;
    }
    int W = 0, H = 0;
    if (!Zelda3D_Hud_Begin(&W, &H) || W <= 0 || H <= 0) {
        sQuads.clear();
        return;
    }

    // HUD virtual space -> framebuffer pixels. SoH keeps the N64 320x240 canvas and EXTENDS it
    // horizontally for widescreen: OTRGetDimensionFromLeftEdge(v) = 160 - 120*aspect + v and
    // OTRGetDimensionFromRightEdge(v) = 160 + 120*aspect - (320 - v), so the visible X range is
    // [160 - 120*aspect, 160 + 120*aspect] while Y stays [0, 240]. One scale, one X origin.
    const float aspect = (float)W / (float)H;
    const float sc = (float)H / 240.0f;
    const float originX = 160.0f - 120.0f * aspect;

    for (const HudQuad& q : sQuads) {
        const int id = Zelda3D_Hud_Tex(q.tex, q.tex, q.texW, q.texH);
        if (id == 0) {
            continue;
        }
        Zelda3D_Hud_DrawEnv(id, (q.x - originX) * sc, q.y * sc, q.w * sc, q.h * sc, q.u0, q.v0, q.u1, q.v1,
                            q.prim, q.env, q.mode);
    }
    Zelda3D_Hud_End();
    sQuads.clear();
}
