// See zelda3d_hud.h for the design and the user directive behind it (#205).
#include "zelda3d_hud.h"

#include "../behaviors/title/title_activity.h" // Zelda3D_Title_IsActive — no HUD over the title demo

#include <cstdint>
#include <cstdlib>
#include <spdlog/spdlog.h>
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
void Zelda3D_Hud_Flush(void);

// SoH stores most HUD "textures" as OTR PATH STRINGS ("__OTR__textures/icon_item_static/...") and the
// Fast3D interpreter resolves them to pixels when it executes gDPLoadTextureBlock. A native HUD gets
// no such step, so it has to resolve them itself — this is why the first native item-icon draw came
// out blank: the recorded pointer was the path, not the image.
int ResourceMgr_OTRSigCheck(char* imgData);
char* ResourceMgr_GetResourceDataByNameHandlingMQ(const char* path);
// How many bytes that resource actually has. Needed because the decoder below derives a byte count
// from (format, width, height) supplied by the CALL SITE, and a call site that disagrees with the
// asset reads off the end -- which is what happened with the magic-meter fill.
size_t ResourceGetTexSizeByName(const char* name);
uint16_t ResourceGetTexWidthByName(const char* name);
uint16_t ResourceGetTexHeightByName(const char* name);
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

// The resolved buffer's size in bytes, or 0 when it is not a resource we can measure (our own
// runtime-built HUD textures are raw buffers the caller owns). 0 means "unknown", never "empty" --
// the caller must not treat it as a zero-length buffer.
size_t resolvedTexBytes(const void* tex) {
    if (tex != nullptr && ResourceMgr_OTRSigCheck((char*)tex)) {
        return ResourceGetTexSizeByName((const char*)tex);
    }
    return 0;
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

} // namespace

// See the header for the format list and for why I-formats decode opaque.
extern "C" const void* Zelda3D_HudDecode(int fmt, const void* tex, int texW, int texH) {
    if (tex == nullptr || texW <= 0 || texH <= 0) {
        return nullptr;
    }
    const uint8_t* in = (const uint8_t*)resolveTex(tex);
    if (in == nullptr) {
        return nullptr;
    }
    const size_t texels = (size_t)texW * texH;
    const size_t bytes = (fmt == ZELDA3D_HUD_FMT_IA4 || fmt == ZELDA3D_HUD_FMT_I4) ? (texels + 1) / 2
                       : (fmt == ZELDA3D_HUD_FMT_IA8 || fmt == ZELDA3D_HUD_FMT_I8) ? texels
                                                                                   : 0;
    if (bytes == 0) {
        return nullptr;
    }
    // The call site supplies the format and the dimensions; the asset supplies the bytes. When they
    // disagree the loop below walks off the end of the resource -- AddressSanitizer caught exactly
    // that on the magic meter (`heap-buffer-overflow READ` at the last byte of a 144-byte resource,
    // from Interface_DrawMagicBar). Refuse and NAME both sides rather than decode garbage: a HUD
    // element that vanishes with a log line is debuggable, an out-of-bounds read is not.
    const size_t have = resolvedTexBytes(tex);
    if (have != 0 && bytes > have) {
        static std::unordered_map<const void*, bool> reported; // once per texture, not once per frame
        if (!reported[tex]) {
            reported[tex] = true;
            SPDLOG_ERROR("HUD texture {}: call site says fmt {} {}x{} = {} bytes, but the resource has {} "
                         "({}x{} per its own header). Not decoding it.",
                         (const char*)tex, fmt, texW, texH, bytes, have,
                         ResourceGetTexWidthByName((const char*)tex), ResourceGetTexHeightByName((const char*)tex));
        }
        return nullptr;
    }

    uint64_t hash = 1469598103934665603ull; // FNV-1a over the encoded bytes, the dims and the format
    for (size_t i = 0; i < bytes; i++) {
        hash = (hash ^ in[i]) * 1099511628211ull;
    }
    hash = (hash ^ (uint64_t)texW) * 1099511628211ull;
    hash = (hash ^ (uint64_t)texH) * 1099511628211ull;
    hash = (hash ^ (uint64_t)fmt) * 1099511628211ull;

    static std::unordered_map<uint64_t, std::vector<uint8_t>> cache;
    auto it = cache.find(hash);
    if (it != cache.end()) {
        return it->second.data();
    }

    std::vector<uint8_t> out(texels * 4);
    for (size_t i = 0; i < texels; i++) {
        uint8_t intensity, alpha;
        switch (fmt) {
            case ZELDA3D_HUD_FMT_IA4: {
                const uint8_t v = (i & 1) ? (uint8_t)(in[i >> 1] & 0x0F) : (uint8_t)(in[i >> 1] >> 4);
                intensity = (uint8_t)(((v >> 1) & 0x07) * 255 / 7);
                alpha = (v & 1) ? 255 : 0;
                break;
            }
            case ZELDA3D_HUD_FMT_IA8:
                intensity = (uint8_t)((in[i] >> 4) * 255 / 15);
                alpha = (uint8_t)((in[i] & 0x0F) * 255 / 15);
                break;
            case ZELDA3D_HUD_FMT_I4: {
                const uint8_t v = (i & 1) ? (uint8_t)(in[i >> 1] & 0x0F) : (uint8_t)(in[i >> 1] >> 4);
                intensity = (uint8_t)(v * 255 / 15);
                alpha = 255;
                break;
            }
            default:
                intensity = in[i];
                alpha = 255;
                break;
        }
        out[i * 4 + 0] = intensity;
        out[i * 4 + 1] = intensity;
        out[i * 4 + 2] = intensity;
        out[i * 4 + 3] = alpha;
    }
    return cache.emplace(hash, std::move(out)).first->second.data();
}

extern "C" void Zelda3D_HudSetEnabled(int on) {
    sEnabled = on ? 1 : 0;
}

extern "C" int Zelda3D_HudGetEnabled(void) {
    return hudEnabled() ? 1 : 0;
}

extern "C" int Zelda3D_HudOwns(int element) {
    (void)element; // every converted group shares one gate; see zelda3d_hud.h
    return hudEnabled() ? 1 : 0;
}

extern "C" void Zelda3D_HudQuad(const void* tex, int texW, int texH, float x, float y, float w, float h,
                                unsigned int primRGBA) {
    record(tex, texW, texH, 0.0f, 0.0f, 1.0f, 1.0f, x, y, w, h, primRGBA, 0u, 0);
}

extern "C" void Zelda3D_HudQuadEx(const void* tex, int texW, int texH, int sx, int sy, int sw, int sh, float x,
                                  float y, float w, float h, unsigned int primRGBA, unsigned int envRGB,
                                  int mode) {
    if (texW <= 0 || texH <= 0) {
        return;
    }
    record(tex, texW, texH, (float)sx / texW, (float)sy / texH, (float)(sx + sw) / texW,
           (float)(sy + sh) / texH, x, y, w, h, primRGBA, envRGB, mode);
}

namespace {

// How many of sQuads have already been handed to the renderer this frame. Quads are RECORDED while
// the display list is being built, but they must be COMPOSITED at the point of the interpreter's
// execution where their element belongs — so they are pushed in instalments: each
// gSPZelda3DHudFlush marker pushes everything recorded up to it, and Zelda3D_HudFrame pushes the
// remainder at end of frame.
size_t sPushed = 0;
bool sBatchOpen = false;
float sScale = 0.0f, sOriginX = 0.0f;

// Push the quads recorded since the last push and composite them here. Opening the renderer batch is
// lazy: it needs a live frame recording, which only exists once the interpreter is running.
void pushPending() {
    if (sPushed >= sQuads.size()) {
        return;
    }
    if (!sBatchOpen) {
        int W = 0, H = 0;
        if (!Zelda3D_Hud_Begin(&W, &H) || W <= 0 || H <= 0) {
            return;
        }
        // HUD virtual space -> framebuffer pixels. SoH keeps the N64 320x240 canvas and EXTENDS it
        // horizontally for widescreen: the visible X range is [160 - 120*aspect, 160 + 120*aspect]
        // while Y stays [0, 240]. One scale, one X origin.
        sScale = (float)H / 240.0f;
        sOriginX = 160.0f - 120.0f * ((float)W / (float)H);
        sBatchOpen = true;
    }
    for (; sPushed < sQuads.size(); sPushed++) {
        const HudQuad& q = sQuads[sPushed];
        const int id = Zelda3D_Hud_Tex(q.tex, q.tex, q.texW, q.texH);
        if (id == 0) {
            continue;
        }
        Zelda3D_Hud_DrawEnv(id, (q.x - sOriginX) * sScale, q.y * sScale, q.w * sScale, q.h * sScale, q.u0, q.v0,
                            q.u1, q.v1, q.prim, q.env, q.mode);
    }
    Zelda3D_Hud_Flush();
}

} // namespace

// Called by the interpreter when it reaches a gSPZelda3DHudFlush marker in the display list. This is
// what lets an element convert ALONE: its quads composite at the marker, so anything the interpreter
// draws afterwards (the minimap's 3D compass arrows, for instance) still lands on top of it.
extern "C" void Zelda3D_HudFlushPoint(void) {
    if (Zelda3D_Title_IsActive()) {
        return;
    }
    pushPending();
}

extern "C" void Zelda3D_HudFrame(void) {
    if (sQuads.empty()) {
        return;
    }
    // The title demo shows no HUD; anything recorded before that was known is dropped rather than
    // drawn (same rule Interface_Draw applies on the N64 path).
    if (Zelda3D_Title_IsActive()) {
        sQuads.clear();
        sPushed = 0;
        return;
    }
    // Everything not already composited at a marker goes now, on top of the finished frame.
    pushPending();
    if (sBatchOpen) {
        Zelda3D_Hud_End();
        sBatchOpen = false;
    }
    sQuads.clear();
    sPushed = 0;
}
