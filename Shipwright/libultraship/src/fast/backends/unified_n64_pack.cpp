#include "fast/backends/unified_n64_pack.h"
#include "fast/interpreter.h" // CCFeatures

#include <cstring>

// See unified_n64_pack.h. Dormant — unreferenced by any live draw path.
UnifiedMaterial Fast_PackCCFeaturesToUnifiedMaterial(const CCFeatures& cc) {
    UnifiedMaterial m{};
    // Direct copy: CCFeatures.c[2][2][4] and UnifiedMaterial.combMux[2][2][4] use the identical
    // SHADER_* operand codes and [cycle][rgbOrAlpha][A,B,C,D] indexing by construction.
    std::memcpy(m.combMux, cc.c, sizeof(m.combMux));
    m.cycleCount = cc.opt_2cyc ? 2 : 1;

    m.primColor[0] = m.primColor[1] = m.primColor[2] = m.primColor[3] = 1.0f;
    m.envColor[0] = m.envColor[1] = m.envColor[2] = m.envColor[3] = 0.0f;
    m.blendColor[0] = m.blendColor[1] = m.blendColor[2] = 0.0f;
    m.blendColor[3] = 1.0f;
    m.fogColor[0] = m.fogColor[1] = m.fogColor[2] = m.fogColor[3] = 0.0f;

    // Both are FIXED thresholds in the old shader (gfx_sdl3gpu.cpp's kSgShaderTemplate), not a
    // real per-material alphaRef: opt_alpha_threshold discards a<8/256; opt_texture_edge instead
    // snaps a=1 above 0.19 or discards below it (a different rule, not just a different constant).
    // Encoded via alphaRef's sign — the unified fragment shader's alphaTest branch treats a
    // negative value as "texture-edge mode" (see unified_shader.cpp). CMB materials use this same
    // field for a REAL per-material alpha_ref (cmb.h), always >= 0, so the sentinel is unambiguous.
    m.alphaTest = cc.opt_alpha_threshold || cc.opt_texture_edge;
    m.alphaRef = cc.opt_texture_edge ? -1.0f : (8.0f / 256.0f);

    // Fixed-function state (blend/depth/cull) is NOT derivable from CCFeatures alone today — the
    // old N64 path bakes it into the GL/Vulkan pipeline from separate other_mode_l/h decode
    // (SetDepthTestAndMask/SetZmodeDecal/SetUseAlpha), not from the combiner. Left at safe
    // opaque-passthrough defaults; a real Phase 3 wiring must source these the same way
    // GetOrCreatePipeline's stateBits does.
    m.blendEnable = false;
    m.blendSrcRgb = 1 /*GL_ONE*/; m.blendDstRgb = 0 /*GL_ZERO*/;
    m.blendSrcA = 1; m.blendDstA = 0;
    m.blendEqRgb = m.blendEqA = 0x8006 /*GL_FUNC_ADD*/;
    m.cullMode = 0;
    m.depthWrite = true;
    m.polygonOffset = 0.0f;
    m.wrapS = m.wrapT = 0x2901 /*GL_REPEAT*/;

    m.matAmbient[0] = m.matAmbient[1] = m.matAmbient[2] = 1.0f;
    m.matDiffuse[0] = m.matDiffuse[1] = m.matDiffuse[2] = 1.0f;
    m.lightingMode = 0; // N64 prebaked-Gouraud passthrough — UnifiedVtx.color0 is already the shade
    return m;
}
