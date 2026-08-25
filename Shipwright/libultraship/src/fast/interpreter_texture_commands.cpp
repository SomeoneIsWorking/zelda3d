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

#define C0(position, width) ((cmd->words.w0 >> (position)) & ((1U << (width)) - 1))
#define C1(position, width) ((cmd->words.w1 >> (position)) & ((1U << (width)) - 1))

namespace Fast {

bool gfx_set_timg_handler_rdp(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;
    uintptr_t i = (uintptr_t)gfx->SegAddr(cmd->words.w1);

    char* imgData = (char*)i;
    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetdata = {};
    // Default scale factors to 1 for raw N64 textures. OTR textures set these
    // from the resource, but raw textures would leave them at 0.
    rawTexMetdata.h_byte_scale = 1;
    rawTexMetdata.v_pixel_scale = 1;

    bool loadedOtrTex = false;
    if ((i & 1) != 1) {
        if (gfx_check_image_signature(imgData) == 1) {
            std::shared_ptr<Fast::Texture> tex = std::static_pointer_cast<Fast::Texture>(
                Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(imgData));

            if (tex == nullptr) {
                (*cmd0)++;
                return false;
            }

            loadedOtrTex = true;
            i = (uintptr_t)reinterpret_cast<char*>(tex->ImageData);
            texFlags = tex->Flags;
            rawTexMetdata.width = tex->Width;
            rawTexMetdata.height = tex->Height;
            rawTexMetdata.h_byte_scale = tex->HByteScale;
            rawTexMetdata.v_pixel_scale = tex->VPixelScale;
            rawTexMetdata.type = tex->Type;
            rawTexMetdata.resource = tex;
        }
    }

    // If the resolved address is still in the N64 segmented range, SegAddr
    // failed to resolve it (segment not set up). Skip to avoid dereferencing
    // invalid memory.
    // EXCEPTION: a successfully-loaded OTR texture's ImageData is a valid host pointer regardless of
    // its numeric value — on systems whose heap sits below 0x10000000 (e.g. a low-mmap'd process)
    // ImageData can legitimately be <= 0x0FFFFFFF, and treating that as "unresolved" wrongly drops
    // the texture (this is how actor-segmented eye/mouth face textures rendered as a VOID). Only the
    // raw/unresolved path (no OTR load) can still be a stale segmented address, so gate on that.
    // For Windows, also check if the address is not from a dll because this validation returns a false positive caused
    // by how the virtual memory is allocated.
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!loadedOtrTex && i <= 0x0FFFFFFF &&
        !(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(i), &module))) {
        return false;
    }
#else
    if (!loadedOtrTex && i <= 0x0FFFFFFF) {
        // DIAGNOSTIC (Zelda3D): the "sky bug" — a non-deterministic scene-load race where a texture's
        // N64 segment base is still 0 when its display list first runs, so SegAddr can't resolve it.
        // The texture is skipped here, leaving a stale GL binding that paints garbage (a HUD icon, a
        // sky stripe). The downstream null guards (GfxDpLoadTlut/ImportTexture) only prevent the
        // crash. This logs WHICH draw is the culprit: the segment number + the OPEN_DISPS breadcrumb
        // (innermost game code file:line) so the next occurrence is identifiable. Capped to avoid
        // spam; only ever fires on the (rare) unresolved-segment path.
        static int reported = 0;
        if (reported < 64) {
            reported++;
            uintptr_t w1 = cmd->words.w1;
            uint32_t segNum = (w1 & 1) ? (uint32_t)(w1 >> 24) : 0xFFFFFFFFu;
            std::string crumb;
            const auto& disp = g_exec_stack.getDisp();
            for (size_t k = disp.size(); k-- > 0 && crumb.size() < 240;) {
                crumb += disp[k].file ? disp[k].file : "?";
                crumb += ":" + std::to_string(disp[k].line);
                if (k)
                    crumb += " <- ";
            }
            SPDLOG_WARN("Zelda3D SKYBUG: unresolved texture segment {} (w1=0x{:08X}, resolved=0x{:016X}) "
                        "skipped; drawn by [{}]",
                        segNum, (uint32_t)w1, (uint64_t)i, crumb.empty() ? "(no OPEN_DISPS context)" : crumb);
        }
        return false;
    }
#endif

    gfx->GfxDpSetTextureImage(C0(21, 3), C0(19, 2), C0(0, 12) + 1, imgData, texFlags, rawTexMetdata, (void*)i);

    return false;
}

bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {
    uintptr_t addr = (*cmd0)->words.w1;
    (*cmd0)++;
    uint64_t hash = ((uint64_t)(*cmd0)->words.w0 << 32) + (uint64_t)(*cmd0)->words.w1;

    const char* fileName =
        Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->HashToCString(hash);
    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata = {};

    if (fileName == nullptr) {
        (*cmd0)++;
        return false;
    }

    std::shared_ptr<Fast::Texture> texture = std::static_pointer_cast<Fast::Texture>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(
            Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->HashToCString(hash)));
    if (texture != nullptr) {
        texFlags = texture->Flags;
        rawTexMetadata.width = texture->Width;
        rawTexMetadata.height = texture->Height;
        rawTexMetadata.h_byte_scale = texture->HByteScale;
        rawTexMetadata.v_pixel_scale = texture->VPixelScale;
        rawTexMetadata.type = texture->Type;
        rawTexMetadata.resource = texture;

        // OTRTODO: We have disabled caching for now to fix a texture corruption issue with HD texture
        // support. In doing so, there is a potential performance hit since we are not caching lookups. We
        // need to do proper profiling to see whether or not it is worth it to keep the caching system.

        char* tex = reinterpret_cast<char*>(texture->ImageData);

        if (tex != nullptr) {
            (*cmd0)--;
            uintptr_t oldData = (*cmd0)->words.w1;
            // TODO: wtf??
            (*cmd0)->words.w1 = (uintptr_t)tex;

            // if (ourHash != (uint64_t)-1) {
            //     auto res = ResourceLoad(ourHash);
            // }

            (*cmd0)++;
        }

        (*cmd0)--;
        F3DGfx* cmd = (*cmd0);
        uint32_t fmt = C0(21, 3);
        uint32_t size = C0(19, 2);
        uint32_t width = C0(0, 12) + 1;

        if (tex != NULL) {
            Interpreter* gfx = GetInterpreterInstance();
            gfx->GfxDpSetTextureImage(fmt, size, width, fileName, texFlags, rawTexMetadata, tex);
        }
    } else {
        SPDLOG_ERROR("G_SETTIMG_OTR_HASH: Texture is null");
    }

    (*cmd0)++;
    return false;
}

bool gfx_set_timg_otr_filepath_handler_custom(F3DGfx** cmd0) {
    F3DGfx* cmd = *cmd0;
    const char* fileName = (char*)cmd->words.w1;

    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata = {};

    std::shared_ptr<Fast::Texture> texture = std::static_pointer_cast<Fast::Texture>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(fileName));
    if (texture != nullptr) {
        Interpreter* gfx = GetInterpreterInstance();
        texFlags = texture->Flags;
        rawTexMetadata.width = texture->Width;
        rawTexMetadata.height = texture->Height;
        rawTexMetadata.h_byte_scale = texture->HByteScale;
        rawTexMetadata.v_pixel_scale = texture->VPixelScale;
        rawTexMetadata.type = texture->Type;
        rawTexMetadata.resource = texture;

        uint32_t fmt = C0(21, 3);
        uint32_t size = C0(19, 2);
        uint32_t width = C0(0, 12) + 1;

        gfx->GfxDpSetTextureImage(fmt, size, width, fileName, texFlags, rawTexMetadata,
                                  reinterpret_cast<char*>(texture->ImageData));
    } else {
        SPDLOG_ERROR("G_SETTIMG_OTR_FILEPATH: Texture is null");
    }
    return false;
}
bool gfx_register_blended_texture_handler_custom(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    // Flush incase we are replacing a previous blended texture that hasn't been finialized to the GPU
    gfx->Flush();

    char* timg = (char*)cmd->words.w1;

    ++(*cmd0);
    cmd = *cmd0;

    uint8_t* mask = (uint8_t*)cmd->words.w0;
    uint8_t* replacementTex = (uint8_t*)cmd->words.w1;

    if (!gfx_check_image_signature(timg)) {
        SPDLOG_ERROR(
            "OTR_G_REGBLENDEDTEX: Texture is not a valid OTR resource name, unable to register blended texture");
        return false;
    }

    // With no mask, we should clear the blended texture
    if (mask == nullptr) {
        gfx->UnregisterBlendedTexture(timg);
    } else {
        gfx->RegisterBlendedTexture(timg, mask, replacementTex);
    }

    return false;
}

bool gfx_set_timg_fb_handler_custom(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    gfx->Flush();
    gfx->mRapi->SelectTextureFb((uint32_t)cmd->words.w1);
    gfx->mRdp->textures_changed[0] = false;
    gfx->mRdp->textures_changed[1] = false;
    return false;
}

bool gfx_load_block_handler_rdp(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    gfx->GfxDpLoadBlock(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
    return false;
}

bool gfx_load_block_wide_handler_rdp(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    uint32_t tile = cmd->words.w0 & 0x7;
    uint32_t lrs = cmd->words.w1;

    (*cmd0)++;
    cmd = *cmd0;

    uint32_t uls = (cmd->words.w0 >> 16) & 0xFFFF;
    uint32_t ult = (cmd->words.w0 >> 0) & 0xFFFF;
    uint32_t dxt = (cmd->words.w1 >> 0) & 0xFFF;

    gfx->GfxDpLoadBlock(tile, uls, ult, lrs, dxt);
    return false;
}

bool gfx_load_tile_handler_rdp(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    gfx->GfxDpLoadTile(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
    return false;
}

bool gfx_set_tile_handler_rdp(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    gfx->GfxDpSetTile(C0(21, 3), C0(19, 2), C0(9, 9), C0(0, 9), C1(24, 3), C1(20, 4), C1(18, 2), C1(14, 4), C1(10, 4),
                      C1(8, 2), C1(4, 4), C1(0, 4));
    return false;
}

bool gfx_set_tile_size_handler_rdp(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    gfx->GfxDpSetTileSize(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
    return false;
}

bool gfx_set_tile_size_interp_handler_rdp(F3DGfx** cmd0) {
    F3DGfx* cmd = *cmd0;
    Interpreter* gfx = GetInterpreterInstance();

    if (gfx->mInterpolationIndex == gfx->mInterpolationIndexTarget) {
        int tile = C1(24, 3);
        gfx->GfxDpSetTileSize(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
        ++(*cmd0);
        memcpy(&gfx->mRdp->texture_tile[tile].uls, &(*cmd0)->words.w0, sizeof(float));
        memcpy(&gfx->mRdp->texture_tile[tile].ult, &(*cmd0)->words.w1, sizeof(float));
        ++(*cmd0);
        memcpy(&gfx->mRdp->texture_tile[tile].lrs, &(*cmd0)->words.w0, sizeof(float));
        memcpy(&gfx->mRdp->texture_tile[tile].lrt, &(*cmd0)->words.w1, sizeof(float));
    } else {
        ++(*cmd0);
        ++(*cmd0);
    }

    return false;
}

bool gfx_set_interpolation_index_target(F3DGfx** cmd0) {
    F3DGfx* cmd = *cmd0;
    Interpreter* gfx = GetInterpreterInstance();

    gfx->mInterpolationIndexTarget = cmd->words.w1;
    return false;
}

bool gfx_load_tlut_handler_rdp(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;

    gfx->GfxDpLoadTlut(C1(24, 3), C1(14, 10));
    return false;
}

} // namespace Fast
