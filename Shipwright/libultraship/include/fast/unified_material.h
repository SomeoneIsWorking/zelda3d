#pragma once

#include <cstdint>

// Render-unification effort (kanban #131). UnifiedMaterial generalizes both the N64 color-combiner
// (CCFeatures.c[2][2][4] in interpreter.h, decoded by gfx_cc_get_features) and the 3DS CmbMaterial
// (cmb3d/asset/cmb.h) into one per-draw-uniform struct used by both unified draw routes.
//
// combMux uses the SAME integer codes as interpreter.h's SHADER_* enum (SHADER_0=0 ...
// SHADER_NOISE=14) so N64's CCFeatures.c[2][2][4] can be copied into combMux verbatim in Phase 3 —
// no translation table needed. Indexing is combMux[cycle][rgbOrAlpha][operand], operand 0-3 = the
// general combiner formula's (A - B) * C + D, matching interpreter.h's ColorCombinerKey semantics
// (see gfx_sdl3gpu.cpp's sg_append_formula): "single"/"multiply"/"mix" are not distinct formulas,
// just special cases of this one (B=0,C=1,D=0 / B=0,D=0 / D=B respectively), so the unified
// evaluator always computes the general form — one code path instead of three.
struct UnifiedMaterial {
    int combMux[2][2][4]; // [cycle 0-1][0=rgb,1=alpha][A,B,C,D] — SHADER_* operand codes
    int cycleCount;       // 1 (3DS: comb_stage_count) or 2 (N64: opt_2cyc)

    float primColor[4];
    float envColor[4];
    float blendColor[4]; // 3DS CmbMaterial.blend_color
    float fogColor[4];

    // Fixed-function state (N64: baked into the GL/Vulkan pipeline per shader_id today; 3DS:
    // CmbMaterial fields verbatim).
    bool alphaTest;
    float alphaRef;
    bool blendEnable;
    uint8_t blendSrcRgb, blendDstRgb, blendSrcA, blendDstA; // GL blend-factor enums
    uint8_t blendEqRgb, blendEqA;                           // GL blend-equation enums
    uint8_t cullMode;                                       // 0 = none, 1 = front, 2 = back
    bool depthWrite;
    float polygonOffset;  // decal bias (3DS CmbMaterial.polygon_offset)
    uint8_t wrapS, wrapT; // GL wrap-mode enums, texture 0 (texture 1 wraps identically today)

    // Lighting (see the plan's "Unified lighting" section).
    float matAmbient[3];  // 3DS CmbMaterial.mat_ambient
    float matDiffuse[3];  // 3DS CmbMaterial.mat_diffuse
    uint8_t lightingMode; // 0 = N64 prebaked-Gouraud passthrough (UnifiedVtx.color0 is final shade)
                          // 1 = 3DS character half-Lambert (hl = N·L*0.5+0.5)
                          // 2 = 3DS scene vertex-lit (ambient*matAmbient + diffuse*matDiffuse*N·L)

    // true (N64): UnifiedVtx.pos is already clip-space (CPU-transformed by the emulated RSP
    // pipeline, guard-band clipping included) — gl_Position = pos verbatim, no MVP multiply.
    // false (3DS): UnifiedVtx.pos is model-space — gl_Position = uMvp * vec4(pos.xyz, 1.0), the
    // GPU does the transform. See unified_vtx.h's pos field doc for why this split is necessary.
    bool alreadyTransformed;
};
