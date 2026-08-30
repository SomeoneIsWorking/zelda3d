// Zelda3D SDL3GPU shader and graphics-pipeline cache ownership.
#ifdef ENABLE_SDL3GPU

#include "zelda3d_sdl3gpu_internal.h"
#include "zelda3d_sdl3gpu_shaders.h"

#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/zelda3d_sdl3gpu.h"
#include "fast/backends/zelda3d_tev_glsl.h"
#include "fast/zelda3d_sg_ubo.h"
#include "fast/unified_vtx.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Fast::g_activeSdl3GpuApi;
using Fast::GfxRenderingAPISdl3Gpu;
using Fast::PipeKey;
using Fast::SgGroup;

extern "C" int gZelda3dFaceCull;

namespace {

int sgFaceCullOn() {
    if (gZelda3dFaceCull < 0) {
        const char* value = getenv("ZELDA3D_FACECULL");
        gZelda3dFaceCull = (value != nullptr && value[0] == '0') ? 0 : 1;
    }
    return gZelda3dFaceCull;
}

// GL blend enum -> SDL3 GPU blend factor.
SDL_GPUBlendFactor mapFactor(unsigned f) {
    switch (f) {
        case 0:
            return SDL_GPU_BLENDFACTOR_ZERO;
        case 1:
            return SDL_GPU_BLENDFACTOR_ONE;
        case 0x300:
            return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case 0x301:
            return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case 0x302:
            return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case 0x303:
            return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x304:
            return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case 0x305:
            return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case 0x306:
            return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case 0x307:
            return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case 0x308:
            return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
        // The four GL constant-blend factors. SDL3 GPU only exposes the *_CONSTANT_COLOR pair, so
        // GL_CONSTANT_ALPHA / GL_ONE_MINUS_CONSTANT_ALPHA also map here — the alpha-broadcast is
        // done on the CONSTANTS side by sgBlendConstants() below, which pushes (a,a,a,a) instead
        // of (r,g,b,a). That is bit-exact: GL_CONSTANT_ALPHA's factor is (Ac,Ac,Ac,Ac).
        case 0x8001:
            return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR; // GL_CONSTANT_COLOR
        case 0x8002:
            return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR; // GL_ONE_MINUS_CONSTANT_COLOR
        case 0x8003:
            return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR; // GL_CONSTANT_ALPHA
        case 0x8004:
            return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR; // GL_ONE_MINUS_CONSTANT_ALPHA
        default:
            return SDL_GPU_BLENDFACTOR_ONE;
    }
}

// GL depth compare enum (CMB mat+0x136) -> SDL3 GPU compare op. The renderer used to hardcode
// LESS_OR_EQUAL for every CMB draw, which is wrong for 11153 of the ROM's 11172 materials: 11147
// specify LESS, 4 ALWAYS, 2 GEQUAL, and only 19 actually want LEQUAL. LESS vs LEQUAL decides who
// wins at EQUAL depth, so forcing LEQUAL lets coplanar fragments OoT3D rejects draw here -- the
// file's own polygon_offset field is what kept that mostly latent. The overlay-depth pass
// (ensureOverlayDepthResources) is NOT a material pipeline and deliberately keeps its own ALWAYS.
SDL_GPUCompareOp mapDepthFunc(unsigned gl) {
    switch (gl) {
        case 0x0200:
            return SDL_GPU_COMPAREOP_NEVER;
        case 0x0201:
            return SDL_GPU_COMPAREOP_LESS;
        case 0x0202:
            return SDL_GPU_COMPAREOP_EQUAL;
        case 0x0203:
            return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        case 0x0204:
            return SDL_GPU_COMPAREOP_GREATER;
        case 0x0205:
            return SDL_GPU_COMPAREOP_NOT_EQUAL;
        case 0x0206:
            return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        case 0x0207:
            return SDL_GPU_COMPAREOP_ALWAYS;
        default:
            return SDL_GPU_COMPAREOP_LESS;
    }
}

SDL_GPUBlendOp mapEq(unsigned e) {
    switch (e) {
        case 0x8006:
            return SDL_GPU_BLENDOP_ADD;
        case 0x800A:
            return SDL_GPU_BLENDOP_SUBTRACT;
        case 0x800B:
            return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        case 0x8007:
            return SDL_GPU_BLENDOP_MIN;
        case 0x8008:
            return SDL_GPU_BLENDOP_MAX;
        default:
            return SDL_GPU_BLENDOP_ADD;
    }
}

} // namespace

// True when any of the group's four blend factors is a constant factor; fills `out` with the
// vector SDL_SetGPUBlendConstants must carry for this draw.
//
// GL's alpha-constant factors broadcast blend_color.a to all four channels, the colour-constant
// ones use blend_color verbatim — but there is ONE blend-constant register per draw, so a
// material that mixed the two forms would be unrepresentable. It cannot happen in the OoT3D
// corpus: a full scan of all 11172 CMB materials (1511 blend-enabled) finds constant factors in
// exactly 91, all of them dstRGB = GL_CONSTANT_ALPHA (srcRGB 0x300 x83 / 0x302 x8, srcA = ONE,
// dstA = ZERO) — zero colour-form uses, zero mixed materials. The warning below exists so a
// future asset that breaks that never fails silently.
bool Fast::Zelda3DSdl3GpuPipeline::BlendConstants(const SgGroup& g, SDL_FColor& out) {
    if (!g.blendEnable)
        return false;
    const unsigned f[4] = { g.bSrcRGB, g.bDstRGB, g.bSrcA, g.bDstA };
    bool colorForm = false, alphaForm = false;
    for (unsigned v : f) {
        if (v == 0x8001 || v == 0x8002)
            colorForm = true;
        else if (v == 0x8003 || v == 0x8004)
            alphaForm = true;
    }
    if (!colorForm && !alphaForm)
        return false;
    if (colorForm && alphaForm) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr, "[Zelda3D_SG] material mixes GL_CONSTANT_COLOR and GL_CONSTANT_ALPHA blend "
                            "factors; SDL3 GPU has one blend-constant register — using the alpha form\n");
        }
    }
    const float a = g.blendColor[3];
    if (alphaForm)
        out = SDL_FColor{ a, a, a, a };
    else
        out = SDL_FColor{ g.blendColor[0], g.blendColor[1], g.blendColor[2], a };
    return true;
}

// CMB materials never exercise N64's 2-cycle/fog/grayscale combiner shapes. Preserve the legacy
// CMB dual-texture modes as a structural two-sampler variant; routing them through kSingleTex drops
// the second binding and bypasses the game-side title-logo TEV override entirely.
Fast::Unified::Variant Fast::Zelda3DSdl3GpuPipeline::VariantForGroup(const SgGroup& g, bool hasTex) {
    if (!hasTex)
        return Fast::Unified::Variant::kUntextured;
    if (g.tevGeneric)
        return Fast::Unified::Variant::kGenericTev;
    if (g.dualTexMode)
        return Fast::Unified::Variant::kDualTex;
    return g.alphaTest ? Fast::Unified::Variant::kSingleTexAlphaTest : Fast::Unified::Variant::kSingleTex;
}

bool Fast::Zelda3DRenderer::ensureResources() {
    if (g_resReady)
        return true;
    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;
    if (!api)
        return false;
    g_device = api->GpuDevice();
    if (!g_device)
        return false;

    // ZELDA3D_SG_FRAGDBG=<1..15>: replace the combiner with an isolated stage so a black/missing draw is
    // diagnosed by VALUE — 1=texture only, 2=raw vertex colour, 3=solid white, 4=shade(flat tint),
    // 5=PRIMARY_COLOR (the vertex-lit o1 the TEV consumes; the direct counterpart of the oracle's
    // `PIXEL ... primary=` probe). Applied to EVERY Zelda3D draw unconditionally (unlike a per-draw
    // uniform gate), so the readback is trustworthy. Measure with the REPL `region` tool, not by eye.
    // The anchor is the line that OPENS the combiner section: it must sit after `prim` is computed and
    // before the TEV/CONSTANT/stage-scale/fog stages, so the probe is not repainted by fog. (It used to
    // anchor on `vec3 rgb = t.rgb * vColor.rgb * shade;`, a line the lighting-port rewrite deleted —
    // FRAGDBG had been silently INERT ever since; found 2026-07-22.)
    // FRAGDBG picks ONE of the template's named taps. The mode->snippet table below is unchanged;
    // what changed is how it lands. This used to be fragSrc.find() on a literal source LINE, and when
    // the lighting-port rewrite deleted the line it matched, the probe silently reported nothing and
    // read as a clean result for weeks. A named hole cannot drift out from under its filler, and the
    // fill is verified after rendering rather than assumed.
    const char* tapCombiner = "";
    const char* tapPreFog = "";
    int fragdbgMode = 0;
    std::string guardedProbe;
    int fragdbgDraw = -1;
    if (const char* draw = getenv("ZELDA3D_SG_FRAGDBG_DRAW")) {
        fragdbgDraw = atoi(draw);
        if (fragdbgDraw < 0)
            fragdbgDraw = -1;
    }
    if (const char* dbg = getenv("ZELDA3D_SG_FRAGDBG")) {
        fragdbgMode = atoi(dbg);
        const int mode = fragdbgMode;
        // ALL taps sit AFTER the combiner block, so the material's real ALPHA TEST (the tevG
        // path discards on the FINAL combiner alpha) still runs: an earlier anchor made
        // alpha-tested foliage paint solid over the ground and silently corrupted every
        // mask-restricted readback taken from a probe frame (found 2026-07-22, after a
        // "our texture is 15% dark" reading that the fog A/B falsified).
        // Each snippet returns IMMEDIATELY so the override bypasses the later combiner/ambient/FOG
        // stages — otherwise the fog mix (fog colour ~= the scene tan) repaints the probe.
        const char* inject =
            mode == 1   ? "frag = vec4(t.rgb, 1.0); return;\n"
            : mode == 2 ? "frag = vec4(vColor.rgb, 1.0); return;\n"
            : mode == 3 ? "frag = vec4(1.0); return;\n"
            : mode == 4 ? "frag = vec4(shade, 1.0); return;\n"
            : mode == 5 ? "frag = vec4(prim.rgb, 1.0); return;\n"
            // Mode 8 is the COLOUR-SPACE RAMP: known constants (0.25, 0.5, 0.75) out of
            // the shader, so a readback says what the path between here and the PNG does
            // to a value. It exists because every ours-vs-oracle number is compared
            // against Azahar's software rasterizer, whose PIXEL lines are raw linear
            // 8-bit — and an assumed-but-unvalidated sRGB conversion on our side would
            // silently reassign the divergence to the wrong channel. Linear passthrough
            // reads (64,128,191); sRGB encoding on write reads about (137,188,225).
            : mode == 8 ? "frag = vec4(0.25, 0.5, 0.75, 1.0); return;\n"
            // Modes 9/10 expose the actual GPU-visible TEV constant slots. They
            // distinguish CPU-side override success from a UBO packing/readback bug.
            : mode == 9  ? "frag = unpackUnorm4x8(ubo.uTevConst[0][1]); return;\n"
            : mode == 10 ? "frag = unpackUnorm4x8(ubo.uTevConst[0][2]); return;\n"
            : mode == 11 ? "uint w=ubo.uTevStages[0].x; "
                           "frag=vec4(float(w&15u)/15.0,float((w>>4)&15u)/15.0,float((w>>8)&15u)/15.0,1); return;\n"
            : mode == 12 ? "uint w=ubo.uTevStages[1].x; "
                           "frag=vec4(float(w&15u)/15.0,float((w>>4)&15u)/15.0,float((w>>8)&15u)/15.0,1); return;\n"
            : mode == 13 ? "frag=vec4(ubo.uTevCtl.x/6.0,ubo.uTevCtl.y/4.0,ubo.uTevCtl.z/4.0,1); return;\n"
            // Mode 14 exposes the secondary TEV sample. It is intentionally a direct sampler
            // read here because t1s is scoped to the generic-TEV block below; dummy-bound draws
            // therefore remain visible as white rather than silently producing an absent probe.
            : mode == 14 ? "frag=vec4(texture(uTex1,vUv1).rgb,1.0); return;\n"
            // Mode 15 exposes the same secondary sample with PICA's software-oracle addressing:
            // integer texel coordinates, rather than the GPU's filtered normalized lookup. This
            // is a discriminator only; the shipping sampler remains mode 14's authored filter.
            : mode == 15 ? "ivec2 sz=textureSize(uTex1,0); ivec2 p=clamp(ivec2(vUv1*vec2(sz)),ivec2(0),sz-1); "
                           "frag=vec4(texelFetch(uTex1,p,0).rgb,1.0); return;\n"
                         : "";
        // With no selector the historical probe remains whole-frame. With a selector, only the
        // selected draw takes the early-return tap; all other draws continue through their normal
        // combiner while retaining depth, blending, and ordering context for that draw.
        if (fragdbgDraw >= 0 && *inject) {
            guardedProbe = "if (ubo.uDebug.x > 0.5) { gl_FragDepth = 0.0;\n";
            guardedProbe += inject;
            guardedProbe += "}\n";
            inject = guardedProbe.c_str();
        }
        // Mode 6 taps the combiner result before the CONSTANT/stage-scale/FOG stages — the direct
        // counterpart of the oracle's `PIXEL ... combined=` field. Mode 7 taps after the CONSTANT +
        // stage-scale stages, immediately BEFORE fog.
        if (mode == 7) {
            tapPreFog = "    frag = vec4(rgb, 1.0); return;\n";
        } else if (mode == 6) {
            tapCombiner = "    frag = vec4(rgb, 1.0); return;\n";
        } else {
            tapCombiner = inject;
        }
        if (fragdbgDraw >= 0 && (mode == 6 || mode == 7)) {
            guardedProbe = "if (ubo.uDebug.x > 0.5) { gl_FragDepth = 0.0;\n";
            guardedProbe += mode == 7 ? tapPreFog : tapCombiner;
            guardedProbe += "}\n";
            if (mode == 7)
                tapPreFog = guardedProbe.c_str();
            else
                tapCombiner = guardedProbe.c_str();
        }
    }

    std::string vertSrc, fragSrc;
    std::string templateError;
    if (!Fast::Zelda3DSdl3GpuShaders::BuildSources(Fast::Zelda3DTev::kGenericFunctions, tapCombiner, tapPreFog, vertSrc,
                                                   fragSrc, templateError)) {
        fprintf(stderr, "[Zelda3D_SG] shader template render FAILED: %s\n", templateError.c_str());
        return false;
    }

    // Verify the tap actually landed. The whole point of moving off find(anchor) was that a probe
    // which quietly injects nothing is indistinguishable from a probe that found no signal.
    if (fragdbgMode != 0) {
        const char* want = *tapPreFog ? tapPreFog : tapCombiner;
        if (*want && fragSrc.find(want) == std::string::npos) {
            fprintf(stderr, "[Zelda3D_SG] FRAGDBG mode=%d: TAP DID NOT LAND — probe inert\n", fragdbgMode);
        } else {
            if (fragdbgDraw >= 0)
                fprintf(stderr, "[Zelda3D_SG] FRAGDBG mode=%d active draw=%d\n", fragdbgMode, fragdbgDraw);
            else
                fprintf(stderr, "[Zelda3D_SG] FRAGDBG mode=%d active\n", fragdbgMode);
        }
    }

    // ZELDA3D_DUMP_SHADERS=<dir> writes the FINAL GLSL actually handed to the compiler. The shader
    // is assembled from templates and then patched further by FRAGDBG, so "what the source file
    // says" and "what the GPU ran" are not the same text — this dumps the latter, which is the only
    // version worth diffing when a shader refactor has to prove it changed nothing.
    if (const char* dumpDir = getenv("ZELDA3D_DUMP_SHADERS")) {
        auto dump = [&](const char* name, const char* text) {
            const std::string path = std::string(dumpDir) + "/" + name;
            if (FILE* f = fopen(path.c_str(), "wb")) {
                fwrite(text, 1, strlen(text), f);
                fclose(f);
                fprintf(stderr, "[Zelda3D_SG] dumped shader -> %s\n", path.c_str());
            } else {
                fprintf(stderr, "[Zelda3D_SG] CANNOT write shader dump %s\n", path.c_str());
            }
        };
        dump("zelda3d_sg.vert", vertSrc.c_str());
        dump("zelda3d_sg.frag", fragSrc.c_str());
    }

    std::vector<uint32_t> vsSpv, fsSpv;
    if (!Fast::Zelda3DSdl3GpuShaders::Compile(EShLangVertex, vertSrc.c_str(), vsSpv) ||
        !Fast::Zelda3DSdl3GpuShaders::Compile(EShLangFragment, fragSrc.c_str(), fsSpv))
        return false;

    SDL_GPUShaderCreateInfo vci{};
    vci.code_size = vsSpv.size() * sizeof(uint32_t);
    vci.code = (const Uint8*)vsSpv.data();
    vci.entrypoint = "main";
    vci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vci.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    // kVert declares TWO vertex uniform buffers: set=1 binding=0 UBO (common) + set=1 binding=1
    // UBOBones (bone matrices for GPU skinning). This count is the pipeline-layout's vertex uniform
    // descriptor count and MUST match the shader, or binding 1 (bones) has no descriptor slot: the
    // Vulkan validation layer flags it (VUID-VkGraphicsPipelineCreateInfo-layout-07988 +
    // "Set 1, Binding 1, bones is invalid") and the bones descriptor is dereferenced unbacked at draw
    // time — a stale/garbage read that lavapipe-serial tolerates but MoltenVK and multi-threaded
    // lavapipe fault on (the headless SKYBUG crash / the macOS BindFragmentSamplers crash).
    vci.num_uniform_buffers = 2;
    g_vert = SDL_CreateGPUShader(g_device, &vci);

    SDL_GPUShaderCreateInfo fci{};
    fci.code_size = fsSpv.size() * sizeof(uint32_t);
    fci.code = (const Uint8*)fsSpv.data();
    fci.entrypoint = "main";
    fci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fci.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fci.num_samplers = 3;        // uTex + uShadowMap + uTex1 (dual-texture detail mask)
    fci.num_uniform_buffers = 1; // UBO
    g_frag = SDL_CreateGPUShader(g_device, &fci);

    if (!g_vert || !g_frag) {
        fprintf(stderr, "[Zelda3D_SG] shader create failed: %s\n", SDL_GetError());
        return false;
    }

    g_resReady = true;
    fprintf(stderr, "[Zelda3D_SG] resources ready (unified op model)\n");
    return true;
}

SDL_GPUGraphicsPipeline* Fast::Zelda3DRenderer::getPipeline(const SgGroup& g, int frontCW) {
    bool doCull = g.faceCull && sgFaceCullOn();
    PipeKey key;
    key.v = { (uint32_t)((g.blendEnable ? 1u : 0u) | (g.depthWrite ? 2u : 0u) | (doCull ? 4u : 0u) |
                         (g.depthTest ? 8192u : 0u) | ((g.depthFunc & 7u) << 14) | (doCull && frontCW ? 8u : 0u)),
              g.bSrcRGB,
              g.bDstRGB,
              g.bEqRGB,
              g.bSrcA,
              g.bDstA,
              g.bEqA,
              0u };
    auto it = g_pipelines.find(key);
    if (it != g_pipelines.end())
        return it->second;

    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    // Vertex input: Zelda3DGlVtx (pos3, nrm3, uv0, boneId4, boneW4, color4, uv1, uv2).
    SDL_GPUVertexAttribute attrs[8]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(Zelda3DGlVtx, pos) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(Zelda3DGlVtx, nrm) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(Zelda3DGlVtx, uv) };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(Zelda3DGlVtx, boneIds) };
    attrs[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(Zelda3DGlVtx, weights) };
    attrs[5] = { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(Zelda3DGlVtx, color) };
    attrs[6] = { 6, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(Zelda3DGlVtx, uv1) };
    attrs[7] = { 7, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(Zelda3DGlVtx, uv2) };
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(Zelda3DGlVtx);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = g_vert;
    pci.fragment_shader = g_frag;
    pci.vertex_input_state.vertex_buffer_descriptions = &vb;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 8;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = doCull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = frontCW ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state.enable_depth_clip = false; // depth clamp (device feature), matches Fast3D

    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    pci.depth_stencil_state.enable_depth_test = g.depthTest != 0;
    pci.depth_stencil_state.enable_depth_write = g.depthWrite != 0;
    pci.depth_stencil_state.compare_op = mapDepthFunc(g.depthFunc);
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription ct{};
    ct.format = api->GpuColorFormat();
    if (g.blendEnable) {
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = mapFactor(g.bSrcRGB);
        ct.blend_state.dst_color_blendfactor = mapFactor(g.bDstRGB);
        ct.blend_state.color_blend_op = mapEq(g.bEqRGB);
        ct.blend_state.src_alpha_blendfactor = mapFactor(g.bSrcA);
        ct.blend_state.dst_alpha_blendfactor = mapFactor(g.bDstA);
        ct.blend_state.alpha_blend_op = mapEq(g.bEqA);
    } else {
        ct.blend_state.enable_blend = false;
    }
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = api->GpuDepthFormat();

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!pipe)
        fprintf(stderr, "[Zelda3D_SG] pipeline create failed: %s\n", SDL_GetError());
    g_pipelines[key] = pipe;
    return pipe;
}

SDL_GPUGraphicsPipeline* Fast::Zelda3DRenderer::getUnifiedPipeline(const SgGroup& g, int frontCW, int variant) {
    bool doCull = g.faceCull && sgFaceCullOn();
    PipeKey key;
    key.v = { (uint32_t)((g.blendEnable ? 1u : 0u) | (g.depthWrite ? 2u : 0u) | (doCull ? 4u : 0u) |
                         (g.depthTest ? 8192u : 0u) | ((g.depthFunc & 7u) << 14) | (doCull && frontCW ? 8u : 0u)),
              g.bSrcRGB,
              g.bDstRGB,
              g.bEqRGB,
              g.bSrcA,
              g.bDstA,
              g.bEqA,
              (uint32_t)variant };
    auto it = g_uniPipelines.find(key);
    if (it != g_uniPipelines.end())
        return it->second;

    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    if (g_uniVert[variant] == nullptr) {
        int fragmentProbeMode = 0;
        int fragmentProbeDraw = -1;
        if (const char* mode = std::getenv("ZELDA3D_SG_FRAGDBG")) {
            const int requested = std::atoi(mode);
            if (requested == 1 || requested == 5 || requested == 6) {
                fragmentProbeMode = requested;
            }
        }
        if (const char* draw = std::getenv("ZELDA3D_SG_FRAGDBG_DRAW")) {
            fragmentProbeDraw = std::atoi(draw);
        }
        if (fragmentProbeMode != 0 && fragmentProbeDraw < 0) {
            std::fprintf(stderr, "[Zelda3D_SG] unified FRAGDBG mode=%d requires FRAGDBG_DRAW; probe disabled\n",
                         fragmentProbeMode);
            fragmentProbeMode = 0;
        }
        std::string vsrc = Fast::Unified::BuildVertexSource((Fast::Unified::Variant)variant);
        std::string fsrc = Fast::Unified::BuildFragmentSource((Fast::Unified::Variant)variant, fragmentProbeMode);
        // 1 UBO (UnifiedCommon) + 1 UBO (bones) for vertex; 1 sampler (untextured variant needs 0)
        // + 1 UBO (UnifiedCommon) for fragment — mirrors makeShader's existing (numSamplers, numUbo)
        // convention for the old fixed CMB shader.
        uint32_t numSamplers = variant == (int)Fast::Unified::Variant::kUntextured
                                   ? 0
                                   : (variant == (int)Fast::Unified::Variant::kGenericTev ? 3 : 1);
        g_uniVert[variant] = makeShader(vsrc.c_str(), EShLangVertex, 0, 2);
        g_uniFrag[variant] = makeShader(fsrc.c_str(), EShLangFragment, numSamplers, 1);
        if (!g_uniVert[variant] || !g_uniFrag[variant])
            fprintf(stderr, "[Zelda3D_SG] unified shader variant %d compile FAILED\n", variant);
        else if (fragmentProbeMode != 0)
            std::fprintf(stderr, "[Zelda3D_SG] unified FRAGDBG mode=%d active draw=%d\n", fragmentProbeMode,
                         fragmentProbeDraw);
    }
    if (!g_uniVert[variant] || !g_uniFrag[variant])
        return nullptr;

    // Vertex input: UnifiedVtx (pos4, nrm3, uv0_2, uv1_2, texClamp4, color0..3 x ubyte4norm, fog2,
    // boneIds ubyte4, boneW ubyte4norm, uv2_2) — see unified_vtx.h.
    SDL_GPUVertexAttribute attrs[13]{};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(UnifiedVtx, pos) };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (uint32_t)offsetof(UnifiedVtx, nrm) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(UnifiedVtx, uv0) };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(UnifiedVtx, uv1) };
    attrs[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (uint32_t)offsetof(UnifiedVtx, texClamp) };
    attrs[5] = { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color0) };
    attrs[6] = { 6, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color1) };
    attrs[7] = { 7, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color2) };
    attrs[8] = { 8, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, color3) };
    attrs[9] = { 9, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(UnifiedVtx, fog) };
    attrs[10] = { 10, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, (uint32_t)offsetof(UnifiedVtx, boneIds) };
    attrs[11] = { 11, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, (uint32_t)offsetof(UnifiedVtx, boneW) };
    attrs[12] = { 12, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (uint32_t)offsetof(UnifiedVtx, uv2) };
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(UnifiedVtx);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = g_uniVert[variant];
    pci.fragment_shader = g_uniFrag[variant];
    pci.vertex_input_state.vertex_buffer_descriptions = &vb;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 13;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = doCull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = frontCW ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state.enable_depth_clip = false;

    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    pci.depth_stencil_state.enable_depth_test = g.depthTest != 0;
    pci.depth_stencil_state.enable_depth_write = g.depthWrite != 0;
    pci.depth_stencil_state.compare_op = mapDepthFunc(g.depthFunc);
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription ct{};
    ct.format = api->GpuColorFormat();
    if (g.blendEnable) {
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = mapFactor(g.bSrcRGB);
        ct.blend_state.dst_color_blendfactor = mapFactor(g.bDstRGB);
        ct.blend_state.color_blend_op = mapEq(g.bEqRGB);
        ct.blend_state.src_alpha_blendfactor = mapFactor(g.bSrcA);
        ct.blend_state.dst_alpha_blendfactor = mapFactor(g.bDstA);
        ct.blend_state.alpha_blend_op = mapEq(g.bEqA);
    } else {
        ct.blend_state.enable_blend = false;
    }
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = api->GpuDepthFormat();

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!pipe)
        fprintf(stderr, "[Zelda3D_SG] unified pipeline create failed: %s\n", SDL_GetError());
    g_uniPipelines[key] = pipe;
    return pipe;
}

SDL_GPUShader* Fast::Zelda3DRenderer::makeShader(const char* glsl, EShLanguage stage, uint32_t numSamplers,
                                                 uint32_t numUbo) {
    std::vector<uint32_t> spv;
    if (!Fast::Zelda3DSdl3GpuShaders::Compile(stage, glsl, spv))
        return nullptr;
    SDL_GPUShaderCreateInfo ci{};
    ci.code_size = spv.size() * sizeof(uint32_t);
    ci.code = (const Uint8*)spv.data();
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = (stage == EShLangVertex) ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = numSamplers;
    ci.num_uniform_buffers = numUbo;
    return SDL_CreateGPUShader(g_device, &ci);
}

// #146 item B: fullscreen depth-only reset for the overlay depth scope (Zelda3D_Overlay2D_Begin).
// Fragment unconditionally writes gl_FragDepth=1.0 (far, this backend's 0=near/1=far convention).
// No color output (the pipeline disables the color write mask entirely, so the already-composited
// 3D scene's color is untouched); depth write ON, compare ALWAYS so this reset always lands.

// #146 item B: one-time resources for the overlay depth-scope reset.
bool Fast::Zelda3DRenderer::ensureOverlayDepthResources() {
    if (g_overlayDepthResReady)
        return true;
    if (!ensureResources())
        return false;

    g_overlayDepthFrag =
        makeShader(Fast::Zelda3DSdl3GpuShaders::OverlayDepthFragment(), EShLangFragment, /*samplers=*/0, /*ubo=*/0);
    if (!g_overlayDepthFrag) {
        fprintf(stderr, "[Zelda3D_SG] overlay depth-reset shader create failed: %s\n", SDL_GetError());
        return false;
    }
    // Reuses the model vertex shader slot? No — needs its own fullscreen-triangle vertex shader
    // (no vertex buffer, matches kAoCompVert's gl_VertexIndex trick) since g_vert expects the
    // full per-vertex model attribute layout. Compile a private copy so this reset has no
    // dependency on the AO module's lazy init/enable gating.
    static const char* kFsVert =
        "#version 450\n"
        "void main() {\n"
        "    vec2 p = vec2((gl_VertexIndex == 2) ? 3.0 : -1.0, (gl_VertexIndex == 1) ? 3.0 : -1.0);\n"
        "    gl_Position = vec4(p, 0.0, 1.0);\n"
        "}\n";
    SDL_GPUShader* vert = makeShader(kFsVert, EShLangVertex, 0, 0);
    if (!vert) {
        fprintf(stderr, "[Zelda3D_SG] overlay depth-reset vertex shader create failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = g_overlayDepthFrag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription ct{};
    ct.format = g_activeSdl3GpuApi->GpuColorFormat();
    ct.blend_state.enable_blend = false;
    ct.blend_state.enable_color_write_mask = true;
    ct.blend_state.color_write_mask = 0; // no color channels written — color buffer untouched
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = g_activeSdl3GpuApi->GpuDepthFormat();

    g_overlayDepthPipe = SDL_CreateGPUGraphicsPipeline(g_device, &pci);
    if (!g_overlayDepthPipe) {
        fprintf(stderr, "[Zelda3D_SG] overlay depth-reset pipeline create failed: %s\n", SDL_GetError());
        return false;
    }
    g_overlayDepthResReady = true;
    return true;
}

#endif // ENABLE_SDL3GPU
