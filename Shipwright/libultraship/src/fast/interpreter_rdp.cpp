#define NOMINMAX

#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif

#include "fast/interpreter.h"
#include "fast/lus_gbi.h"
#include "fast/resource/type/Light.h"
#include "fast/zelda3d_submission.h"
#include "fast/backends/gfx_window_manager_api.h"
#include "fast/Fast3dWindow.h"
#include "fast/backends/gfx_rendering_api.h"
#include "ship/window/gui/Gui.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/Utils.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/zelda3d_hostiface.h"
#include "libultraship/libultra/os.h"
#include <spdlog/fmt/fmt.h>

#include "interpreter_geometry_observation.h"
#include "interpreter_rdp_encoding.h"
#include "interpreter_runtime_state.h"
#include "interpreter_texture_decode.h"
#include "interpreter_viewport_math.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define SUPPORT_CHECK(expression) assert(expression)

namespace Fast {

void Interpreter::GfxDpSetScissor(uint32_t mode, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    float x = ulx / 4.0f;
    float y = lry / 4.0f;
    float width = (lrx - ulx) / 4.0f;
    float height = (lry - uly) / 4.0f;

    mRdp->scissor.x = x;
    mRdp->scissor.y = y;
    mRdp->scissor.width = width;
    mRdp->scissor.height = height;

    AdjustVIewportOrScissor(&mRdp->scissor);

    mRdp->viewport_or_scissor_changed = true;
}

void Interpreter::GfxDpSetTextureImage(uint32_t format, uint32_t size, uint32_t width, const char* texPath,
                                       uint32_t texFlags, RawTexMetadata rawTexMetdata, const void* addr) {
    // fprintf(stderr, "GfxDpSetTextureImage: %s (width=%d; size=0x%X)\n",
    //         rawTexMetdata.resource ? rawTexMetdata.resource->GetInitData()->Path.c_str() : nullptr, width, size);
    mRdp->texture_to_load.addr = (const uint8_t*)addr;
    mRdp->texture_to_load.siz = size;
    mRdp->texture_to_load.width = width;
    mRdp->texture_to_load.tex_flags = texFlags;
    mRdp->texture_to_load.raw_tex_metadata = rawTexMetdata;
}

void Interpreter::GfxDpSetTile(uint8_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint8_t tile, uint32_t palette,
                               uint32_t cmt, uint32_t maskt, uint32_t shiftt, uint32_t cms, uint32_t masks,
                               uint32_t shifts) {
    // OTRTODO:
    // SUPPORT_CHECK(tmem == 0 || tmem == 256);

    if (cms == G_TX_WRAP && masks == G_TX_NOMASK) {
        cms = G_TX_CLAMP;
    }
    if (cmt == G_TX_WRAP && maskt == G_TX_NOMASK) {
        cmt = G_TX_CLAMP;
    }

    mRdp->texture_tile[tile].palette = palette; // palette should set upper 4 bits of color index in 4b mode
    mRdp->texture_tile[tile].fmt = fmt;
    mRdp->texture_tile[tile].siz = siz;
    mRdp->texture_tile[tile].cms = cms;
    mRdp->texture_tile[tile].cmt = cmt;
    mRdp->texture_tile[tile].shifts = shifts;
    mRdp->texture_tile[tile].shiftt = shiftt;
    mRdp->texture_tile[tile].line_size_bytes = line * 8;

    mRdp->texture_tile[tile].tmem = tmem;
    // mRdp->texture_tile[tile].tmem_index = tmem / 256; // tmem is the 64-bit word offset, so 256 words means 2 kB

    mRdp->texture_tile[tile].tmem_index =
        tmem != 0; // assume one texture is loaded at address 0 and another texture at any other address

    mRdp->textures_changed[0] = true;
    mRdp->textures_changed[1] = true;
}

void Interpreter::GfxDpSetTileSize(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs, uint16_t lrt) {
    mRdp->texture_tile[tile].uls = uls;
    mRdp->texture_tile[tile].ult = ult;
    mRdp->texture_tile[tile].lrs = lrs;
    mRdp->texture_tile[tile].lrt = lrt;
    mRdp->textures_changed[0] = true;
    mRdp->textures_changed[1] = true;
}

void Interpreter::GfxDpLoadTlut(uint8_t tile, uint32_t high_index) {
    SUPPORT_CHECK(mRdp->texture_to_load.siz == G_IM_SIZ_16b);

    uint16_t tmem = mRdp->texture_tile[tile].tmem;
    const uint8_t* src = mRdp->texture_to_load.addr;
    uint32_t entryCount = high_index + 1;
    uint32_t byteCount = entryCount * 2;

    // No texture source set -> the preceding G_SETTIMG was deliberately skipped because
    // its source couldn't be resolved (see gfx_set_timg_handler_rdp: an unresolved N64
    // segment address, or a resource not yet loaded). That skip was only half applied:
    // SETTIMG bailed but left texture_to_load with no/stale source, so this dependent
    // palette load would memcpy from a null/garbage pointer and crash (intermittently on
    // the first frame of a scene, before any valid SETTIMG has run). Honour the same
    // skip here — with no source there is no palette to load — and report it (mirrors the
    // null-texture handling in ImportTexture*). This COMPLETES SoH's own skip semantics.
    if (src == nullptr) {
        SPDLOG_WARN("GfxDpLoadTlut: skipping palette load (tile={}, tmem={}, entries={}) — no texture source; the "
                    "preceding G_SETTIMG was skipped (unresolved segment / resource not loaded)",
                    tile, tmem, entryCount);
        return;
    }

    if (tmem >= 256) {
        // N64 TMEM palette area starts at tmem word 256. Each CI4 palette = 16 entries = 16 tmem words.
        uint32_t paletteByteOffset = (tmem - 256) * 2;

        if (high_index == 255 && paletteByteOffset == 0) {
            // CI8: full 256-entry palette spanning both halves
            memcpy(mRdp->palette_staging[0], src, 256);
            memcpy(mRdp->palette_staging[1], src + 256, 256);
            mRdp->palettes[0] = mRdp->palette_staging[0];
            mRdp->palettes[1] = mRdp->palette_staging[1];
            mRdp->palette_dram_addr[0] = src;
            mRdp->palette_dram_addr[1] = src + 256;
        } else if (paletteByteOffset < 256) {
            // Palettes 0-7 range
            uint32_t copyLen = (paletteByteOffset + byteCount <= 256) ? byteCount : (256 - paletteByteOffset);
            memcpy(mRdp->palette_staging[0] + paletteByteOffset, src, copyLen);
            mRdp->palettes[0] = mRdp->palette_staging[0];
            mRdp->palette_dram_addr[0] = src;
        } else {
            // Palettes 8-15 range
            uint32_t offset = paletteByteOffset - 256;
            uint32_t copyLen = (offset + byteCount <= 256) ? byteCount : (256 - offset);
            memcpy(mRdp->palette_staging[1] + offset, src, copyLen);
            mRdp->palettes[1] = mRdp->palette_staging[1];
            mRdp->palette_dram_addr[1] = src;
        }
    } else {
        // tmem < 256: non-standard location, fall back to direct pointer
        mRdp->palettes[1] = src;
        mRdp->palette_dram_addr[1] = src;
    }
}

void Interpreter::GfxDpLoadBlock(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t dxt) {
    SUPPORT_CHECK(uls == 0);
    SUPPORT_CHECK(ult == 0);

    // The lrs field rather seems to be number of pixels to load
    uint32_t word_size_shift = 0;
    switch (mRdp->texture_to_load.siz) {
        case G_IM_SIZ_4b:
            word_size_shift = -1;
            break;
        case G_IM_SIZ_8b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_16b:
            word_size_shift = 1;
            break;
        case G_IM_SIZ_32b:
            word_size_shift = 2;
            break;
    }
    uint32_t orig_size_bytes =
        word_size_shift > 0 ? (lrs + 1) << word_size_shift : (lrs + 1) >> (-(int64_t)word_size_shift);
    uint32_t size_bytes = orig_size_bytes;
    if (mRdp->texture_to_load.raw_tex_metadata.h_byte_scale != 1 ||
        mRdp->texture_to_load.raw_tex_metadata.v_pixel_scale != 1) {
        size_bytes *= mRdp->texture_to_load.raw_tex_metadata.h_byte_scale;
        size_bytes *= mRdp->texture_to_load.raw_tex_metadata.v_pixel_scale;
    }
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].orig_size_bytes = orig_size_bytes;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].size_bytes = size_bytes;
    // Compute actual per-line DRAM stride from SetTextureImage width when available.
    // The standard gDPLoadTextureBlock macro sets width=1, but manually-built DL
    // commands may set the real pixel width.
    uint32_t actual_line_bytes = size_bytes;
    if (mRdp->texture_to_load.width > 1) {
        uint32_t candidate;
        switch (mRdp->texture_to_load.siz) {
            case G_IM_SIZ_4b:
                candidate = (mRdp->texture_to_load.width + 1) / 2;
                break;
            case G_IM_SIZ_8b:
                candidate = mRdp->texture_to_load.width;
                break;
            case G_IM_SIZ_16b:
                candidate = mRdp->texture_to_load.width * 2;
                break;
            case G_IM_SIZ_32b:
                candidate = mRdp->texture_to_load.width * 4;
                break;
            default:
                candidate = mRdp->texture_to_load.width;
                break;
        }
        if (candidate > 0 && candidate < size_bytes && size_bytes % candidate == 0) {
            actual_line_bytes = candidate;
        }
    }
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].line_size_bytes = actual_line_bytes;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].full_image_line_size_bytes = actual_line_bytes;
    // assert(size_bytes <= 4096 && "bug: too big texture");
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].tex_flags = mRdp->texture_to_load.tex_flags;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].raw_tex_metadata = mRdp->texture_to_load.raw_tex_metadata;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].addr = mRdp->texture_to_load.addr;
    // fprintf(stderr, "GfxDpLoadBlock: line_size = 0x%x; orig = 0x%x; bpp=%d; lrs=%d\n", size_bytes,
    // orig_size_bytes,
    //         mRdp->texture_to_load.siz, lrs);

    const std::string_view texPath =
        mRdp->texture_to_load.raw_tex_metadata.resource != nullptr
            ? GetBaseTexturePath(mRdp->texture_to_load.raw_tex_metadata.resource->GetInitData()->Path)
            : std::string_view{};
    auto maskedTextureIter = mMaskedTextures.find(texPath);
    if (maskedTextureIter != mMaskedTextures.end()) {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = true;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended =
            maskedTextureIter->second.replacementData != nullptr;
    } else {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = false;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended = false;
    }

    mRdp->textures_changed[mRdp->texture_tile[tile].tmem_index] = true;
}

void Interpreter::GfxDpLoadTile(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    SUPPORT_CHECK(tile == G_TX_LOADTILE);

    uint32_t word_size_shift = 0;
    switch (mRdp->texture_to_load.siz) {
        case G_IM_SIZ_4b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_8b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_16b:
            word_size_shift = 1;
            break;
        case G_IM_SIZ_32b:
            word_size_shift = 2;
            break;
    }

    uint32_t offset_x = uls >> G_TEXTURE_IMAGE_FRAC;
    uint32_t offset_y = ult >> G_TEXTURE_IMAGE_FRAC;
    uint32_t tile_width = ((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1;
    uint32_t tile_height = ((lrt - ult) >> G_TEXTURE_IMAGE_FRAC) + 1;
    uint32_t full_image_width = mRdp->texture_to_load.width;

    uint32_t offset_x_in_bytes = offset_x << word_size_shift;
    uint32_t tile_line_size_bytes = tile_width << word_size_shift;
    uint32_t full_image_line_size_bytes = full_image_width << word_size_shift;

    uint32_t orig_size_bytes = tile_line_size_bytes * tile_height;
    uint32_t size_bytes = orig_size_bytes;
    uint32_t start_offset_bytes = full_image_line_size_bytes * offset_y + offset_x_in_bytes;

    float h_byte_scale = mRdp->texture_to_load.raw_tex_metadata.h_byte_scale;
    float v_pixel_scale = mRdp->texture_to_load.raw_tex_metadata.v_pixel_scale;

    if (h_byte_scale != 1 || v_pixel_scale != 1) {
        start_offset_bytes = h_byte_scale * (v_pixel_scale * offset_y * full_image_line_size_bytes + offset_x_in_bytes);
        size_bytes *= h_byte_scale * v_pixel_scale;
        full_image_line_size_bytes *= h_byte_scale;
        tile_line_size_bytes *= h_byte_scale;
    }

    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].orig_size_bytes = orig_size_bytes;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].size_bytes = size_bytes;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].full_image_line_size_bytes = full_image_line_size_bytes;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].line_size_bytes = tile_line_size_bytes;

    //    assert(size_bytes <= 4096 && "bug: too big texture");
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].tex_flags = mRdp->texture_to_load.tex_flags;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].raw_tex_metadata = mRdp->texture_to_load.raw_tex_metadata;
    mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].addr = mRdp->texture_to_load.addr + start_offset_bytes;

    const std::string_view texPath =
        mRdp->texture_to_load.raw_tex_metadata.resource != nullptr
            ? GetBaseTexturePath(mRdp->texture_to_load.raw_tex_metadata.resource->GetInitData()->Path)
            : std::string_view{};
    auto maskedTextureIter = mMaskedTextures.find(texPath);
    if (maskedTextureIter != mMaskedTextures.end()) {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = true;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended =
            maskedTextureIter->second.replacementData != nullptr;
    } else {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = false;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended = false;
    }

    mRdp->texture_tile[tile].uls = uls;
    mRdp->texture_tile[tile].ult = ult;
    mRdp->texture_tile[tile].lrs = lrs;
    mRdp->texture_tile[tile].lrt = lrt;

    mRdp->textures_changed[mRdp->texture_tile[tile].tmem_index] = true;
}

/*static uint8_t color_comb_component(uint32_t v) {
    switch (v) {
        case G_CCMUX_TEXEL0:
            return CC_TEXEL0;
        case G_CCMUX_TEXEL1:
            return CC_TEXEL1;
        case G_CCMUX_PRIMITIVE:
            return CC_PRIM;
        case G_CCMUX_SHADE:
            return CC_SHADE;
        case G_CCMUX_ENVIRONMENT:
            return CC_ENV;
        case G_CCMUX_TEXEL0_ALPHA:
            return CC_TEXEL0A;
        case G_CCMUX_LOD_FRACTION:
            return CC_LOD;
        default:
            return CC_0;
    }
}

static inline uint32_t color_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return color_comb_component(a) |
           (color_comb_component(b) << 3) |
           (color_comb_component(c) << 6) |
           (color_comb_component(d) << 9);
}

static void GfxDpSetCombineMode(uint32_t rgb, uint32_t alpha) {
    mRdp->combine_mode = rgb | (alpha << 12);
}*/

void Interpreter::GfxDpSetCombineMode(uint32_t rgb, uint32_t alpha, uint32_t rgb_cyc2, uint32_t alpha_cyc2) {
    mRdp->combine_mode = rgb | (alpha << 16) | ((uint64_t)rgb_cyc2 << 28) | ((uint64_t)alpha_cyc2 << 44);
}

void Interpreter::GfxDpSetGrayscaleColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    mRdp->grayscale_color.r = r;
    mRdp->grayscale_color.g = g;
    mRdp->grayscale_color.b = b;
    mRdp->grayscale_color.a = a;
}

void Interpreter::GfxDpSetEnvColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    mRdp->env_color.r = r;
    mRdp->env_color.g = g;
    mRdp->env_color.b = b;
    mRdp->env_color.a = a;
}

void Interpreter::GfxDpSetPrimColor(uint8_t m, uint8_t l, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    mRdp->prim_lod_fraction = l;
    mRdp->prim_color.r = r;
    mRdp->prim_color.g = g;
    mRdp->prim_color.b = b;
    mRdp->prim_color.a = a;
}

void Interpreter::GfxDpSetFogColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    mRdp->fog_color.r = r;
    mRdp->fog_color.g = g;
    mRdp->fog_color.b = b;
    mRdp->fog_color.a = a;
}

void Interpreter::GfxDpSetBlendColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    mRdp->blend_color.r = r;
    mRdp->blend_color.g = g;
    mRdp->blend_color.b = b;
    mRdp->blend_color.a = a;
}

void Interpreter::GfxDpSetFillColor(uint32_t packed_color) {
    uint16_t col16 = (uint16_t)packed_color;
    uint32_t r = col16 >> 11;
    uint32_t g = (col16 >> 6) & 0x1f;
    uint32_t b = (col16 >> 1) & 0x1f;
    uint32_t a = col16 & 1;
    mRdp->fill_color.r = Scale5To8(r);
    mRdp->fill_color.g = Scale5To8(g);
    mRdp->fill_color.b = Scale5To8(b);
    mRdp->fill_color.a = a * 255;
}

void Interpreter::GfxDrawRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t saved_other_mode_h = mRdp->other_mode_h;
    uint32_t cycle_type = (mRdp->other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    if (cycle_type == G_CYC_COPY) {
        mRdp->other_mode_h = (mRdp->other_mode_h & ~(3U << G_MDSFT_TEXTFILT)) | G_TF_POINT;
    }

    // U10.2 coordinates
    float ulxf = ulx;
    float ulyf = uly;
    float lrxf = lrx;
    float lryf = lry;

    const FBInfo* framebuffer = mFbActive ? &mActiveFrameBuffer->second : nullptr;
    const float halfWidth = InterpreterHalfWidth(framebuffer, mNativeDimensions);
    const float halfHeight = InterpreterHalfHeight(framebuffer, mNativeDimensions);
    ulxf = ulxf / (4.0f * halfWidth) - 1.0f;
    ulyf = -(ulyf / (4.0f * halfHeight)) + 1.0f;
    lrxf = lrxf / (4.0f * halfWidth) - 1.0f;
    lryf = -(lryf / (4.0f * halfHeight)) + 1.0f;

    ulxf = AdjXForAspectRatio(ulxf);
    lrxf = AdjXForAspectRatio(lrxf);

    struct LoadedVertex* ul = &mRsp->loaded_vertices[MAX_VERTICES + 0];
    struct LoadedVertex* ll = &mRsp->loaded_vertices[MAX_VERTICES + 1];
    struct LoadedVertex* lr = &mRsp->loaded_vertices[MAX_VERTICES + 2];
    struct LoadedVertex* ur = &mRsp->loaded_vertices[MAX_VERTICES + 3];

    ul->x = ulxf;
    ul->y = ulyf;
    ul->z = -1.0f;
    ul->w = 1.0f;

    ll->x = ulxf;
    ll->y = lryf;
    ll->z = -1.0f;
    ll->w = 1.0f;

    lr->x = lrxf;
    lr->y = lryf;
    lr->z = -1.0f;
    lr->w = 1.0f;

    ur->x = lrxf;
    ur->y = ulyf;
    ur->z = -1.0f;
    ur->w = 1.0f;

    // The coordinates for texture rectangle shall bypass the viewport setting
    struct XYWidthHeight default_viewport;
    if (!mFbActive) {
        default_viewport = { 0, (int16_t)mNativeDimensions.height, mNativeDimensions.width, mNativeDimensions.height };
    } else {
        default_viewport = { 0, (int16_t)mActiveFrameBuffer->second.orig_height, mActiveFrameBuffer->second.orig_width,
                             mActiveFrameBuffer->second.orig_height };
    }

    struct XYWidthHeight viewport_saved = mRdp->viewport;
    uint32_t geometry_mode_saved = mRsp->geometry_mode;

    AdjustVIewportOrScissor(&default_viewport);

    mRdp->viewport = default_viewport;
    mRdp->viewport_or_scissor_changed = true;
    mRsp->geometry_mode = 0;

    GfxSpTri1(MAX_VERTICES + 0, MAX_VERTICES + 1, MAX_VERTICES + 3, true);
    GfxSpTri1(MAX_VERTICES + 1, MAX_VERTICES + 2, MAX_VERTICES + 3, true);

    mRsp->geometry_mode = geometry_mode_saved;
    mRdp->viewport = viewport_saved;
    mRdp->viewport_or_scissor_changed = true;

    if (cycle_type == G_CYC_COPY) {
        mRdp->other_mode_h = saved_other_mode_h;
    }
}

void Interpreter::GfxDpTextureRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile, int16_t uls,
                                        int16_t ult, int16_t dsdx, int16_t dtdy, bool flip) {
    // printf("render %d at %d\n", tile, lrx);
    uint64_t saved_combine_mode = mRdp->combine_mode;
    if ((mRdp->other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        // Per RDP Command Summary Set Tile's shift s and this dsdx should be set to 4 texels
        // Divide by 4 to get 1 instead
        dsdx >>= 2;

        // Color combiner is turned off in copy mode
        GfxDpSetCombineMode(EncodeColorCombiner(0, 0, 0, G_CCMUX_TEXEL0), EncodeAlphaCombiner(0, 0, 0, G_ACMUX_TEXEL0),
                            0, 0);

        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    // uls and ult are S10.5
    // dsdx and dtdy are S5.10
    // lrx, lry, ulx, uly are U10.2
    // lrs, lrt are S10.5
    if (flip) {
        dsdx = -dsdx;
        dtdy = -dtdy;
    }
    int16_t width = !flip ? lrx - ulx : lry - uly;
    int16_t height = !flip ? lry - uly : lrx - ulx;
    float lrs = ((uls << 7) + dsdx * width) >> 7;
    float lrt = ((ult << 7) + dtdy * height) >> 7;

    LoadedVertex* ul = &mRsp->loaded_vertices[MAX_VERTICES + 0];
    LoadedVertex* ll = &mRsp->loaded_vertices[MAX_VERTICES + 1];
    LoadedVertex* lr = &mRsp->loaded_vertices[MAX_VERTICES + 2];
    LoadedVertex* ur = &mRsp->loaded_vertices[MAX_VERTICES + 3];
    ul->u = uls;
    ul->v = ult;
    lr->u = lrs;
    lr->v = lrt;
    if (!flip) {
        ll->u = uls;
        ll->v = lrt;
        ur->u = lrs;
        ur->v = ult;
    } else {
        ll->u = lrs;
        ll->v = ult;
        ur->u = uls;
        ur->v = lrt;
    }

    uint8_t saved_tile = mRdp->first_tile_index;
    if (saved_tile != tile) {
        mRdp->textures_changed[0] = true;
        mRdp->textures_changed[1] = true;
    }
    mRdp->first_tile_index = tile;

    GfxDrawRectangle(ulx, uly, lrx, lry);
    if (saved_tile != tile) {
        mRdp->textures_changed[0] = true;
        mRdp->textures_changed[1] = true;
    }
    mRdp->first_tile_index = saved_tile;
    mRdp->combine_mode = saved_combine_mode;
}

void Interpreter::GfxDpImageRectangle(int32_t tile, int32_t w, int32_t h, int32_t ulx, int32_t uly, int16_t uls,
                                      int16_t ult, int32_t lrx, int32_t lry, int16_t lrs, int16_t lrt) {

    LoadedVertex* ul = &mRsp->loaded_vertices[MAX_VERTICES + 0];
    LoadedVertex* ll = &mRsp->loaded_vertices[MAX_VERTICES + 1];
    LoadedVertex* lr = &mRsp->loaded_vertices[MAX_VERTICES + 2];
    LoadedVertex* ur = &mRsp->loaded_vertices[MAX_VERTICES + 3];
    ul->u = uls * 32;
    ul->v = ult * 32;
    lr->u = lrs * 32;
    lr->v = lrt * 32;
    ll->u = uls * 32;
    ll->v = lrt * 32;
    ur->u = lrs * 32;
    ur->v = ult * 32;

    // ensure we have the correct texture size, format and starting position
    mRdp->texture_tile[tile].siz = G_IM_SIZ_8b;
    mRdp->texture_tile[tile].fmt = G_IM_FMT_RGBA;
    mRdp->texture_tile[tile].cms = 0;
    mRdp->texture_tile[tile].cmt = 0;
    mRdp->texture_tile[tile].shifts = 0;
    mRdp->texture_tile[tile].shiftt = 0;
    mRdp->texture_tile[tile].uls = 0 * 4;
    mRdp->texture_tile[tile].ult = 0 * 4;
    mRdp->texture_tile[tile].lrs = w * 4;
    mRdp->texture_tile[tile].lrt = h * 4;
    mRdp->texture_tile[tile].line_size_bytes = w << (mRdp->texture_tile[tile].siz >> 1);

    auto& loadtex = mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index];
    loadtex.full_image_line_size_bytes = loadtex.line_size_bytes = mRdp->texture_tile[tile].line_size_bytes;
    loadtex.size_bytes = loadtex.orig_size_bytes = loadtex.line_size_bytes * h;

    uint8_t saved_tile = mRdp->first_tile_index;
    if (saved_tile != tile) {
        mRdp->textures_changed[0] = true;
        mRdp->textures_changed[1] = true;
    }
    mRdp->first_tile_index = tile;

    GfxDrawRectangle(ulx, uly, lrx, lry);
    if (saved_tile != tile) {
        mRdp->textures_changed[0] = true;
        mRdp->textures_changed[1] = true;
    }
    mRdp->first_tile_index = saved_tile;
}

void Interpreter::GfxDpFillRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    if (mRdp->color_image_address == mRdp->z_buf_address) {
        // Fullscreen Z clears are redundant — already done by glClear at frame start.
        bool isFullScreen = (ulx <= 0 && uly <= 0 && lrx >= (int32_t)(mNativeDimensions.width - 1) * 4 &&
                             lry >= (int32_t)(mNativeDimensions.height - 1) * 4);
        if (isFullScreen) {
            return;
        }

        // Partial depth clear (e.g. HUD model regions): clear the actual depth buffer
        // via a scissored depth clear instead of drawing a colored rect to the color buffer.
        Flush();

        // Convert U10.2 coords to pixel coords and add +1 pixel for fill mode
        int32_t expanded_lrx = lrx + (1 << 2);
        int32_t expanded_lry = lry + (1 << 2);
        float x = ulx / 4.0f;
        float y = expanded_lry / 4.0f;
        float w = (expanded_lrx - ulx) / 4.0f;
        float h = (expanded_lry - uly) / 4.0f;

        struct XYWidthHeight area;
        area.x = (int16_t)x;
        area.y = (int16_t)y;
        area.width = (uint32_t)w;
        area.height = (uint32_t)h;
        AdjustVIewportOrScissor(&area);

        mRapi->ClearDepthRegion(area.x, area.y, area.width, area.height);
        return;
    }
    uint32_t mode = (mRdp->other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    // Expand fullscreen fill rects to cover widescreen viewports.
    // Without this, screen clears and fades only cover the native 4:3 area.
    if (ulx == 0 && uly == 0) {
        bool isFullScreen = (lrx == ((int32_t)(mNativeDimensions.width - 1) * 4) &&
                             lry == ((int32_t)(mNativeDimensions.height - 1) * 4));
        if (isFullScreen) {
            ulx = -1024;
            uly = -1024;
            lrx = 2048;
            lry = 2048;
        }
    }

    if (mode == G_CYC_COPY || mode == G_CYC_FILL) {
        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    for (int i = MAX_VERTICES; i < MAX_VERTICES + 4; i++) {
        LoadedVertex* v = &mRsp->loaded_vertices[i];
        v->color = mRdp->fill_color;
    }

    uint64_t saved_combine_mode = mRdp->combine_mode;

    if (mode == G_CYC_FILL) {
        GfxDpSetCombineMode(EncodeColorCombiner(0, 0, 0, G_CCMUX_SHADE), EncodeAlphaCombiner(0, 0, 0, G_ACMUX_SHADE), 0,
                            0);
    }

    GfxDrawRectangle(ulx, uly, lrx, lry);
    mRdp->combine_mode = saved_combine_mode;
}

void Interpreter::GfxDpSetZImage(void* zBufAddr) {
    mRdp->z_buf_address = zBufAddr;
}

void Interpreter::GfxDpSetColorImage(uint32_t format, uint32_t size, uint32_t width, void* address) {
    mRdp->color_image_address = address;
}

void Interpreter::GfxSpSetOtherMode(uint32_t shift, uint32_t num_bits, uint64_t mode) {
    uint64_t mask = (((uint64_t)1 << num_bits) - 1) << shift;
    uint64_t om = mRdp->other_mode_l | ((uint64_t)mRdp->other_mode_h << 32);
    om = (om & ~mask) | mode;
    mRdp->other_mode_l = (uint32_t)om;
    mRdp->other_mode_h = (uint32_t)(om >> 32);
}

void Interpreter::GfxDpSetOtherMode(uint32_t h, uint32_t l) {
    mRdp->other_mode_h = h;
    mRdp->other_mode_l = l;
}

void Interpreter::Gfxs2dexBgCopy(F3DuObjBg* bg) {
    /*
    bg->b.imageX = 0;
    bg->b.imageW = width * 4;
    bg->b.frameX = frameX * 4;
    bg->b.imageY = 0;
    bg->b.imageH = height * 4;
    bg->b.frameY = frameY * 4;
    bg->b.imagePtr = source;
    bg->b.imageLoad = G_BGLT_LOADTILE;
    bg->b.imageFmt = fmt;
    bg->b.imageSiz = siz;
    bg->b.imagePal = 0;
    bg->b.imageFlip = 0;
    */

    uintptr_t data = (uintptr_t)bg->b.imagePtr;

    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata = {};

    if ((bool)gfx_check_image_signature((char*)data)) {
        std::shared_ptr<Fast::Texture> tex = std::static_pointer_cast<Fast::Texture>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess((char*)data));
        texFlags = tex->Flags;
        rawTexMetadata.width = tex->Width;
        rawTexMetadata.height = tex->Height;
        rawTexMetadata.h_byte_scale = tex->HByteScale;
        rawTexMetadata.v_pixel_scale = tex->VPixelScale;
        rawTexMetadata.type = tex->Type;
        rawTexMetadata.resource = tex;
        data = (uintptr_t)reinterpret_cast<char*>(tex->ImageData);
    }

    s16 dsdx = 4 << 10;
    s16 uls = bg->b.imageX << 3;
    // Flip flag only flips horizontally
    if (bg->b.imageFlip == G_BG_FLAG_FLIPS) {
        dsdx = -dsdx;
        uls = (bg->b.imageW - bg->b.imageX) << 3;
    }

    SUPPORT_CHECK(bg->b.imageSiz == G_IM_SIZ_16b);
    GfxDpSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, nullptr, texFlags, rawTexMetadata, (void*)data);
    GfxDpSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    GfxDpLoadBlock(G_TX_LOADTILE, 0, 0, (bg->b.imageW * bg->b.imageH >> 4) - 1, 0);
    GfxDpSetTile(bg->b.imageFmt, G_IM_SIZ_16b, bg->b.imageW >> 4, 0, G_TX_RENDERTILE, bg->b.imagePal, 0, 0, 0, 0, 0, 0);
    GfxDpSetTileSize(G_TX_RENDERTILE, 0, 0, bg->b.imageW, bg->b.imageH);
    GfxDpTextureRectangle(bg->b.frameX, bg->b.frameY, bg->b.frameX + bg->b.imageW - 4, bg->b.frameY + bg->b.imageH - 4,
                          G_TX_RENDERTILE, uls, bg->b.imageY << 3, dsdx, 1 << 10, false);
}

void Interpreter::Gfxs2dexBg1cyc(F3DuObjBg* bg) {
    uintptr_t data = (uintptr_t)bg->b.imagePtr;

    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata = {};

    if ((bool)gfx_check_image_signature((char*)data)) {
        std::shared_ptr<Fast::Texture> tex = std::static_pointer_cast<Fast::Texture>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess((char*)data));
        texFlags = tex->Flags;
        rawTexMetadata.width = tex->Width;
        rawTexMetadata.height = tex->Height;
        rawTexMetadata.h_byte_scale = tex->HByteScale;
        rawTexMetadata.v_pixel_scale = tex->VPixelScale;
        rawTexMetadata.type = tex->Type;
        rawTexMetadata.resource = tex;
        data = (uintptr_t)reinterpret_cast<char*>(tex->ImageData);
    }

    // TODO: Implement bg scaling correctly
    s16 uls = bg->b.imageX >> 2;
    s16 lrs = bg->b.imageW >> 2;

    s16 dsdxRect = 1 << 10;
    s16 ulsRect = bg->b.imageX << 3;
    // Flip flag only flips horizontally
    if (bg->b.imageFlip == G_BG_FLAG_FLIPS) {
        dsdxRect = -dsdxRect;
        ulsRect = (bg->b.imageW - bg->b.imageX) << 3;
    }

    GfxDpSetTextureImage(bg->b.imageFmt, bg->b.imageSiz, bg->b.imageW >> 2, nullptr, texFlags, rawTexMetadata,
                         (void*)data);
    GfxDpSetTile(bg->b.imageFmt, bg->b.imageSiz, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    GfxDpLoadBlock(G_TX_LOADTILE, 0, 0, (bg->b.imageW * bg->b.imageH >> 4) - 1, 0);
    GfxDpSetTile(bg->b.imageFmt, bg->b.imageSiz, (((lrs - uls) * bg->b.imageSiz) + 7) >> 3, 0, G_TX_RENDERTILE,
                 bg->b.imagePal, 0, 0, 0, 0, 0, 0);
    GfxDpSetTileSize(G_TX_RENDERTILE, 0, 0, bg->b.imageW, bg->b.imageH);

    GfxDpTextureRectangle(bg->b.frameX, bg->b.frameY, bg->b.frameW, bg->b.frameH, G_TX_RENDERTILE, ulsRect,
                          bg->b.imageY << 3, dsdxRect, 1 << 10, false);
}

void Interpreter::Gfxs2dexRecyCopy(F3DuObjSprite* spr) {
    s16 dsdx = 4 << 10;
    [[maybe_unused]] s16 uls = spr->s.objX << 3;
    // Flip flag only flips horizontally
    if (spr->s.imageFlags == G_BG_FLAG_FLIPS) {
        dsdx = -dsdx;
        uls = (spr->s.imageW - spr->s.objX) << 3;
    }

    int realX = spr->s.objX >> 2;
    int realY = spr->s.objY >> 2;
    int realW = (((spr->s.imageW)) >> 5);
    int realH = (((spr->s.imageH)) >> 5);
    float realSW = spr->s.scaleW / 1024.0f;
    float realSH = spr->s.scaleH / 1024.0f;

    int testX = (realX + (realW / realSW));
    int testY = (realY + (realH / realSH));

    GfxDpTextureRectangle(realX << 2, realY << 2, testX << 2, testY << 2, G_TX_RENDERTILE,
                          (s32)mRdp->texture_tile[0].uls << 3, (s32)mRdp->texture_tile[0].ult << 3,
                          (float)(1 << 10) * realSW, (float)(1 << 10) * realSH, false);
}

} // namespace Fast
