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

// Zelda3D model draw opcode. Rather than drawing inline (interleaved with Fast3D, fighting its
// cached GL state), CAPTURE this draw — its model id, the interpreter's current MP_matrix
// snapshot, tint and clip params — into the Zelda3D draw list. The whole list is rendered later
// in one bracketed pass (OTR_G_ZELDA3D_RENDERPASS). Capturing MP here is essential: it's this
// item's matrix (set by the preceding gSPMatrix); the render-pass opcode comes later when
// MP_matrix is something else.
bool gfx_zelda3d_draw_handler_custom(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    F3DGfx* cmd = *cmd0;
    uint64_t w1 = (uint64_t)cmd->words.w1;
    int handle = (int)(uint32_t)w1; // low 32 bits = the draw handle (sign-extends bit 31 = lit)
    // The emitter packs a "lit" flag into the handle's high bit (modelIds are small): 1 = apply the
    // character/prop lighting term, 0 = scene geometry (keeps its baked vertex colours). Mask it off
    // to recover the model id.
    int sky = (handle >> 30) & 1; // bit 30 = skybox dome (far-plane depth, no shadow/AO)
    // bit 29 = force-unlit override (ZELDA3D_HANDLE_FORCE_UNLIT): ignore the CMB material's own
    // vertex_lighting flag for this draw so a self-illuminated overlay (title logo) isn't darkened
    // by the scene's ambient/world lighting term. See gbi.h comment above gSPZelda3DDrawUV.
    int forceUnlit = (handle >> 29) & 1;
    int lit = (handle < 0) && !forceUnlit; // bit 31 = lit (half-Lambert form term)
    // bit 28 = screen-space override (ZELDA3D_HANDLE_SCREEN_SPACE): this draw's MP already came
    // from a self-contained fixed-aspect ortho projection (title overlay), not the 3D camera's
    // N64-4:3-authored one — skip the widescreen aspect correction below. See gbi.h.
    int screenSpace = (handle >> 28) & 1;
    int modelId = handle & 0x0FFFFFFF;        // low 28 bits = model id (small)
    uint8_t a = (uint8_t)((w1 >> 32) & 0xFF); // w1[32:40] = per-draw alpha (255 = opaque)
    uint64_t w0 = (uint64_t)cmd->words.w0;
    uint32_t tint = (uint32_t)(w0 & 0xFFFFFF);
    uint8_t r = (tint >> 16) & 0xFF, g = (tint >> 8) & 0xFF, b = tint & 0xFF;
    // w0[32:48]=U, w0[48:64]=V: per-draw texcoord scroll offset, 16-bit fixed (value/65536 = UV).
    // Animates the OoT3D sky cloud band per its .cmab rate (#28b); 0 for every other draw.
    float uvOffU = (float)((w0 >> 32) & 0xFFFF) / 65536.0f;
    float uvOffV = (float)((w0 >> 48) & 0xFFFF) / 65536.0f;
    bool invertY = gfx->mRapi->GetClipParameters().invertY;
    // N64 vertices get `x = AdjXForAspectRatio(x)` per-vertex (see gfx_sp_vertex); our draw must
    // apply the SAME clip-X scale or the OoT3D scene shears vs the N64 actors as the camera pans —
    // EXCEPT for a screen-space overlay draw, whose own ortho box already IS the correct aspect
    // (see ZELDA3D_HANDLE_SCREEN_SPACE, gbi.h).
    float aspectAdj = screenSpace ? 1.0f : gfx->AdjXForAspectRatio(1.0f);
    // Modelview (no projection) for the view-space normal: top of the current modelview stack, the
    // same matrix MP_matrix was built from (gSPMatrix LOAD set it just before this opcode).
    const float* mv = &gfx->mRsp->modelview_matrix_stack[gfx->mRsp->modelview_matrix_stack_size - 1][0][0];
    // Flush Fast3D's pending (not-yet-appended) N64 triangle batch BEFORE appending this op, so the
    // two content streams interleave in true emission order in the unified op-list. Fast3D buffers
    // triangles in mBufVbo and only appends them as an op at a Flush() boundary (state changes /
    // frame end); without this, a G_ZELDA3D_DRAW firing mid-batch would jump ahead of (or behind)
    // still-buffered N64 geometry it was actually emitted after (or before) in the game's dlist.
    gfx->Flush();
    // TEMP DIAGNOSTIC (ZELDA3D_DBG_OVERLAY_MP=1): dump the P/MV/MP the overlay draws actually carry,
    // to root-cause the measured anisotropic overlay scale error (X~0.87, Y~0.72 of commanded).
    if (screenSpace) {
        static int sDbgOverlayMp = -1;
        if (sDbgOverlayMp < 0) {
            const char* v = getenv("ZELDA3D_DBG_OVERLAY_MP");
            sDbgOverlayMp = (v && *v == '1') ? 1 : 0;
        }
        if (sDbgOverlayMp) {
            const float* P = &gfx->mRsp->P_matrix[0][0];
            const float* MP = &gfx->mRsp->MP_matrix[0][0];
            fprintf(stderr,
                    "[OVERLAY_MP] model=%d P diag=(%.6f,%.6f,%.6f) P row3=(%.4f,%.4f,%.4f) "
                    "MV diag=(%.4f,%.4f,%.4f) MV trans=(%.2f,%.2f,%.2f) MP diag=(%.6f,%.6f) MP trans=(%.4f,%.4f)\n",
                    modelId, P[0], P[5], P[10], P[12], P[13], P[14], mv[0], mv[5], mv[10], mv[12], mv[13], mv[14],
                    MP[0], MP[5], MP[12], MP[13]);
        }
    }
    Zelda3D_GL_Submit(modelId, &gfx->mRsp->MP_matrix[0][0], mv, lit, invertY ? 1 : 0, r, g, b, a, aspectAdj, sky,
                      uvOffU, uvOffV, forceUnlit);
    return false;
}

// Zelda3D auto-scale measure bracket. Begin (w0 bit0 = 1): start accumulating the
// eye-space bbox in GfxSpVertex. End (bit0 = 0): finalize the bbox diagonal and report
// it to zelda3d.c keyed by w1, so it can derive the OoT3D model's world scale next frame.
bool gfx_zelda3d_measure_handler_custom(F3DGfx** cmd0) {
    F3DGfx* cmd = *cmd0;
    int key = (int)(intptr_t)cmd->words.w1;
    bool begin = (cmd->words.w0 & 0x1) != 0;
    GeometryObservationMeasureCommand(key, begin);
    return false;
}

// Zelda3D overlay depth-scope reset (#146 item B, gSPZelda3DClearDepth). No operands — flush any
// pending Fast3D batch first (same reasoning as gfx_zelda3d_draw_handler_custom: keeps this op's
// position in the unified stream exactly where the caller emitted it, relative to the surrounding
// G_ZELDA3D_DRAW ops), then bridge into the SG renderer's depth-only fullscreen reset. Bridged
// through the focused submission owner exactly like Zelda3D_GL_Submit, so this file
// stays free of a direct Zelda3D_Sg_* / ENABLE_SDL3GPU dependency.
bool gfx_zelda3d_cleardepth_handler_custom(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    (void)cmd0;
    gfx->Flush();
    Zelda3D_ClearOverlayDepth();
    return false;
}

// #205 — composite the native HUD's pending quads here. Same shape as the cleardepth handler above:
// flush the interpreter's own batched geometry first so the HUD ops land AFTER it in the op list,
// then hand off. Declared extern "C" so the interpreter keeps no direct Zelda3D_Hud_* dependency.
bool gfx_zelda3d_hudflush_handler_custom(F3DGfx** cmd0) {
    Interpreter* gfx = GetInterpreterInstance();
    (void)cmd0;
    gfx->Flush();
    Zelda3D_HostHudFlushPoint();
    return false;
}

} // namespace Fast
