#pragma once

#include <cstdint>

// Render-unification effort (kanban #131, plan "Full N64/3DS Render Pipeline Merge").
//
// UnifiedVtx is the ONE vertex layout both the N64 Fast3D combiner pipeline and the 3DS CMB model
// pipeline will feed once Phases 2/3 wire them through it. It is a strict superset of the two
// pipelines' current per-vertex data:
//   - N64 (LoadedVertex, interpreter.h) writes pos/uv0/color0 (CPU-baked Gouraud shade) and leaves
//     texClamp/color1-3/uv1/fog/bone* at their identity defaults (boneW = {255,0,0,0} = 100% bone 0).
//   - 3DS (CmbVertex, cmb3d/asset/cmb.h) writes pos/nrm/uv0/color0/boneIds/boneW and leaves
//     texClamp/color1-3/uv1/fog at their identity defaults.
//
// Phase 1 (current): this struct is dormant — landed but unreferenced by any live draw path.
// Phase 2/3 wire the CMB / N64 emitters to populate it; the two skinning MECHANISMS themselves
// are explicitly NOT unified (N64 stays CPU limb-walk writing identity bone data; 3DS stays GPU
// 4-bone blend) — see the plan's "Explicit non-goal".
struct UnifiedVtx {
    float pos[3];        // object-space (N64) or model-space (3DS) position
    float nrm[3];         // vertex normal; N64 content that has no real normal writes {0,0,1}
    float uv0[2];         // primary texture coordinate (texel0 for N64; the CMB's single UV set)
    float uv1[2];          // secondary texture coordinate (texel1 for N64 2-cycle combiners; unused by 3DS)
    float texClamp[4];    // {clampS0, clampT0, clampS1, clampT1} — mirrors N64's per-vertex
                            // aTexClampS/T attributes (gfx_sdl3gpu.cpp's o_clamp path); 3DS content
                            // that doesn't clamp writes the tex's full extent (no-op clamp).
    uint8_t color0[4];    // RGBA. N64: CPU-baked Gouraud shade (SHADER_INPUT_1 in the combiner).
                            // 3DS: per-vertex color (tinting); combined with lightingMode in the
                            // material, not baked at emit time like N64's.
    uint8_t color1[4];    // combiner SHADER_INPUT_2 slot (N64 prim/env/lod-frac source data that's
                            // genuinely per-vertex rather than per-draw-uniform); zeroed otherwise.
    uint8_t color2[4];    // combiner SHADER_INPUT_3 slot; zeroed otherwise.
    uint8_t color3[4];    // combiner SHADER_INPUT_4 slot; zeroed otherwise.
    float fog[2];          // {fogAmount, fogAlpha} — N64 fog blend factor; 3DS writes {0,0} (no fog).
    uint8_t boneIds[4];   // 3DS: up to 4 skin-matrix indices. N64: {0,0,0,0} (identity/no skin).
    uint8_t boneW[4];      // 3DS: per-bone blend weights (unorm8, sum to 255). N64: {255,0,0,0}.
};

static_assert(sizeof(UnifiedVtx) == 88, "UnifiedVtx layout changed — update the emitters in lockstep");
