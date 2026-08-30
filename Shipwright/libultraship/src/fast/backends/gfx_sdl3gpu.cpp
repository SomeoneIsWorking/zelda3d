#include "ship/window/Window.h"
#ifdef ENABLE_SDL3GPU

// ============================================================================
// SDL3 GPU rendering backend for Fast3D (zelda3d).
//
// Ported from gfx_vulkan.cpp (the structural template). SDL3 GPU is a thinner
// version of the same explicit model — one SDL_GPUDevice instead of
// instance/physical-device/device/queue, no standalone render-pass/framebuffer
// objects (targets are bound at SDL_BeginGPURenderPass), and SPIR-V shaders fed
// to SDL_CreateGPUShader. The per-combiner GLSL is generated exactly like the
// Vulkan backend (prism processor) and compiled to SPIR-V with glslang at
// runtime; only the resource-binding decorations differ (SDL3 GPU requires
// fragment samplers in descriptor set 2 and the fragment UBO in set 3, with
// contiguous 0-based bindings — see kSgShaderTemplate / BuildShaderSource).
//
// Two structural differences from Vulkan drive the design:
//   1. SDL3 GPU forbids transfer (vertex/texture) uploads inside a render pass.
//   2. Clears are expressed as load_op at pass-begin, not mid-pass.
// So this backend RECORDS draws/clears/blits into an op list during the frame
// (staging vertex bytes into a mapped transfer buffer), then at FinishRender
// uploads all vertices in ONE copy pass and replays the ops — opening a render
// pass only when the target framebuffer changes. CPU-readback paths
// (GetPixelDepth / ReadFramebufferToCPU) flush the recorded ops first.
//
// Frame-loop mapping (see Interpreter::EndFrame):
//   mRapi->StartFrame()  -> reset op list + map the vertex transfer buffer.
//   mRapi->EndFrame()    -> no-op (replay deferred to FinishRender).
//   mWapi->SwapBuffersBegin -> framerate pacing only.
//   mRapi->FinishRender()-> upload verts, replay ops, blit fb0 -> swapchain,
//                           submit + present; honor on-demand frame dumps.
// ============================================================================

#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/zelda3d_sdl3gpu.h" // Zelda3DRenderer / Zelda3DHudRenderer member subsystems
#include "fast/backends/gfx_sdl.h"
#include "fast/backends/unified_shader.h"
#include "fast/backends/unified_n64_pack.h" // render-unification Phase 3: CCFeatures -> UnifiedMaterial
#include "fast/unified_vtx.h"
#include "fast/unified_ubo.h"
#include "fast/interpreter.h"
#include "ship/Context.h"
#include <unordered_map>
#include <prism/processor.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <bit>
#include <mutex>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

// Zelda3D frame-dump globals (defined in gfx_sdl2.cpp). The REPL sets these to capture the current
// frame on demand; we honor them from the present path (reading fb 0, which works headless).
extern "C" {
extern char gSoh3dDumpPath[1024];
extern volatile int gSoh3dDumpPending;
extern char gSoh3dDepthDumpPath[1024];
extern volatile int gSoh3dDepthDumpPending;
extern int gUnifiedRenderer; // render-unification effort (kanban #131): bit 1 = N64 unified

// Direct-harness capture-to-buffer path (defined in gfx_sdl3.cpp). See the
// comment there for the protocol. FinishRender consumes gSoh3dCapturePending
// after each frame — the download re-uses the exact same GPU copy pattern
// as WriteFbPpm but writes RGBA8 into gSoh3dCaptureBuf instead of a file.
extern uint8_t* gSoh3dCaptureBuf;
extern uint32_t gSoh3dCaptureCap;
extern uint32_t gSoh3dCaptureW;
extern uint32_t gSoh3dCaptureH;
extern volatile int gSoh3dCapturePending;

// Harness introspection: last-seen state of fb 0 at capture time. Written by
// WriteFbToCaptureBuf on every capture attempt (whether it succeeded or hit
// an early-return), so the harness can `diag` the exact reason capture is
// empty — usually fb0 not yet sized (no game-render frame at title).
extern "C" volatile int gSoh3dFb0LastCaptureAttempt = 0; // count of attempts
extern "C" volatile uint32_t gSoh3dFb0LastW = 0;
extern "C" volatile uint32_t gSoh3dFb0LastH = 0;
extern "C" volatile int gSoh3dFb0LastHasColor = 0; // fb.color != nullptr
extern "C" volatile int gSoh3dFb0LastInRange = 0;  // fbId in [0, size)
}

namespace {
// Runtime GLSL -> SPIR-V via glslang. Same setup the Vulkan backend uses (the produced SPIR-V is
// Vulkan-1.1 / SPV-1.3, which SDL3 GPU's Vulkan driver consumes). glslang's process is initialized
// once per process.
std::once_flag gSgGlslangOnce;

// Cumulative shader compiles for the life of the process. Reported once per run (see
// Ship::Context::BeginRun), so the DELTA between runs answers a question nothing else here could:
// whether a second run recompiles shaders or reuses the engine-lifetime cache. It exists because
// `mm,mm` was measured leaking 5.1 MB on its second run, essentially all of it inside glslang's pool
// allocator, and there was no way to tell a cache miss from a library that simply never frees.
static size_t sSgShaderCompileCount = 0;

bool CompileGlslToSpirv(EShLanguage stage, const std::string& src, std::vector<uint32_t>& outSpirv,
                        std::string& outLog) {
    std::call_once(gSgGlslangOnce, []() { glslang::InitializeProcess(); });
    ++sSgShaderCompileCount;

    glslang::TShader shader(stage);
    const char* str = src.c_str();
    shader.setStrings(&str, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

    const TBuiltInResource* resources = GetDefaultResources();
    const int defaultVersion = 450;
    const EShMessages messages =
        std::bit_cast<EShMessages>(static_cast<unsigned>(EShMsgSpvRules) | static_cast<unsigned>(EShMsgVulkanRules));

    if (!shader.parse(resources, defaultVersion, false, messages)) {
        outLog = std::string("parse: ") + shader.getInfoLog() + "\n" + shader.getInfoDebugLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        outLog = std::string("link: ") + program.getInfoLog();
        return false;
    }
    glslang::SpvOptions spvOptions;
    spvOptions.disableOptimizer = true;
    spvOptions.validate = false;
    glslang::GlslangToSpv(*program.getIntermediate(stage), outSpirv, &spvOptions);
    return !outSpirv.empty();
}
} // namespace

// ============================================================================
// Combiner -> GLSL helpers. Mirror gfx_vulkan.cpp's vk_* helpers verbatim (the combiner formula
// logic is backend-agnostic); kept local to this TU to avoid disturbing the GL/Vulkan backends.
// ============================================================================
namespace {
using namespace Fast;

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

const char* sg_shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                  bool first_cycle, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return first_cycle ? (with_alpha ? "texVal0" : "texVal0.rgb")
                                   : (with_alpha ? "texVal1" : "texVal1.rgb");
            case SHADER_TEXEL0A:
                return first_cycle
                           ? (hint_single_element ? "texVal0.a"
                                                  : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                                : "vec3(texVal0.a, texVal0.a, texVal0.a)"))
                           : (hint_single_element ? "texVal1.a"
                                                  : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                                : "vec3(texVal1.a, texVal1.a, texVal1.a)"));
            case SHADER_TEXEL1A:
                return first_cycle
                           ? (hint_single_element ? "texVal1.a"
                                                  : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                                : "vec3(texVal1.a, texVal1.a, texVal1.a)"))
                           : (hint_single_element ? "texVal0.a"
                                                  : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                                : "vec3(texVal0.a, texVal0.a, texVal0.a)"));
            case SHADER_TEXEL1:
                return first_cycle ? (with_alpha ? "texVal1" : "texVal1.rgb")
                                   : (with_alpha ? "texVal0" : "texVal0.rgb");
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
            case SHADER_TEXEL0A:
                return first_cycle ? "texVal0.a" : "texVal1.a";
            case SHADER_TEXEL1A:
            case SHADER_TEXEL1:
                return first_cycle ? "texVal1.a" : "texVal0.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return RAND_NOISE;
        }
    }
    return "";
}

bool sg_get_bool(prism::ContextTypes* value) {
    if (std::holds_alternative<int>(*value)) {
        return std::get<int>(*value) == 1;
    }
    return false;
}

prism::ContextTypes* sg_append_formula(prism::ContextTypes* _, prism::ContextTypes* a_arg,
                                       prism::ContextTypes* a_single, prism::ContextTypes* a_mult,
                                       prism::ContextTypes* a_mix, prism::ContextTypes* a_with_alpha,
                                       prism::ContextTypes* a_only_alpha, prism::ContextTypes* a_alpha,
                                       prism::ContextTypes* a_first_cycle) {
    auto c = std::get<prism::MTDArray<int>>(*a_arg);
    bool do_single = sg_get_bool(a_single);
    bool do_multiply = sg_get_bool(a_mult);
    bool do_mix = sg_get_bool(a_mix);
    bool with_alpha = sg_get_bool(a_with_alpha);
    bool only_alpha = sg_get_bool(a_only_alpha);
    bool opt_alpha = sg_get_bool(a_alpha);
    bool first_cycle = sg_get_bool(a_first_cycle);
    std::string out = "";
    if (do_single) {
        out += sg_shader_item_to_str(c.at(only_alpha, 3), with_alpha, only_alpha, opt_alpha, first_cycle, false);
    } else if (do_multiply) {
        out += sg_shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += " * ";
        out += sg_shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
    } else if (do_mix) {
        out += "mix(";
        out += sg_shader_item_to_str(c.at(only_alpha, 1), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ", ";
        out += sg_shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ", ";
        out += sg_shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
        out += ")";
    } else {
        out += "(";
        out += sg_shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += " - ";
        out += sg_shader_item_to_str(c.at(only_alpha, 1), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ") * ";
        out += sg_shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
        out += " + ";
        out += sg_shader_item_to_str(c.at(only_alpha, 3), with_alpha, only_alpha, opt_alpha, first_cycle, false);
    }
    return new prism::ContextTypes{ out };
}

// Running location counters for the template's explicit layout(location=) / binding= qualifiers.
// Reset from BuildShaderSource before each stage build. gSgSamplerBind only advances inside taken
// @if branches, giving the fragment samplers contiguous 0-based bindings in set 2 (required by
// SDL3 GPU), in usedSlot order — which DrawTriangles binds in the same order.
int gSgAttrLoc = 0;
int gSgVaryLoc = 0;
int gSgSamplerBind = 0;
prism::ContextTypes* sg_aloc(prism::ContextTypes*) {
    return new prism::ContextTypes{ gSgAttrLoc++ };
}
prism::ContextTypes* sg_vloc(prism::ContextTypes*) {
    return new prism::ContextTypes{ gSgVaryLoc++ };
}
prism::ContextTypes* sg_sloc(prism::ContextTypes*) {
    return new prism::ContextTypes{ gSgSamplerBind++ };
}

std::optional<std::string> sg_include_noop(const std::string&) {
    return std::nullopt;
}

// #version 450 template. Identical combiner body to shaders/opengl/default.shader.glsl and the
// Vulkan template; the ONLY structural change from the Vulkan template is the fragment resource
// binding model: SDL3 GPU SPIR-V requires fragment sampled textures in descriptor set 2 and
// fragment uniform buffers in set 3, with contiguous 0-based bindings within each set.
const char* kSgShaderTemplate = R"PRISM(@prism(type='fragment', name='Fast3D SDL3GPU Shader', version='1.0.0')
#version 450

@if(VERTEX_SHADER)
    layout(location = @{aloc()}) in vec4 aVtxPos;

    @for(i in 0..2)
        @if(o_textures[i])
            layout(location = @{aloc()}) in vec2 aTexCoord@{i};
            layout(location = @{vloc()}) out vec2 vTexCoord@{i};
            @for(j in 0..2)
                @if(o_clamp[i][j])
                    @if(j == 0)
                        layout(location = @{aloc()}) in float aTexClampS@{i};
                        layout(location = @{vloc()}) out float vTexClampS@{i};
                    @else
                        layout(location = @{aloc()}) in float aTexClampT@{i};
                        layout(location = @{vloc()}) out float vTexClampT@{i};
                    @end
                @end
            @end
        @end
    @end

    @if(o_fog)
        layout(location = @{aloc()}) in vec4 aFog;
        layout(location = @{vloc()}) out vec4 vFog;
    @end

    @if(o_grayscale)
        layout(location = @{aloc()}) in vec4 aGrayscaleColor;
        layout(location = @{vloc()}) out vec4 vGrayscaleColor;
    @end

    @for(i in 0..o_inputs)
        @if(o_alpha)
            layout(location = @{aloc()}) in vec4 aInput@{i + 1};
            layout(location = @{vloc()}) out vec4 vInput@{i + 1};
        @else
            layout(location = @{aloc()}) in vec3 aInput@{i + 1};
            layout(location = @{vloc()}) out vec3 vInput@{i + 1};
        @end
    @end

    void main() {
        @for(i in 0..2)
            @if(o_textures[i])
                vTexCoord@{i} = aTexCoord@{i};
                @for(j in 0..2)
                    @if(o_clamp[i][j])
                        @if(j == 0)
                            vTexClampS@{i} = aTexClampS@{i};
                        @else
                            vTexClampT@{i} = aTexClampT@{i};
                        @end
                    @end
                @end
            @end
        @end
        @if(o_fog)
            vFog = aFog;
        @end
        @if(o_grayscale)
            vGrayscaleColor = aGrayscaleColor;
        @end
        @for(i in 0..o_inputs)
            vInput@{i + 1} = aInput@{i + 1};
        @end
        gl_Position = aVtxPos;
    }
@else
    layout(location = 0) out vec4 vOutColor;

    @for(i in 0..2)
        @if(o_textures[i])
            layout(location = @{vloc()}) in vec2 vTexCoord@{i};
            @for(j in 0..2)
                @if(o_clamp[i][j])
                    @if(j == 0)
                        layout(location = @{vloc()}) in float vTexClampS@{i};
                    @else
                        layout(location = @{vloc()}) in float vTexClampT@{i};
                    @end
                @end
            @end
        @end
    @end

    @if(o_fog) layout(location = @{vloc()}) in vec4 vFog;
    @if(o_grayscale) layout(location = @{vloc()}) in vec4 vGrayscaleColor;

    @for(i in 0..o_inputs)
        @if(o_alpha)
            layout(location = @{vloc()}) in vec4 vInput@{i + 1};
        @else
            layout(location = @{vloc()}) in vec3 vInput@{i + 1};
        @end
    @end

    @if(o_textures[0]) layout(set = 2, binding = @{sloc()}) uniform sampler2D uTex0;
    @if(o_textures[1]) layout(set = 2, binding = @{sloc()}) uniform sampler2D uTex1;
    @if(o_masks[0]) layout(set = 2, binding = @{sloc()}) uniform sampler2D uTexMask0;
    @if(o_masks[1]) layout(set = 2, binding = @{sloc()}) uniform sampler2D uTexMask1;
    @if(o_blend[0]) layout(set = 2, binding = @{sloc()}) uniform sampler2D uTexBlend0;
    @if(o_blend[1]) layout(set = 2, binding = @{sloc()}) uniform sampler2D uTexBlend1;

    layout(set = 3, binding = 0, std140) uniform UBO {
        int frame_count;
        float noise_scale;
        float prim_depth;
        ivec2 texture_width;
        ivec2 texture_height;
        ivec2 texture_filtering;
    } ubo;
    #define frame_count ubo.frame_count
    #define noise_scale ubo.noise_scale
    #define prim_depth ubo.prim_depth
    #define texture_width ubo.texture_width
    #define texture_height ubo.texture_height
    #define texture_filtering ubo.texture_filtering

    #define TEX_OFFSET(off) texture(tex, texCoord - off / texSize)
    #define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)

    float random(in vec3 value) {
        float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));
        return fract(sin(random) * 143758.5453);
    }

    vec4 fromLinear(vec4 linearRGB){
        bvec3 cutoff = lessThan(linearRGB.rgb, vec3(0.0031308));
        vec3 higher = vec3(1.055)*pow(linearRGB.rgb, vec3(1.0/2.4)) - vec3(0.055);
        vec3 lower = linearRGB.rgb * vec3(12.92);
        return vec4(mix(higher, lower, cutoff), linearRGB.a);
    }

    vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {
        vec2 offset = fract(texCoord*texSize - vec2(0.5));
        offset -= step(1.0, offset.x + offset.y);
        vec4 c0 = TEX_OFFSET(offset);
        vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));
        vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));
        return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);
    }

    vec4 hookTexture2D(in int id, sampler2D tex, in vec2 uv, in vec2 texSize) {
    @if(o_three_point_filtering)
        if(texture_filtering[id] == @{FILTER_THREE_POINT}) {
            return filter3point(tex, uv, texSize);
        }
    @end
        return texture(tex, uv);
    }

    #define TEX_SIZE(tex) vec2(texture_width[tex], texture_height[tex])

    void main() {
        @for(i in 0..2)
            @if(o_textures[i])
                @{s = o_clamp[i][0]}
                @{t = o_clamp[i][1]}

                vec2 texSize@{i} = TEX_SIZE(@{i});

                @if(!s && !t)
                    vec2 vTexCoordAdj@{i} = vTexCoord@{i};
                @else
                    @if(s && t)
                        vec2 vTexCoordAdj@{i} = clamp(vTexCoord@{i}, 0.5 / texSize@{i}, vec2(vTexClampS@{i}, vTexClampT@{i}));
                    @elseif(s)
                        vec2 vTexCoordAdj@{i} = vec2(clamp(vTexCoord@{i}.s, 0.5 / texSize@{i}.s, vTexClampS@{i}), vTexCoord@{i}.t);
                    @else
                        vec2 vTexCoordAdj@{i} = vec2(vTexCoord@{i}.s, clamp(vTexCoord@{i}.t, 0.5 / texSize@{i}.t, vTexClampT@{i}));
                    @end
                @end

                vec4 texVal@{i} = hookTexture2D(@{i}, uTex@{i}, vTexCoordAdj@{i}, texSize@{i});

                @if(o_masks[i])
                    vec2 maskSize@{i} = textureSize(uTexMask@{i}, 0);

                    vec4 maskVal@{i} = hookTexture2D(@{i}, uTexMask@{i}, vTexCoordAdj@{i}, maskSize@{i});

                    @if(o_blend[i])
                        vec4 blendVal@{i} = hookTexture2D(@{i}, uTexBlend@{i}, vTexCoordAdj@{i}, texSize@{i});
                    @else
                        vec4 blendVal@{i} = vec4(0, 0, 0, 0);
                    @end

                    texVal@{i} = mix(texVal@{i}, blendVal@{i}, maskVal@{i}.a);
                @end
            @end
        @end

        @if(o_alpha)
            vec4 texel;
        @else
            vec3 texel;
        @end

        @if(o_2cyc)
            @{f_range = 2}
        @else
            @{f_range = 1}
        @end

        @for(c in 0..f_range)
            @if(c == 1)
                @if(o_alpha)
                    @if(o_c[c][1][2] == SHADER_COMBINED)
                        texel.a = WRAP(texel.a, -1.01, 1.01);
                    @else
                        texel.a = WRAP(texel.a, -0.51, 1.51);
                    @end
                @end

                @if(o_c[c][0][2] == SHADER_COMBINED)
                    texel.rgb = WRAP(texel.rgb, -1.01, 1.01);
                @else
                    texel.rgb = WRAP(texel.rgb, -0.51, 1.51);
                @end
            @end

            @if(!o_color_alpha_same[c] && o_alpha)
                texel = vec4(@{
                append_formula(o_c[c], o_do_single[c][0],
                            o_do_multiply[c][0], o_do_mix[c][0], false, false, true, c == 0)
                }, @{append_formula(o_c[c], o_do_single[c][1],
                            o_do_multiply[c][1], o_do_mix[c][1], true, true, true, c == 0)
                });
            @else
                texel = @{append_formula(o_c[c], o_do_single[c][0],
                            o_do_multiply[c][0], o_do_mix[c][0], o_alpha, false,
                            o_alpha, c == 0)};
            @end
        @end

        texel = WRAP(texel, -0.51, 1.51);
        texel = clamp(texel, 0.0, 1.0);
        @if(o_fog)
            @if(o_alpha)
                texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);
            @else
                texel = mix(texel, vFog.rgb, vFog.a);
            @end
        @end

        @if(o_texture_edge && o_alpha)
            if (texel.a > 0.19) texel.a = 1.0; else discard;
        @end

        @if(o_alpha && o_noise)
            texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + texel.a, 0.0, 1.0));
        @end

        @if(o_grayscale)
            float intensity = (texel.r + texel.g + texel.b) / 3.0;
            vec3 new_texel = vGrayscaleColor.rgb * intensity;
            texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);
        @end

        @if(o_alpha)
            @if(o_alpha_threshold)
                if (texel.a < 8.0 / 256.0) discard;
            @end
            @if(o_invisible)
                texel.a = 0.0;
            @end
            vOutColor = texel;
        @else
            vOutColor = vec4(texel, 1.0);
        @end

        @if(srgb_mode)
            vOutColor = fromLinear(vOutColor);
        @end

        @if(o_prim_depth)
            gl_FragDepth = prim_depth;
        @end
    }
@end
)PRISM";

// std140 fragment UBO matching the template's UBO block (48 bytes).
struct SgUboData {
    int32_t frame_count;
    float noise_scale;
    float prim_depth;
    int32_t _pad0;
    int32_t texture_width[2];
    int32_t texture_height[2];
    int32_t texture_filtering[2];
    int32_t _pad1[2];
};
} // namespace

namespace Fast {

// Defined here rather than beside the counter: that lives in this file's anonymous namespace, so a
// definition there would have internal linkage and could not satisfy the declaration in the header.
size_t Sdl3GpuShaderCompileCount() {
    return sSgShaderCompileCount;
}

// Release glslang's process-wide pools once, at engine shutdown.
//
// glslang's public API is InitializeProcess/FinalizeProcess and nothing finer (issue 0020: the
// per-compile TPoolAllocator bracketing that would fix the per-compile growth needs internal headers
// this build does not ship). So this reclaims the builtin symbol tables at exit -- several MB -- and
// does NOT address the per-compile growth, which is the honest scope.
//
// Guarded on the compile counter rather than a separate flag: FinalizeProcess without a preceding
// InitializeProcess is undefined, and the counter is incremented in exactly the function that does
// the call_once init, so a non-zero count is proof the process was initialized.
void Sdl3GpuFinalizeShaderCompiler() {
    if (sSgShaderCompileCount == 0) {
        SPDLOG_INFO("Shutdown: glslang was never initialized (0 shaders compiled); nothing to finalize.");
        return;
    }
    SPDLOG_INFO("Shutdown: finalizing glslang after {} shader compile(s).", sSgShaderCompileCount);
    glslang::FinalizeProcess();
}

GfxRenderingAPISdl3Gpu* g_activeSdl3GpuApi = nullptr;

GfxRenderingAPISdl3Gpu::GfxRenderingAPISdl3Gpu(GfxWindowBackendSDL3* windowBackend) : mWindowBackend(windowBackend) {
}

// Duplicate-release detection for GPU handles.
//
// SDL3's Vulkan backend does not destroy a released resource immediately: it queues it and frees it
// later from VULKAN_INTERNAL_PerformPendingDestroys. So releasing the same handle twice is silent at
// the call site and surfaces, much later and somewhere else entirely, as a double free inside
// SDL_ReleaseWindowFromGPUDevice -> VULKAN_Wait -> PerformPendingDestroys. That is precisely the
// abort this project spent two issues chasing: the reported site named the teardown call that
// happened to flush the queue, never the release that queued the handle twice.
//
// So the accounting lives HERE, at the release, where the offender is still on the stack. It reports
// pass or fail with the denominator -- "released N handles, 0 released twice" is the only form of
// "no duplicates" that can be distinguished from an instrument that looked at nothing.
static std::unordered_map<const void*, const char*> sGpuReleasedHandles;
static size_t sGpuReleaseCount = 0;
static size_t sGpuDoubleReleaseCount = 0;

// TEARDOWN ONLY, and that is a correctness boundary rather than a shortcut.
//
// The tracking is pointer identity, and mid-run pointer identity does not mean what it looks like:
// SDL frees a released resource lazily and a later SDL_CreateGPU* readily hands back the SAME
// address, so a live, freshly-created handle can equal one released earlier. Recording releases
// across a whole run therefore reported four "double releases" that were nothing of the kind -- a
// recycled texture address, still live, which the check would then have refused to release.
//
// Hooking all 25 creation sites to un-record a reused address would fix that, but the window where
// the bug this exists for actually lives is teardown, and in that window nothing is created at all,
// so identity is exact. So the map is cleared and tracking switched on at the top of the
// destructor: full precision where it means something, silence where it would lie.
static bool sGpuTrackReleases = false;

static void BeginGpuReleaseTracking(void) {
    sGpuReleasedHandles.clear();
    sGpuReleaseCount = 0;
    sGpuDoubleReleaseCount = 0;
    sGpuTrackReleases = true;
}

// Returns true if it is safe to release `handle` now -- i.e. it is not a handle this teardown has
// already released. Outside teardown it always returns true and records nothing.
static bool NoteGpuRelease(const void* handle, const char* what) {
    if (handle == nullptr)
        return false;
    if (!sGpuTrackReleases)
        return true;
    sGpuReleaseCount++;
    auto it = sGpuReleasedHandles.find(handle);
    if (it != sGpuReleasedHandles.end()) {
        sGpuDoubleReleaseCount++;
        SPDLOG_ERROR("SDL3 GPU: DOUBLE RELEASE of {} handle {} -- already released as '{}'. Skipping;"
                     " this is the release that would have double-freed inside SDL's deferred destroy.",
                     what, handle, it->second);
        return false;
    }
    sGpuReleasedHandles.emplace(handle, what);
    return true;
}

// Claim/release accounting for the window<->GPU-device attachment. Process-wide rather than
// per-object on purpose: the launcher runs several game cores in one process against one engine, so
// "did anyone else already claim this window" is a question no single renderer instance can answer.
static int sGpuWindowClaims = 0;
static int sGpuWindowReleases = 0;

GfxRenderingAPISdl3Gpu::~GfxRenderingAPISdl3Gpu() {
    // Step trace for teardown, gated on the backend's existing debug flag. This destructor spent the
    // project's whole life unreachable -- OoT's DeinitOTR calls _exit(0), so static destructors never
    // ran -- and the first time a core unwound and returned (MM, through the launcher) it aborted with
    // a glibc "double free or corruption (!prev)" somewhere inside these ~15 release loops. A bare
    // backtrace cannot name which one: the binary is -O2/NDEBUG with no line info and the system SDL3
    // exports a single symbol, so both addr2line and nm resolve to nothing. Each step names itself
    // BEFORE the call it is about to make, so the last line printed is the one that died.
    const bool trace = [] {
        const char* e = getenv("ZELDA3D_SDL3GPU_DEBUG");
        return e != nullptr && e[0] == '1';
    }();
    const auto step = [trace](const char* what) {
        if (trace) {
            SPDLOG_INFO("~GfxRenderingAPISdl3Gpu: {}", what);
        }
    };
    step("begin");
    BeginGpuReleaseTracking();
    if (g_activeSdl3GpuApi == this)
        g_activeSdl3GpuApi = nullptr;
    // Release the folded-in renderer subsystems while the device is still alive (their GPU resources
    // are owned by the device and freed at SDL_DestroyGPUDevice below — same as before the fold).
    // Hand back what the model renderer owns FIRST, while the device is still alive. The comment
    // above used to claim SDL_DestroyGPUDevice would take care of it; the validation layer counted 409
    // objects still alive at vkDestroyDevice and said otherwise (docs/issues/0009).
    step("mSoh3d.releaseGpuResources");
    if (mSoh3d) {
        mSoh3d->releaseGpuResources(&NoteGpuRelease);
    }
    step("mSoh3d.reset");
    mSoh3d.reset();
    step("mHud.reset");
    mHud.reset();
    if (mDevice == nullptr)
        return;
    if (mVtxMapped) {
        SDL_UnmapGPUTransferBuffer(mDevice, mVtxTransfer);
        mVtxMapped = nullptr;
    }
    if (mCmd) {
        SDL_CancelGPUCommandBuffer(mCmd);
        mCmd = nullptr;
    }
    step("WaitForGPUIdle");
    SDL_WaitForGPUIdle(mDevice);
    step("FlushPendingTexReleases");
    FlushPendingTexReleases(); // GPU idle: release any textures deferred from the last frame
    step("framebuffers");
    for (auto& fb : mFramebuffers)
        DestroyFbResources(fb);
    mFramebuffers.clear();
    step("pipelines");
    for (auto& kv : mPipelineCache)
        if (NoteGpuRelease(kv.second, "graphics pipeline"))
            SDL_ReleaseGPUGraphicsPipeline(mDevice, kv.second);
    mPipelineCache.clear();
    step("shaders");
    for (auto& kv : mShaderProgramPool) {
        if (NoteGpuRelease(kv.second.vert, "vertex shader"))
            SDL_ReleaseGPUShader(mDevice, kv.second.vert);
        if (NoteGpuRelease(kv.second.frag, "fragment shader"))
            SDL_ReleaseGPUShader(mDevice, kv.second.frag);
    }
    mShaderProgramPool.clear();
    step("samplers");
    for (auto& kv : mSamplerCache)
        if (NoteGpuRelease(kv.second, "sampler"))
            SDL_ReleaseGPUSampler(mDevice, kv.second);
    mSamplerCache.clear();
    step("textures");
    for (auto& t : mTextures) {
        if (!t.isFbAlias && NoteGpuRelease(t.tex, "texture"))
            SDL_ReleaseGPUTexture(mDevice, t.tex);
    }
    mTextures.clear();
    step("dummy tex/sampler");
    // mDummyTex is created here (SDL_CreateGPUTexture in GetOrCreateDummyTexture), so it is ours.
    if (NoteGpuRelease(mDummyTex, "dummy texture"))
        SDL_ReleaseGPUTexture(mDevice, mDummyTex);
    // mDummySampler is NOT. It is `GetOrCreateSampler(false, CLAMP, CLAMP)` -- a BORROWED pointer to
    // a sampler the cache above owns and has already released. Releasing it here was the second
    // release of one handle, and it is what aborted every clean quit: SDL's Vulkan backend queues a
    // released resource instead of destroying it, so the duplicate sat in the pending-destroy list
    // until VULKAN_ReleaseWindow flushed it and freed the same 16 bytes twice. The abort therefore
    // pointed at SDL_ReleaseWindowFromGPUDevice -- or, once the heap was damaged, at whatever freed
    // next (Config::Save's json, SDL_DestroyWindow's X11 reply) -- and never at this line.
    mDummySampler = nullptr;
    step("vbo/transfer");
    if (NoteGpuRelease(mVbo, "vertex buffer"))
        SDL_ReleaseGPUBuffer(mDevice, mVbo);
    if (NoteGpuRelease(mVtxTransfer, "vertex transfer buffer"))
        SDL_ReleaseGPUTransferBuffer(mDevice, mVtxTransfer);
    step("release window from device");
    // Printed unconditionally, with both counts, because the question this answers -- is the window
    // claimed exactly once for this device? -- has a wrong answer that is otherwise invisible: SDL
    // frees one 16-byte block twice inside this single call (ASAN: attempting double-free, both
    // stacks identical and both ending here), and a claim/release imbalance is the only explanation
    // that lives on our side of the boundary. A silent "released it" line could not distinguish
    // "claimed once" from "claimed twice".
    SPDLOG_INFO("~GfxRenderingAPISdl3Gpu: releasing window {} from device {} -- claims={} releases={}", (void*)mWindow,
                (void*)mDevice, sGpuWindowClaims, sGpuWindowReleases);
    if (mWindow) {
        sGpuWindowReleases++;
        SDL_ReleaseWindowFromGPUDevice(mDevice, mWindow);
    }
    SPDLOG_INFO("SDL3 GPU: released {} handle(s) tearing down, {} of them released more than once.", sGpuReleaseCount,
                sGpuDoubleReleaseCount);
    step("DestroyGPUDevice");
    SDL_DestroyGPUDevice(mDevice);
    step("done");
    mDevice = nullptr;
}

const char* GfxRenderingAPISdl3Gpu::GetName() {
    return "SDL3GPU";
}

// ---------------------------------------------------------------------------
// Device + window
// ---------------------------------------------------------------------------

void GfxRenderingAPISdl3Gpu::CreateDeviceAndClaim() {
    const char* dbg = getenv("ZELDA3D_SDL3GPU_DEBUG");
    bool debug = dbg != nullptr && dbg[0] == '1';
    mDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, debug, nullptr);
    if (mDevice == nullptr) {
        SPDLOG_ERROR("SDL_CreateGPUDevice failed: {}", SDL_GetError());
        abort();
    }
    if (!SDL_ClaimWindowForGPUDevice(mDevice, mWindow)) {
        SPDLOG_ERROR("SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
        abort();
    }
    sGpuWindowClaims++;
    SPDLOG_INFO("SDL3 GPU backend: claimed window {} for device {} -- claims={} releases={}", (void*)mWindow,
                (void*)mDevice, sGpuWindowClaims, sGpuWindowReleases);
    SDL_SetGPUAllowedFramesInFlight(mDevice, 2);
    const char* drv = SDL_GetGPUDeviceDriver(mDevice);
    SPDLOG_INFO("SDL3 GPU backend: device driver = {}", drv ? drv : "(unknown)");
}

void GfxRenderingAPISdl3Gpu::OnResize() {
    // Swapchain is managed by SDL; nothing to do.
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

void GfxRenderingAPISdl3Gpu::EnsureCommandBuffer() {
    if (mCmd == nullptr) {
        mCmd = SDL_AcquireGPUCommandBuffer(mDevice);
    }
}

void GfxRenderingAPISdl3Gpu::StartFrame() {
    if (mFrameAcquired)
        return; // idempotent: the interpreter calls StartFrame twice per displayed frame

    mFrameCount++;
    mOps.clear();
    mSoh3dModelUbos.clear();
    mVtxUsed = 0;
    mVtxMapped = (uint8_t*)SDL_MapGPUTransferBuffer(mDevice, mVtxTransfer, true /*cycle*/);

    int w = 0, h = 0;
    if (!mFramebuffers.empty() && mFramebuffers[0].width > 0) {
        w = (int)mFramebuffers[0].width;
        h = (int)mFramebuffers[0].height;
    } else {
        SDL_GetWindowSizeInPixels(mWindow, &w, &h);
    }
    mCurrentFb = 0;
    mCurrentViewport = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    mCurrentScissor = { 0, 0, w, h };
    mFrameAcquired = true;
}

void GfxRenderingAPISdl3Gpu::EndFrame() {
    // Replay is deferred to FinishRender (where the swapchain texture is acquired). No-op here.
}

void GfxRenderingAPISdl3Gpu::ReplayOps(SDL_GPUTexture* presentTex, uint32_t presentW, uint32_t presentH) {
    EnsureCommandBuffer();

    if (mVtxMapped) {
        SDL_UnmapGPUTransferBuffer(mDevice, mVtxTransfer);
        mVtxMapped = nullptr;
    }
    if (mVtxUsed > 0) {
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(mCmd);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = mVtxTransfer;
        src.offset = 0;
        SDL_GPUBufferRegion dst{};
        dst.buffer = mVbo;
        dst.offset = 0;
        dst.size = mVtxUsed;
        SDL_UploadToGPUBuffer(cp, &src, &dst, true /*cycle*/);
        SDL_EndGPUCopyPass(cp);
    }

    const size_t nfb = mFramebuffers.size();
    std::vector<uint8_t> pendClearColor(nfb, 0), pendClearDepth(nfb, 0);
    SDL_GPURenderPass* pass = nullptr;
    int curFb = -1;
    // Blend constants are RENDER-PASS state (SDL_SetGPUBlendConstants), not pipeline state, and
    // their value at the start of a pass is undefined — so track what this pass was last told and
    // (re)issue only when a draw that actually reads them needs a different value. Ops that don't
    // use a CONSTANT_COLOR blend factor never touch it: leftover constants are inert for them.
    bool blendConstSet = false;
    SDL_FColor curBlendConst{};

    auto endPass = [&]() {
        if (pass) {
            SDL_EndGPURenderPass(pass);
            pass = nullptr;
            curFb = -1;
        }
    };
    auto beginPass = [&](int fb) {
        FramebufferSDL3& f = mFramebuffers[fb];
        SDL_GPUColorTargetInfo ct{};
        ct.texture = f.color;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.load_op = pendClearColor[fb] ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        ct.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        bool hasD = f.depth != nullptr;
        SDL_GPUDepthStencilTargetInfo dt{};
        if (hasD) {
            dt.texture = f.depth;
            dt.store_op = SDL_GPU_STOREOP_STORE;
            dt.load_op = pendClearDepth[fb] ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            dt.clear_depth = 1.0f;
            dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        }
        pass = SDL_BeginGPURenderPass(mCmd, &ct, 1, hasD ? &dt : nullptr);
        pendClearColor[fb] = 0;
        pendClearDepth[fb] = 0;
        curFb = fb;
        blendConstSet = false; // new pass -> blend constants are undefined again
    };

#if defined(__APPLE__)
    // Pre-create the dummy texture/sampler now, while no render pass is active, so the OP_DRAW
    // null-binding guard below (macOS/MoltenVK) never has to create GPU resources mid-pass.
    DummyTexture();
    DummySampler();
#endif

    for (Op& op : mOps) {
        if (op.fb < 0 || (size_t)op.fb >= nfb)
            continue;
        switch (op.kind) {
            case OP_CLEAR: {
                if (pass && curFb == op.fb)
                    endPass();
                if (op.clearColor)
                    pendClearColor[op.fb] = 1;
                if (op.clearDepth)
                    pendClearDepth[op.fb] = 1;
                // Open immediately so the clear executes even without a following draw.
                if (mFramebuffers[op.fb].color)
                    beginPass(op.fb);
                break;
            }
            case OP_COPY:
                if (getenv("ZELDA3D_OPORDER")) {
                    static int shownC = 0;
                    if (shownC < 60) {
                        fprintf(stderr, "[OPORDER]     COPY -> fb=%d (src %d)\n", op.fb, op.srcFb);
                        shownC++;
                    }
                }
                {
                    endPass();
                    if (op.srcFb < 0 || (size_t)op.srcFb >= nfb)
                        break;
                    FramebufferSDL3& s = mFramebuffers[op.srcFb];
                    FramebufferSDL3& d = mFramebuffers[op.fb];
                    if (!s.color || !d.color)
                        break;
                    SDL_GPUBlitInfo bi{};
                    bi.source.texture = s.color;
                    bi.source.x = op.srcRect.x;
                    bi.source.y = op.srcRect.y;
                    bi.source.w = op.srcRect.w;
                    bi.source.h = op.srcRect.h;
                    bi.destination.texture = d.color;
                    bi.destination.x = op.dstRect.x;
                    bi.destination.y = op.dstRect.y;
                    bi.destination.w = op.dstRect.w;
                    bi.destination.h = op.dstRect.h;
                    bi.load_op = SDL_GPU_LOADOP_LOAD;
                    bi.filter = op.nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
                    SDL_BlitGPUTexture(mCmd, &bi);
                    break;
                }
            case OP_DRAW: {
                FramebufferSDL3& f = mFramebuffers[op.fb];
                if (!f.color || !f.depth)
                    break;
                if (!(pass && curFb == op.fb)) {
                    endPass();
                    beginPass(op.fb);
                }
                // Diagnostic (ZELDA3D_DBG_OPDRAW=1): dump every replayed draw's recorded state —
                // pipeline, vertex range, and each fragment-sampler binding. With a synchronous GPU
                // (LP_NUM_THREADS=0 on lavapipe, or any single-threaded driver) the last line printed
                // before a fault pins the exact culprit op; combined with ZELDA3D_SDL3GPU_DEBUG=1
                // validation it's how the "bones"-descriptor / pipeline-layout crash was found. Pure
                // logging, no behaviour change.
                static const bool kDbgOpDraw = getenv("ZELDA3D_DBG_OPDRAW") != nullptr;
                if (kDbgOpDraw) {
                    static const char* kClass[] = { "n64", "model", "hud" };
                    fprintf(stderr, "[DBG_OPDRAW] fb=%d cls=%s pipe=%p nVerts=%u first=%u vbo=%p nSamp=%u", op.fb,
                            kClass[op.drawClass], (void*)op.pipeline, op.numVerts, op.firstVertex,
                            (void*)(op.altVbo ? op.altVbo : mVbo), op.numSamplers);
                    for (uint32_t si = 0; si < op.numSamplers; si++)
                        fprintf(stderr, " [%u tex=%p samp=%p]", si, (void*)op.samplers[si].texture,
                                (void*)op.samplers[si].sampler);
                    fprintf(stderr, "\n");
                    fflush(stderr);
                }
                if (getenv("ZELDA3D_OPORDER")) {
                    // SEPARATE counters per target. A single shared cap is useless here: at MSAA 4
                    // the model draws (fb=1) exhaust it before any HUD draw (fb=0) can print, which
                    // reads as "the HUD never draws" when it simply never got logged.
                    static int nFb0 = 0, nOther = 0;
                    if (op.fb == 0) {
                        if (nFb0 < 20)
                            fprintf(stderr, "[OPORDER] HUDDRAW fb=0 #%d\n", nFb0);
                        nFb0++;
                    } else {
                        nOther++;
                    }
                    if (((nFb0 + nOther) % 500) == 0)
                        fprintf(stderr, "[OPORDER] totals: fb0=%d other=%d\n", nFb0, nOther);
                }
                SDL_BindGPUGraphicsPipeline(pass, op.pipeline);
                if (op.useBlendConstants &&
                    (!blendConstSet || op.blendConstants.r != curBlendConst.r ||
                     op.blendConstants.g != curBlendConst.g || op.blendConstants.b != curBlendConst.b ||
                     op.blendConstants.a != curBlendConst.a)) {
                    SDL_SetGPUBlendConstants(pass, op.blendConstants);
                    curBlendConst = op.blendConstants;
                    blendConstSet = true;
                    // ZELDA3D_BLENDCONST_LOG=1 prints each constant vector actually pushed to the
                    // GPU. It exists because "the constants never arrive" and "this scene draws no
                    // constant-blend material" look identical from a screenshot: verifying this fix
                    // at Zora's Domain produced a ~1% pixel shift that was pure launch-to-launch
                    // variance, and only this log distinguished the two (Water Temple: 40 sets of
                    // (0.5,0.5,0.5,0.5); Zora: zero, because nothing there uses a constant factor).
                    if (getenv("ZELDA3D_BLENDCONST_LOG")) {
                        static int n = 0;
                        if (n < 40) {
                            fprintf(stderr, "[Zelda3D_BLEND] set constants (%.3f,%.3f,%.3f,%.3f)\n",
                                    op.blendConstants.r, op.blendConstants.g, op.blendConstants.b, op.blendConstants.a);
                            n++;
                        }
                    }
                }
                SDL_SetGPUViewport(pass, &op.viewport);
                SDL_SetGPUScissor(pass, &op.scissor);
                // Only the uniform push + vertex source differ across the draw classes (the vertex
                // layout is baked into op.pipeline). The single fragment-sampler bind path below and the
                // draw are shared. altVbo != null selects the Zelda3D model / HUD buffer (offset 0); null
                // falls back to the shared frame mVbo at vboOffset (N64). DRAW_FULLSCREEN binds no vbo.
                bool bindVbo = true;
                switch (op.drawClass) {
                    case Op::DRAW_MODEL: {
                        // Two-block push: common state at binding 0 (both stages), bone matrices at
                        // vertex binding 1 — neither may exceed SDL3 GPU's 4096-byte cap (zelda3d_sg_ubo.h).
                        const uint8_t* u = mSoh3dModelUbos[op.zelda3dDrawIdx].data();
                        SDL_PushGPUVertexUniformData(mCmd, 0, u, Zelda3DSg::kCommonBytes);
                        SDL_PushGPUFragmentUniformData(mCmd, 0, u, Zelda3DSg::kCommonBytes);
                        SDL_PushGPUVertexUniformData(mCmd, 1, u + Zelda3DSg::kCommonBytes, Zelda3DSg::kBonesBytes);
                        break;
                    }
                    case Op::DRAW_HUD:
                        SDL_PushGPUVertexUniformData(mCmd, 0, op.ubo, 16); // { vec2 viewport; vec2 pad; }
                        break;
                    case Op::DRAW_FULLSCREEN:
                        SDL_PushGPUFragmentUniformData(mCmd, 0, op.ubo, op.uboLen);
                        bindVbo = false; // fullscreen triangle is generated from gl_VertexIndex
                        break;
                    case Op::DRAW_N64:
                    default:
                        SDL_PushGPUFragmentUniformData(mCmd, 0, op.ubo, sizeof(SgUboData));
                        break;
                }
                if (bindVbo) {
                    SDL_GPUBufferBinding vb{};
                    vb.buffer = op.altVbo ? op.altVbo : mVbo;
                    vb.offset = op.altVbo ? 0 : op.vboOffset;
                    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
                }
                if (op.numSamplers > 0) {
#if defined(__APPLE__)
                    // macOS/MoltenVK only: SDL_BindGPUFragmentSamplers dereferences each binding and
                    // segfaults on a null texture/sampler. Patch nulls with the (pre-warmed) dummies;
                    // if a dummy is itself null, skip the draw rather than fault. Must NOT create GPU
                    // resources here (we're inside an active render pass) — the dummies are pre-created
                    // before the op loop, so DummyTexture()/DummySampler() only return cached handles.
                    bool bindable = true;
                    for (uint32_t si = 0; si < op.numSamplers; si++) {
                        if (op.samplers[si].texture == nullptr) {
                            op.samplers[si].texture = DummyTexture();
                        }
                        if (op.samplers[si].sampler == nullptr) {
                            op.samplers[si].sampler = DummySampler();
                        }
                        if (op.samplers[si].texture == nullptr || op.samplers[si].sampler == nullptr) {
                            static bool warned = false;
                            if (!warned) {
                                warned = true;
                                SPDLOG_ERROR("OP_DRAW: null fragment-sampler binding (slot {} of {}); "
                                             "skipping draw to avoid a GPU-driver fault",
                                             si, op.numSamplers);
                            }
                            bindable = false;
                        }
                    }
                    if (!bindable) {
                        break;
                    }
#endif
                    SDL_BindGPUFragmentSamplers(pass, 0, op.samplers, op.numSamplers);
                }
                SDL_DrawGPUPrimitives(pass, op.numVerts, 1, op.firstVertex, 0);
                break;
            }
            case OP_EXT_OWN_PASS: {
                // Offscreen Zelda3D pass (shadow / AO depth). The callback owns its own render pass, so
                // the main fb pass must be closed first (SDL3 GPU render passes do not nest).
                endPass();
                if (op.extOwn)
                    op.extOwn(mCmd);
                break;
            }
        }
    }
    endPass();

    // Present: blit fb 0's color onto the acquired swapchain texture. (Headless / hidden window:
    // presentTex may be null — then we skip the blit; rendering to fb 0 still completes, and the
    // frame dump reads fb 0 directly.)
    if (presentTex != nullptr && !mFramebuffers.empty() && mFramebuffers[0].color) {
        FramebufferSDL3& m = mFramebuffers[0];
        SDL_GPUBlitInfo bi{};
        bi.source.texture = m.color;
        bi.source.w = m.width;
        bi.source.h = m.height;
        bi.destination.texture = presentTex;
        bi.destination.w = presentW;
        bi.destination.h = presentH;
        bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        bi.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(mCmd, &bi);
    }
}

void GfxRenderingAPISdl3Gpu::FinishRender() {
    if (!mFrameAcquired)
        return;
    EnsureCommandBuffer();

    SDL_GPUTexture* swap = nullptr;
    uint32_t sw = 0, sh = 0;
    SDL_AcquireGPUSwapchainTexture(mCmd, mWindow, &swap, &sw, &sh);

    ReplayOps(swap, sw, sh);

    // On-demand / scripted frame dump. Reads fb 0 after the frame submit completes.
    const char* dumpPath = nullptr;
    bool exitAfter = false;
    {
        static const char* envDump = getenv("SOH_FRAMEDUMP");
        static const char* envTargetFrame = getenv("SOH_FRAMEDUMP_FRAME");
        static long frame = 0;
        static long targetFrame = envTargetFrame != nullptr ? atol(envTargetFrame) : 300;
        if (envDump != nullptr) {
            ++frame;
            if (frame == targetFrame) {
                dumpPath = envDump;
                exitAfter = true;
            }
        }
        if (gSoh3dDumpPending) {
            dumpPath = gSoh3dDumpPath;
            exitAfter = false;
        }
    }

    const bool wantCapture = gSoh3dCapturePending != 0;
    const bool wantDepthDump = gSoh3dDepthDumpPending != 0;
    if (dumpPath != nullptr || wantCapture || wantDepthDump) {
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(mCmd);
        mCmd = nullptr;
        if (fence) {
            SDL_WaitForGPUFences(mDevice, true, &fence, 1);
            SDL_ReleaseGPUFence(mDevice, fence);
        }
        if (dumpPath != nullptr) {
            WriteFbPpm(0, dumpPath);
            gSoh3dDumpPending = 0;
            if (exitAfter)
                exit(0);
        }
        if (wantDepthDump) {
            WriteFbDepthPpm(0, gSoh3dDepthDumpPath);
            gSoh3dDepthDumpPending = 0;
        }
        if (wantCapture) {
            WriteFbToCaptureBuf(0);
            gSoh3dCapturePending = 0;
        }
    } else {
        SDL_SubmitGPUCommandBuffer(mCmd);
        mCmd = nullptr;
    }

    // The frame's ops are submitted: any texture whose release we deferred this frame is no longer
    // referenced by un-submitted recording, and SDL keeps the GPU memory alive until the submitted
    // work completes. Safe to release now.
    FlushPendingTexReleases();
    mFrameAcquired = false;
    mOps.clear();
    mSoh3dModelUbos.clear();
}

void GfxRenderingAPISdl3Gpu::FlushAndWait() {
    if (!mFrameAcquired)
        return;
    EnsureCommandBuffer();
    ReplayOps(nullptr, 0, 0);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(mCmd);
    mCmd = nullptr;
    if (fence) {
        SDL_WaitForGPUFences(mDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(mDevice, fence);
    }
    // GPU is idle (fence waited): the submitted ops that referenced any deferred-release texture are
    // done, so release them before the frame resumes recording new ops.
    FlushPendingTexReleases();
    // Resume the frame: fresh op list + remapped (cycled) vertex staging. The framebuffers are
    // persistent textures, so already-rendered content is retained and subsequent draws layer on.
    mOps.clear();
    mSoh3dModelUbos.clear();
    mVtxUsed = 0;
    mVtxMapped = (uint8_t*)SDL_MapGPUTransferBuffer(mDevice, mVtxTransfer, true /*cycle*/);
}

// ---------------------------------------------------------------------------
// Frame dump (readback of fb 0's color texture)
// ---------------------------------------------------------------------------

void GfxRenderingAPISdl3Gpu::WriteFbPpm(int fbId, const char* path) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return;
    FramebufferSDL3& fb = mFramebuffers[fbId];
    if (!fb.color)
        return;
    const uint32_t w = fb.width, h = fb.height;
    const uint32_t size = w * h * 4;

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);

    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureRegion reg{};
    reg.texture = fb.color;
    reg.w = w;
    reg.h = h;
    reg.d = 1;
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.offset = 0;
    ti.pixels_per_row = w;
    ti.rows_per_layer = h;
    SDL_DownloadFromGPUTexture(cp, &reg, &ti);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(c);
    if (fence) {
        SDL_WaitForGPUFences(mDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(mDevice, fence);
    }

    const uint8_t* px = (const uint8_t*)SDL_MapGPUTransferBuffer(mDevice, tb, false);
    FILE* f = fopen(path, "wb");
    if (f && px) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t* p = &px[((size_t)y * w + x) * 4]; // R8G8B8A8
                fwrite(p, 1, 3, f);
            }
        }
        SPDLOG_INFO("SDL3 GPU frame dump written: {} ({}x{})", path, w, h);
    } else if (!px) {
        SPDLOG_ERROR("SDL3 GPU frame dump: map failed: {}", SDL_GetError());
    }
    if (f)
        fclose(f);
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
}

// Dump fb's DEPTH buffer (D32_FLOAT) as an auto-contrast grayscale PPM. The raw [0,1] depth is
// crushed near 1.0 (far), so we min/max-stretch over the in-range texels (depth < 1.0, i.e. not
// the cleared far plane) — near geometry = bright, far = dark, cleared = black. Diagnostic only.
void GfxRenderingAPISdl3Gpu::WriteFbDepthPpm(int fbId, const char* path) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return;
    FramebufferSDL3& fb = mFramebuffers[fbId];
    if (!fb.depth)
        return;
    const uint32_t w = fb.width, h = fb.height;
    const uint32_t size = w * h * (uint32_t)sizeof(float);

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);

    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureRegion reg{};
    reg.texture = fb.depth;
    reg.w = w;
    reg.h = h;
    reg.d = 1;
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.offset = 0;
    ti.pixels_per_row = w;
    ti.rows_per_layer = h;
    SDL_DownloadFromGPUTexture(cp, &reg, &ti);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(c);
    if (fence) {
        SDL_WaitForGPUFences(mDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(mDevice, fence);
    }

    const float* dz = (const float*)SDL_MapGPUTransferBuffer(mDevice, tb, false);
    FILE* f = fopen(path, "wb");
    if (f && dz) {
        float lo = 1.0f, hi = 0.0f;
        for (uint32_t i = 0; i < w * h; i++) {
            float d = dz[i];
            if (d < 1.0f) {
                lo = std::min(lo, d);
                hi = std::max(hi, d);
            }
        }
        const float range = (hi > lo) ? (hi - lo) : 1.0f;
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint32_t i = 0; i < w * h; i++) {
            float d = dz[i];
            uint8_t g = (d >= 1.0f) ? 0 : (uint8_t)(255.0f * (1.0f - (d - lo) / range));
            uint8_t rgb[3] = { g, g, g };
            fwrite(rgb, 1, 3, f);
        }
        SPDLOG_INFO("SDL3 GPU depth dump written: {} ({}x{}) depth[{},{}]", path, w, h, lo, hi);
    } else if (!dz) {
        SPDLOG_ERROR("SDL3 GPU depth dump: map failed: {}", SDL_GetError());
    }
    if (f)
        fclose(f);
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
}

void GfxRenderingAPISdl3Gpu::WriteFbToCaptureBuf(int fbId) {
    // Stamp introspection state EVERY call so `diag` can see why capture
    // came up empty. Written before any early-return.
    gSoh3dFb0LastCaptureAttempt = (int)((int)gSoh3dFb0LastCaptureAttempt + 1);
    gSoh3dFb0LastInRange = (fbId >= 0 && fbId < (int)mFramebuffers.size()) ? 1 : 0;
    if (!gSoh3dFb0LastInRange)
        return;
    FramebufferSDL3& fb = mFramebuffers[fbId];
    gSoh3dFb0LastHasColor = (fb.color != nullptr) ? 1 : 0;
    gSoh3dFb0LastW = fb.width;
    gSoh3dFb0LastH = fb.height;
    if (!fb.color)
        return;
    const uint32_t w = fb.width, h = fb.height;
    const uint32_t size = w * h * 4;
    if (gSoh3dCaptureBuf == nullptr || gSoh3dCaptureCap < size) {
        SPDLOG_WARN("SoH3D capture: buffer too small ({} < {}) or null; skipping", (unsigned)gSoh3dCaptureCap,
                    (unsigned)size);
        return;
    }

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);

    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureRegion reg{};
    reg.texture = fb.color;
    reg.w = w;
    reg.h = h;
    reg.d = 1;
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.offset = 0;
    ti.pixels_per_row = w;
    ti.rows_per_layer = h;
    SDL_DownloadFromGPUTexture(cp, &reg, &ti);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(c);
    if (fence) {
        SDL_WaitForGPUFences(mDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(mDevice, fence);
    }

    const uint8_t* px = (const uint8_t*)SDL_MapGPUTransferBuffer(mDevice, tb, false);
    if (px) {
        std::memcpy(gSoh3dCaptureBuf, px, size);
        gSoh3dCaptureW = w;
        gSoh3dCaptureH = h;
    } else {
        SPDLOG_ERROR("SoH3D capture: map failed: {}", SDL_GetError());
    }
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
}

void GfxRenderingAPISdl3Gpu::MaybeDumpFrame() {
    // Folded into FinishRender (needs the swapchain present + submit completed first).
}

// ---------------------------------------------------------------------------
// Samplers / dummy
// ---------------------------------------------------------------------------

SDL_GPUSampler* GfxRenderingAPISdl3Gpu::GetOrCreateSampler(bool linear, uint32_t cms, uint32_t cmt) {
    auto wrap = [](uint32_t cm) -> SDL_GPUSamplerAddressMode {
        switch (cm) {
            case G_TX_NOMIRROR | G_TX_CLAMP:
                return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            case G_TX_MIRROR | G_TX_WRAP:
            case G_TX_MIRROR | G_TX_CLAMP:
                // SDL3 GPU has no mirror-clamp-to-edge; mirrored repeat is the closest match.
                return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
            case G_TX_NOMIRROR | G_TX_WRAP:
            default:
                return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        }
    };
    SDL_GPUFilter f = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    return GetOrCreateSamplerEx(f, f, SDL_GPU_SAMPLERMIPMAPMODE_NEAREST, wrap(cms), wrap(cmt), 0.0f);
}

// The one sampler factory + cache for the whole backend, keyed on the fully-resolved SDL params so
// the N64 wrapper above and the Zelda3D model path (LINEAR + max_lod=1000) share it without colliding
// (the differing max_lod lands in a distinct cache slot). max_lod is bucketed to {0, nonzero}: the
// only two values in use are 0 (N64) and 1000 (Zelda3D), and a non-mipmapped texture is unaffected by
// any positive max_lod, so one "uncapped" bucket suffices.
SDL_GPUSampler* GfxRenderingAPISdl3Gpu::GetOrCreateSamplerEx(SDL_GPUFilter minFilter, SDL_GPUFilter magFilter,
                                                             SDL_GPUSamplerMipmapMode mipmapMode,
                                                             SDL_GPUSamplerAddressMode u, SDL_GPUSamplerAddressMode v,
                                                             float maxLod) {
    uint32_t key = (uint32_t)minFilter | ((uint32_t)magFilter << 2) | ((uint32_t)mipmapMode << 4) | ((uint32_t)u << 6) |
                   ((uint32_t)v << 10) | ((maxLod > 0.0f ? 1u : 0u) << 14);
    auto it = mSamplerCache.find(key);
    if (it != mSamplerCache.end())
        return it->second;
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = minFilter;
    si.mag_filter = magFilter;
    si.mipmap_mode = mipmapMode;
    si.address_mode_u = u;
    si.address_mode_v = v;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.max_lod = maxLod;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(mDevice, &si);
    if (sampler == nullptr) {
        // Don't cache a null — binding a null sampler faults inside the GPU driver
        // (BindFragmentSamplers dereferences it, notably on macOS/MoltenVK). Surface it instead.
        SPDLOG_ERROR("SDL_CreateGPUSampler failed (min={}, mag={}, mip={}, u={}, v={}, maxLod={}): {}", (int)minFilter,
                     (int)magFilter, (int)mipmapMode, (int)u, (int)v, maxLod, SDL_GetError());
        return nullptr;
    }
    mSamplerCache[key] = sampler;
    return sampler;
}

SDL_GPUTexture* GfxRenderingAPISdl3Gpu::DummyTexture() {
    if (mDummyTex)
        return mDummyTex;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = 1;
    ci.height = 1;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    mDummyTex = SDL_CreateGPUTexture(mDevice, &ci);
    if (mDummyTex == nullptr) {
        // The dummy backs every unbound/missing texture slot; a null here would propagate into a
        // fragment-sampler binding and fault inside the GPU driver. Surface the failure.
        SPDLOG_ERROR("SDL_CreateGPUTexture (dummy) failed: {}", SDL_GetError());
        return nullptr;
    }

    const uint8_t white[4] = { 255, 255, 255, 255 };
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);
    void* mapped = SDL_MapGPUTransferBuffer(mDevice, tb, false);
    memcpy(mapped, white, 4);
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.pixels_per_row = 1;
    ti.rows_per_layer = 1;
    SDL_GPUTextureRegion reg{};
    reg.texture = mDummyTex;
    reg.w = 1;
    reg.h = 1;
    reg.d = 1;
    SDL_UploadToGPUTexture(cp, &ti, &reg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(c);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
    return mDummyTex;
}

SDL_GPUSampler* GfxRenderingAPISdl3Gpu::DummySampler() {
    if (!mDummySampler)
        mDummySampler = GetOrCreateSampler(false, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP);
    return mDummySampler;
}

// ---------------------------------------------------------------------------
// Shaders / pipelines
// ---------------------------------------------------------------------------

std::string GfxRenderingAPISdl3Gpu::BuildShaderSource(const CCFeatures& cc, bool vertex, ShaderProgramSDL3* prg) {
    if (vertex) {
        gSgAttrLoc = 0;
        gSgVaryLoc = 0;
    } else {
        gSgVaryLoc = 0;
        gSgSamplerBind = 0;
    }
    prism::Processor processor;
    prism::ContextItems ctx = {
        { "VERTEX_SHADER", vertex },
        { "o_c", M_ARRAY(cc.c, int, 2, 2, 4) },
        { "o_alpha", cc.opt_alpha },
        { "o_fog", cc.opt_fog },
        { "o_texture_edge", cc.opt_texture_edge },
        { "o_noise", cc.opt_noise },
        { "o_2cyc", cc.opt_2cyc },
        { "o_alpha_threshold", cc.opt_alpha_threshold },
        { "o_invisible", cc.opt_invisible },
        { "o_grayscale", cc.opt_grayscale },
        { "o_prim_depth", cc.opt_prim_depth },
        { "o_textures", M_ARRAY(cc.usedTextures, bool, 2) },
        { "o_masks", M_ARRAY(cc.used_masks, bool, 2) },
        { "o_blend", M_ARRAY(cc.used_blend, bool, 2) },
        { "o_clamp", M_ARRAY(cc.clamp, bool, 2, 2) },
        { "o_inputs", cc.numInputs },
        { "o_do_mix", M_ARRAY(cc.do_mix, bool, 2, 2) },
        { "o_do_single", M_ARRAY(cc.do_single, bool, 2, 2) },
        { "o_do_multiply", M_ARRAY(cc.do_multiply, bool, 2, 2) },
        { "o_color_alpha_same", M_ARRAY(cc.color_alpha_same, bool, 2) },
        { "FILTER_THREE_POINT", FILTER_THREE_POINT },
        { "FILTER_LINEAR", FILTER_LINEAR },
        { "FILTER_NONE", FILTER_NONE },
        { "srgb_mode", mSrgbMode },
        { "SHADER_0", SHADER_0 },
        { "SHADER_INPUT_1", SHADER_INPUT_1 },
        { "SHADER_INPUT_2", SHADER_INPUT_2 },
        { "SHADER_INPUT_3", SHADER_INPUT_3 },
        { "SHADER_INPUT_4", SHADER_INPUT_4 },
        { "SHADER_INPUT_5", SHADER_INPUT_5 },
        { "SHADER_INPUT_6", SHADER_INPUT_6 },
        { "SHADER_INPUT_7", SHADER_INPUT_7 },
        { "SHADER_TEXEL0", SHADER_TEXEL0 },
        { "SHADER_TEXEL0A", SHADER_TEXEL0A },
        { "SHADER_TEXEL1", SHADER_TEXEL1 },
        { "SHADER_TEXEL1A", SHADER_TEXEL1A },
        { "SHADER_1", SHADER_1 },
        { "SHADER_COMBINED", SHADER_COMBINED },
        { "SHADER_NOISE", SHADER_NOISE },
        { "o_three_point_filtering", mCurrentFilterMode == FILTER_THREE_POINT },
        { "append_formula", (InvokeFunc)sg_append_formula },
        { "aloc", (InvokeFunc)sg_aloc },
        { "vloc", (InvokeFunc)sg_vloc },
        { "sloc", (InvokeFunc)sg_sloc },
    };
    processor.populate(ctx);
    processor.load(kSgShaderTemplate);
    processor.bind_include_loader(sg_include_noop);
    return processor.process();
}

SDL_GPUShader* GfxRenderingAPISdl3Gpu::CreateShader(const std::vector<uint32_t>& spirv, bool vertex,
                                                    uint32_t numSamplers, uint32_t numUniformBuffers) {
    SDL_GPUShaderCreateInfo ci{};
    ci.code_size = spirv.size() * sizeof(uint32_t);
    ci.code = (const Uint8*)spirv.data();
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = numSamplers;
    ci.num_storage_textures = 0;
    ci.num_storage_buffers = 0;
    ci.num_uniform_buffers = numUniformBuffers;
    SDL_GPUShader* sh = SDL_CreateGPUShader(mDevice, &ci);
    if (sh == nullptr) {
        SPDLOG_ERROR("SDL_CreateGPUShader failed ({}): {}", vertex ? "vert" : "frag", SDL_GetError());
        abort();
    }
    return sh;
}

void GfxRenderingAPISdl3Gpu::ClearShaderCache() {
    SDL_WaitForGPUIdle(mDevice);
    for (auto& kv : mPipelineCache)
        SDL_ReleaseGPUGraphicsPipeline(mDevice, kv.second);
    mPipelineCache.clear();
    for (auto& kv : mShaderProgramPool) {
        if (kv.second.vert)
            SDL_ReleaseGPUShader(mDevice, kv.second.vert);
        if (kv.second.frag)
            SDL_ReleaseGPUShader(mDevice, kv.second.frag);
    }
    mShaderProgramPool.clear();
}

ShaderProgram* GfxRenderingAPISdl3Gpu::CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) {
    CCFeatures cc{};
    gfx_cc_get_features(shaderId0, shaderId1, &cc);

    ShaderProgramSDL3& prg = mShaderProgramPool[std::make_pair(shaderId0, shaderId1)];
    prg.id0 = shaderId0;
    prg.id1 = shaderId1;
    prg.cc = cc; // render-unification Phase 3 groundwork (kanban #131) — see the field's doc comment
    prg.numInputs = cc.numInputs;
    prg.usedTextures[0] = cc.usedTextures[0];
    prg.usedTextures[1] = cc.usedTextures[1];
    prg.usedSlot[0] = cc.usedTextures[0];
    prg.usedSlot[1] = cc.usedTextures[1];
    prg.usedSlot[2] = cc.used_masks[0];
    prg.usedSlot[3] = cc.used_masks[1];
    prg.usedSlot[4] = cc.used_blend[0];
    prg.usedSlot[5] = cc.used_blend[1];
    prg.numSamplers = 0;
    for (int i = 0; i < 6; i++)
        if (prg.usedSlot[i])
            prg.numSamplers++;

    // Vertex-attribute layout in the SAME order the template emits layout(location=) qualifiers.
    uint32_t floatOffset = 0;
    auto add = [&](uint32_t size) {
        prg.attribs.push_back({ size, floatOffset * (uint32_t)sizeof(float) });
        floatOffset += size;
    };
    add(4); // aVtxPos
    for (int i = 0; i < 2; i++) {
        if (cc.usedTextures[i]) {
            add(2);
            for (int j = 0; j < 2; j++) {
                if (cc.clamp[i][j])
                    add(1);
            }
        }
    }
    if (cc.opt_fog)
        add(4);
    if (cc.opt_grayscale)
        add(4);
    for (int i = 0; i < cc.numInputs; i++)
        add(cc.opt_alpha ? 4 : 3);
    prg.numFloats = (uint8_t)floatOffset;

    std::string vsSrc = BuildShaderSource(cc, true, &prg);
    std::string fsSrc = BuildShaderSource(cc, false, &prg);

    std::vector<uint32_t> vsSpv, fsSpv;
    std::string log;
    if (!CompileGlslToSpirv(EShLangVertex, vsSrc, vsSpv, log)) {
        SPDLOG_ERROR("SDL3 GPU VS compile failed (shader {:#x}): {}\n--- source ---\n{}", cc.shader_id, log, vsSrc);
        abort();
    }
    if (!CompileGlslToSpirv(EShLangFragment, fsSrc, fsSpv, log)) {
        SPDLOG_ERROR("SDL3 GPU FS compile failed (shader {:#x}): {}\n--- source ---\n{}", cc.shader_id, log, fsSrc);
        abort();
    }
    prg.vert = CreateShader(vsSpv, true, 0, 0);
    prg.frag = CreateShader(fsSpv, false, prg.numSamplers, 1);

    mCurrentShaderProgram = &prg;
    return reinterpret_cast<ShaderProgram*>(&prg);
}

ShaderProgram* GfxRenderingAPISdl3Gpu::LookupShader(uint64_t shaderId0, uint64_t shaderId1) {
    auto it = mShaderProgramPool.find(std::make_pair(shaderId0, shaderId1));
    return it == mShaderProgramPool.end() ? nullptr : reinterpret_cast<ShaderProgram*>(&it->second);
}

void GfxRenderingAPISdl3Gpu::ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    auto* sg = reinterpret_cast<ShaderProgramSDL3*>(prg);
    if (sg == nullptr) {
        *numInputs = 0;
        usedTextures[0] = usedTextures[1] = false;
        return;
    }
    *numInputs = sg->numInputs;
    usedTextures[0] = sg->usedTextures[0];
    usedTextures[1] = sg->usedTextures[1];
}

void GfxRenderingAPISdl3Gpu::LoadShader(ShaderProgram* prg) {
    mCurrentShaderProgram = reinterpret_cast<ShaderProgramSDL3*>(prg);
}
void GfxRenderingAPISdl3Gpu::UnloadShader(ShaderProgram*) {
}

SDL_GPUGraphicsPipeline* GfxRenderingAPISdl3Gpu::GetOrCreatePipeline(ShaderProgramSDL3* prg, uint32_t stateBits) {
    Sdl3PipelineKey key{ prg->id0, prg->id1, stateBits };
    auto it = mPipelineCache.find(key);
    if (it != mPipelineCache.end())
        return it->second;

    const bool depthTest = (stateBits & 1) != 0;
    const bool depthMask = (stateBits & 2) != 0;
    const bool zmodeDecal = (stateBits & 4) != 0;
    const bool useAlpha = (stateBits & 8) != 0;

    std::vector<SDL_GPUVertexAttribute> attrs(prg->attribs.size());
    for (size_t i = 0; i < prg->attribs.size(); i++) {
        SDL_GPUVertexElementFormat fmt = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        switch (prg->attribs[i].size) {
            case 1:
                fmt = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
                break;
            case 2:
                fmt = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
                break;
            case 3:
                fmt = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
                break;
            case 4:
                fmt = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
                break;
        }
        attrs[i].location = (uint32_t)i;
        attrs[i].buffer_slot = 0;
        attrs[i].format = fmt;
        attrs[i].offset = prg->attribs[i].offset;
    }
    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = prg->numFloats * sizeof(float);
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbDesc.instance_step_rate = 0;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = prg->vert;
    pci.fragment_shader = prg->frag;
    pci.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs.data();
    pci.vertex_input_state.num_vertex_attributes = (uint32_t)attrs.size();
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // Fast3D culls on the CPU
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state.enable_depth_clip = false; // depth clamp, matches GL GL_DEPTH_CLAMP
    // No dynamic depth bias in SDL3 GPU: bake the zmode-decal slope-scaled offset into the pipeline
    // (matches the GL/Vulkan backends' vkCmdSetDepthBias(-2, 0, -2)). zmodeDecal is part of the key.
    pci.rasterizer_state.enable_depth_bias = zmodeDecal;
    pci.rasterizer_state.depth_bias_constant_factor = zmodeDecal ? -2.0f : 0.0f;
    pci.rasterizer_state.depth_bias_clamp = 0.0f;
    pci.rasterizer_state.depth_bias_slope_factor = zmodeDecal ? -2.0f : 0.0f;

    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    if (depthTest || depthMask) {
        pci.depth_stencil_state.enable_depth_test = true;
        pci.depth_stencil_state.enable_depth_write = depthMask;
        pci.depth_stencil_state.compare_op =
            depthTest ? (zmodeDecal ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL : SDL_GPU_COMPAREOP_LESS)
                      : SDL_GPU_COMPAREOP_ALWAYS;
    } else {
        pci.depth_stencil_state.enable_depth_test = false;
        pci.depth_stencil_state.enable_depth_write = false;
        pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    }
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription colorDesc{};
    colorDesc.format = mColorFormat;
    if (useAlpha) {
        colorDesc.blend_state.enable_blend = true;
        colorDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colorDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        colorDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colorDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    } else {
        colorDesc.blend_state.enable_blend = false;
    }
    pci.target_info.color_target_descriptions = &colorDesc;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = mDepthFormat;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(mDevice, &pci);
    if (pipeline == nullptr) {
        SPDLOG_ERROR("SDL_CreateGPUGraphicsPipeline failed: {}", SDL_GetError());
        abort();
    }
    mPipelineCache[key] = pipeline;
    return pipeline;
}

// ---------------------------------------------------------------------------
// Render-unification effort (kanban #131), Phase 3: N64 draws through the unified shader
// (`gUnifiedRenderer & 2`), mirroring GetOrCreatePipeline's depth/blend/cull mapping (N64
// always culls on the CPU, front-face fixed CCW, blend is fixed standard alpha-over when
// useAlpha); only UnifiedVtx's 13 attributes and the per-variant unified shader pair differ.
// ---------------------------------------------------------------------------
SDL_GPUGraphicsPipeline* GfxRenderingAPISdl3Gpu::GetOrCreateUnifiedN64Pipeline(int variant, uint32_t stateBits) {
    uint32_t key = (uint32_t)variant * 16u + stateBits;
    auto it = mUniN64PipelineCache.find(key);
    if (it != mUniN64PipelineCache.end())
        return it->second;

    if (mUniN64Vert[variant] == nullptr) {
        std::string vsrc = Fast::Unified::BuildVertexSource((Fast::Unified::Variant)variant);
        std::string fsrc = Fast::Unified::BuildFragmentSource((Fast::Unified::Variant)variant);
        std::vector<uint32_t> vspv, fspv;
        std::string log;
        bool ok = CompileGlslToSpirv(EShLangVertex, vsrc, vspv, log);
        if (ok)
            ok = CompileGlslToSpirv(EShLangFragment, fsrc, fspv, log);
        if (!ok) {
            SPDLOG_ERROR("[unified N64] shader variant {} compile FAILED: {}", variant, log);
            return nullptr;
        }
        // 2 UBOs: UnifiedCommon (binding 0) + UnifiedBones (binding 1) — the vertex shader declares
        // both even though N64 never uses the bones one (unlike the CMB path's getUnifiedPipeline,
        // which passes the same 2 here).
        mUniN64Vert[variant] = CreateShader(vspv, true, 0, 2);
        uint32_t numSamplers =
            (variant == (int)Fast::Unified::Variant::kDualTex || variant == (int)Fast::Unified::Variant::kDualTexFog)
                ? 2
                : (variant == (int)Fast::Unified::Variant::kUntextured ? 0 : 1);
        mUniN64Frag[variant] = CreateShader(fspv, false, numSamplers, 1);
    }
    if (!mUniN64Vert[variant] || !mUniN64Frag[variant])
        return nullptr;

    const bool depthTest = (stateBits & 1) != 0;
    const bool depthMask = (stateBits & 2) != 0;
    const bool zmodeDecal = (stateBits & 4) != 0;
    const bool useAlpha = (stateBits & 8) != 0;

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
    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(UnifiedVtx);
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = mUniN64Vert[variant];
    pci.fragment_shader = mUniN64Frag[variant];
    pci.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 13;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // Fast3D culls on the CPU
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state.enable_depth_clip = false;
    pci.rasterizer_state.enable_depth_bias = zmodeDecal;
    pci.rasterizer_state.depth_bias_constant_factor = zmodeDecal ? -2.0f : 0.0f;
    pci.rasterizer_state.depth_bias_clamp = 0.0f;
    pci.rasterizer_state.depth_bias_slope_factor = zmodeDecal ? -2.0f : 0.0f;

    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    if (depthTest || depthMask) {
        pci.depth_stencil_state.enable_depth_test = true;
        pci.depth_stencil_state.enable_depth_write = depthMask;
        pci.depth_stencil_state.compare_op =
            depthTest ? (zmodeDecal ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL : SDL_GPU_COMPAREOP_LESS)
                      : SDL_GPU_COMPAREOP_ALWAYS;
    } else {
        pci.depth_stencil_state.enable_depth_test = false;
        pci.depth_stencil_state.enable_depth_write = false;
        pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    }
    pci.depth_stencil_state.enable_stencil_test = false;

    SDL_GPUColorTargetDescription colorDesc{};
    colorDesc.format = mColorFormat;
    if (useAlpha) {
        colorDesc.blend_state.enable_blend = true;
        colorDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colorDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        colorDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colorDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    } else {
        colorDesc.blend_state.enable_blend = false;
    }
    pci.target_info.color_target_descriptions = &colorDesc;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = mDepthFormat;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(mDevice, &pci);
    if (!pipeline)
        SPDLOG_ERROR("[unified N64] pipeline create failed: {}", SDL_GetError());
    mUniN64PipelineCache[key] = pipeline;
    return pipeline;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

int GfxRenderingAPISdl3Gpu::GetMaxTextureSize() {
    return mMaxTextureSize;
}

GfxClipParameters GfxRenderingAPISdl3Gpu::GetClipParameters() {
    // SDL3 GPU clip space (per SDL_gpu.h "Coordinate System"): NDC lower-left = (-1,-1), upper-right
    // = (1,1) -> +Y is UP (the OpenGL convention), with Z in [0,1]. SDL converts to each backend's
    // native NDC (e.g. Vulkan's Y-down) internally, so the app must NOT pre-flip Y the way the raw
    // Vulkan backend does. Hence: z_is_from_0_to_1 = true (like Vulkan), but invertY follows the
    // GL convention (the FB's openglInvertY flag, NOT its inverse). Targeting Vulkan-style Y-down
    // here renders the whole frame vertically flipped.
    int fb = (mCurrentFb >= 0 && mCurrentFb < (int)mFramebuffers.size()) ? mCurrentFb : 0;
    return { true, mFramebuffers[fb].invertY };
}

uint32_t GfxRenderingAPISdl3Gpu::NewTexture() {
    uint32_t id = mNextTextureId++;
    if (id >= mTextures.size())
        mTextures.resize(id + 1);
    return id;
}

void GfxRenderingAPISdl3Gpu::SelectTexture(int tile, uint32_t textureId) {
    if (tile < 0 || tile >= 6)
        return;
    mCurrentTextureIds[tile] = textureId;
    mCurrentTile = (uint8_t)tile;
}

void GfxRenderingAPISdl3Gpu::UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return;
    uint32_t id = mCurrentTextureIds[mCurrentTile];
    if (id >= mTextures.size())
        mTextures.resize(id + 1);
    TextureSDL3& t = mTextures[id];
    if (t.tex == nullptr || t.width != width || t.height != height) {
        if (t.tex && !t.isFbAlias) {
            // A texture re-uploaded at a new size (the engine reused this id for different texels this
            // frame) must keep its OLD GPU handle alive until the frame's command buffer is submitted:
            // ops ALREADY recorded this frame captured the old pointer and will bind it at replay time,
            // so releasing it now is a use-after-free. Proven on macOS/MoltenVK (BindFragmentSamplers
            // faults on the freed handle; refByOp>=0 below). DeferReleaseTexture handles the lifetime.
            static const bool kDbgOpDraw = getenv("ZELDA3D_DBG_OPDRAW") != nullptr;
            if (kDbgOpDraw) {
                int refOp = -1;
                for (size_t oi = 0; oi < mOps.size(); oi++)
                    for (uint32_t si = 0; si < mOps[oi].numSamplers; si++)
                        if (mOps[oi].samplers[si].texture == t.tex)
                            refOp = (int)oi;
                fprintf(stderr,
                        "[DBG_TEXREL] defer-release tex id=%u ptr=%p (resize %ux%u->%ux%u) refByOp=%d ops=%zu\n", id,
                        (void*)t.tex, t.width, t.height, width, height, refOp, mOps.size());
                fflush(stderr);
            }
            DeferReleaseTexture(t.tex);
        }
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ci.width = width;
        ci.height = height;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        t.tex = SDL_CreateGPUTexture(mDevice, &ci);
        t.width = width;
        t.height = height;
        t.isFbAlias = false;
    }

    const uint32_t size = width * height * 4;
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);
    void* mapped = SDL_MapGPUTransferBuffer(mDevice, tb, false);
    memcpy(mapped, rgba32Buf, size);
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    // Separate command buffer for the upload. SDL3 GPU tracks resource hazards across submissions,
    // so the frame command buffer (submitted later) sees the uploaded texels without an explicit
    // fence/wait here.
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.pixels_per_row = width;
    ti.rows_per_layer = height;
    SDL_GPUTextureRegion reg{};
    reg.texture = t.tex;
    reg.w = width;
    reg.h = height;
    reg.d = 1;
    SDL_UploadToGPUTexture(cp, &ti, &reg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(c);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
    t.uploaded = true;
}

void GfxRenderingAPISdl3Gpu::SetSamplerParameters(int tile, bool linearFilter, uint32_t cms, uint32_t cmt) {
    if (tile < 0 || tile >= 6)
        return;
    uint32_t id = mCurrentTextureIds[tile];
    if (id >= mTextures.size())
        return;
    TextureSDL3& t = mTextures[id];
    t.linearFilter = linearFilter && mCurrentFilterMode == FILTER_LINEAR;
    t.filtering = !linearFilter ? FILTER_LINEAR : FILTER_THREE_POINT;
    t.cms = cms;
    t.cmt = cmt;
}

void GfxRenderingAPISdl3Gpu::DeleteTexture(uint32_t texId) {
    if (texId >= mTextures.size())
        return;
    TextureSDL3& t = mTextures[texId];
    if (t.isFbAlias || t.tex == nullptr)
        return;
    // Same lifetime hazard as the UploadTexture resize path: a delete mid-frame can free a handle a
    // recorded op still binds. Defer until the frame is submitted.
    DeferReleaseTexture(t.tex);
    t = TextureSDL3{};
}

// Release a GPU texture safely w.r.t. the deferred op-list. While a frame is recording, the op-list
// may hold this handle (an op captured it at DrawTriangles time and binds it at ReplayOps time), so
// the release MUST wait until that command buffer is submitted — otherwise the bind dereferences a
// freed texture (the macOS/MoltenVK BindFragmentSamplers crash). Outside frame recording there are no
// pending ops that reference it (any are already submitted, where SDL's own deferred-free is safe),
// so release immediately.
void GfxRenderingAPISdl3Gpu::DeferReleaseTexture(SDL_GPUTexture* tex) {
    if (tex == nullptr)
        return;
    if (mFrameAcquired)
        mPendingTexRelease.push_back(tex);
    else if (NoteGpuRelease(tex, "texture (immediate)"))
        SDL_ReleaseGPUTexture(mDevice, tex);
}

// Release every texture whose destruction was deferred during the just-submitted (or just-waited)
// frame. Called only at points where the ops that referenced them have been submitted to the GPU.
void GfxRenderingAPISdl3Gpu::FlushPendingTexReleases() {
    for (SDL_GPUTexture* tex : mPendingTexRelease)
        if (NoteGpuRelease(tex, "deferred texture"))
            SDL_ReleaseGPUTexture(mDevice, tex);
    mPendingTexRelease.clear();
}

// ---------------------------------------------------------------------------
// Render state
// ---------------------------------------------------------------------------

void GfxRenderingAPISdl3Gpu::SetDepthTestAndMask(bool depthTest, bool zUpd) {
    mCurrentDepthTest = depthTest;
    mCurrentDepthMask = zUpd;
}
void GfxRenderingAPISdl3Gpu::SetZmodeDecal(bool decal) {
    mCurrentZmodeDecal = decal;
}
void GfxRenderingAPISdl3Gpu::SetViewport(int x, int y, int width, int height) {
    mCurrentViewport.x = (float)x;
    mCurrentViewport.y = (float)y;
    mCurrentViewport.w = (float)width;
    mCurrentViewport.h = (float)height;
    mCurrentViewport.min_depth = 0.0f;
    mCurrentViewport.max_depth = 1.0f;
}
void GfxRenderingAPISdl3Gpu::SetScissor(int x, int y, int width, int height) {
    mCurrentScissor.x = std::max(0, x);
    mCurrentScissor.y = std::max(0, y);
    mCurrentScissor.w = std::max(0, width);
    mCurrentScissor.h = std::max(0, height);
}
void GfxRenderingAPISdl3Gpu::SetUseAlpha(bool useAlpha) {
    mCurrentUseAlpha = useAlpha;
}

// ---------------------------------------------------------------------------
// Render-unification effort (kanban #131), Phase 3: convert the OLD path's already-correctly-
// packed per-vertex floats (bufVbo, laid out per ShaderProgramSDL3::attribs) into UnifiedVtx,
// using the CCFeatures this program was built from (prg->cc, stored since the Phase 3 groundwork
// commit) to know each float's meaning. Deliberately does NOT touch interpreter.cpp's tri-emit
// packing (the hot path for every N64 triangle) — this reuses values the old path already computed
// correctly (PRIM/ENV/LOD-fraction colors are pre-resolved into concrete RGB(A) floats by the time
// they reach bufVbo, so no RDP-register access is needed here).
namespace {

Fast::Unified::Variant N64VariantFor(const CCFeatures& cc) {
    if (cc.opt_grayscale)
        return Fast::Unified::Variant::kGrayscale;
    bool tex0 = cc.usedTextures[0], tex1 = cc.usedTextures[1];
    if (tex0 && tex1)
        // Known residual: dual-tex + alpha-threshold/texture-edge has no dedicated variant (the
        // plan caps at 6 structural buckets) — falls through to plain dual-tex, skipping the
        // discard. Rare in practice (Phase 0 corpus: 2-texture draws needing edge/threshold discard
        // are a small minority of the already-small texture-edge set) and never silently WRONG in a
        // worse way than "renders opaque instead of cutout" for those few draws.
        return cc.opt_fog ? Fast::Unified::Variant::kDualTexFog : Fast::Unified::Variant::kDualTex;
    if (!tex0 && !tex1)
        return Fast::Unified::Variant::kUntextured;
    return (cc.opt_alpha_threshold || cc.opt_texture_edge) ? Fast::Unified::Variant::kSingleTexAlphaTest
                                                           : Fast::Unified::Variant::kSingleTex;
}

// Reads one vertex's floats (already resolved concrete color values, not combiner codes — see the
// header comment above) starting at `vf`, in the SAME order CreateAndLoadNewShader's `add()` calls
// declared them (interpreter.cpp's tri-emit loop packs in this identical order).
UnifiedVtx PackN64VertexToUnified(const float* vf, const CCFeatures& cc, float outFogColor[3]) {
    UnifiedVtx u{};
    int idx = 0;
    u.pos[0] = vf[idx++];
    u.pos[1] = vf[idx++];
    u.pos[2] = vf[idx++];
    u.pos[3] = vf[idx++];
    u.nrm[0] = 0.0f;
    u.nrm[1] = 0.0f;
    u.nrm[2] = 1.0f; // N64 has no real per-vertex normal
    float uv[2][2] = { { 0, 0 }, { 0, 0 } };
    float clampS[2] = { 1e6f, 1e6f }, clampT[2] = { 1e6f, 1e6f };
    for (int t = 0; t < 2; t++) {
        if (cc.usedTextures[t]) {
            uv[t][0] = vf[idx++];
            uv[t][1] = vf[idx++];
            if (cc.clamp[t][0])
                clampS[t] = vf[idx++];
            if (cc.clamp[t][1])
                clampT[t] = vf[idx++];
        }
    }
    u.uv0[0] = uv[0][0];
    u.uv0[1] = uv[0][1];
    u.uv1[0] = uv[1][0];
    u.uv1[1] = uv[1][1];
    u.texClamp[0] = clampS[0];
    u.texClamp[1] = clampT[0];
    u.texClamp[2] = clampS[1];
    u.texClamp[3] = clampT[1];

    // Fog: interpreter.cpp packs {r,g,b,factor} per vertex, but r/g/b come from the RDP's fog_color
    // register — constant across the whole draw, not really per-vertex. Read once (any vertex) into
    // outFogColor (caller puts it in the per-draw UBO); only the 4th float (factor) is genuinely
    // per-vertex and lives in UnifiedVtx.fog[0].
    float fogFactor = 0.0f;
    if (cc.opt_fog) {
        outFogColor[0] = vf[idx++];
        outFogColor[1] = vf[idx++];
        outFogColor[2] = vf[idx++];
        fogFactor = vf[idx++];
    }
    u.fog[0] = fogFactor;
    u.fog[1] = 0.0f;

    // Grayscale: same per-draw-constant-register shape as fog (mRdp->grayscale_color.rgba, all 4
    // components from the SAME register). The unified shader's grayscale variant currently uses a
    // generic luma approximation (documented residual, not a real port of this blend-toward-color
    // mechanism) — skip the 4 floats here to keep numInputs parsing aligned, don't use the values.
    if (cc.opt_grayscale)
        idx += 4;

    for (int j = 0; j < cc.numInputs && j < 4; j++) {
        uint8_t* slot = (j == 0) ? u.color0 : (j == 1) ? u.color1 : (j == 2) ? u.color2 : u.color3;
        for (int k = 0; k < 3; k++)
            slot[k] = (uint8_t)std::lround(std::clamp(vf[idx + k], 0.0f, 1.0f) * 255.0f);
        idx += 3;
        if (cc.opt_alpha) {
            slot[3] = (uint8_t)std::lround(std::clamp(vf[idx], 0.0f, 1.0f) * 255.0f);
            idx += 1;
        } else {
            slot[3] = 255;
        }
    }
    // Bone data: N64 has no GPU skinning — identity (100% bone 0, which the unified vertex shader
    // never reads for N64 anyway since UnifiedMaterial.alreadyTransformed skips the whole branch).
    u.boneIds[0] = u.boneIds[1] = u.boneIds[2] = u.boneIds[3] = 0;
    u.boneW[0] = 255;
    u.boneW[1] = u.boneW[2] = u.boneW[3] = 0;
    return u;
}

} // namespace

void GfxRenderingAPISdl3Gpu::DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) {
    if (!mFrameAcquired || mCurrentShaderProgram == nullptr || mCurrentShaderProgram->frag == nullptr)
        return;
    if (mCurrentFb < 0 || mCurrentFb >= (int)mFramebuffers.size())
        return;
    FramebufferSDL3& f = mFramebuffers[mCurrentFb];
    if (!f.color || !f.depth)
        return; // current FB is not a drawable render target

    ShaderProgramSDL3* prg = mCurrentShaderProgram;
    const CCFeatures& cc = prg->cc;
    const uint32_t stateBits = (mCurrentDepthTest ? 1u : 0u) | (mCurrentDepthMask ? 2u : 0u) |
                               (mCurrentZmodeDecal ? 4u : 0u) | (mCurrentUseAlpha ? 8u : 0u);
    const uint32_t numVerts = 3u * (uint32_t)bufVboNumTris;

    // Render-unification effort (kanban #131), Phase 3: route through the unified shader/vertex
    // format when the bit is set. Bounded to the common case for this first cut — mask/blend
    // texture combiner modes (rare) still fall back to the old per-permutation path, same
    // never-silently-drop-a-draw discipline as Phase 2's CMB wiring.
    bool unified = (gUnifiedRenderer & 2) != 0 && cc.numInputs <= 4 && !cc.used_masks[0] && !cc.used_masks[1] &&
                   !cc.used_blend[0] && !cc.used_blend[1];

    uint32_t vboBytes, aligned;
    Op op{};
    if (unified) {
        vboBytes = numVerts * (uint32_t)sizeof(UnifiedVtx);
        aligned = (mVtxUsed + 0xF) & ~0xFu;
        if (mVtxMapped == nullptr || aligned + vboBytes > mVtxCapacity)
            return;
        float fogColor[3] = { 0, 0, 0 };
        UnifiedVtx* dst = reinterpret_cast<UnifiedVtx*>(mVtxMapped + aligned);
        for (uint32_t i = 0; i < numVerts; i++)
            dst[i] = PackN64VertexToUnified(bufVbo + (size_t)i * prg->numFloats, cc, fogColor);
        mVtxUsed = aligned + vboBytes;

        Fast::Unified::Variant variant = N64VariantFor(cc);
        op.pipeline = GetOrCreateUnifiedN64Pipeline((int)variant, stateBits);
        if (!op.pipeline)
            unified = false; // shader/pipeline create failed — fall back rather than drop the draw
        else {
            UnifiedMaterial um = Fast_PackCCFeaturesToUnifiedMaterial(cc);
            Zelda3DUnified::UnifiedDrawUbo uu{};
            // uMvp/uMv unused when alreadyTransformed (N64 always is here) except uMv for vNrmView,
            // which N64 content never lights from (lightingMode 0) — identity is fine either way.
            uu.common.uMvp[0] = uu.common.uMvp[5] = uu.common.uMvp[10] = uu.common.uMvp[15] = 1.0f;
            uu.common.uMv[0] = uu.common.uMv[5] = uu.common.uMv[10] = uu.common.uMv[15] = 1.0f;
            memcpy(uu.common.uCombA, um.combMux, sizeof(uu.common.uCombA));
            uu.common.uFogColor[0] = fogColor[0];
            uu.common.uFogColor[1] = fogColor[1];
            uu.common.uFogColor[2] = fogColor[2];
            uu.common.uFogColor[3] = 0.0f;
            uu.common.uParams0[0] = um.alphaRef;
            uu.common.uParams0[1] = 0.0f; // lightingMode 0 — N64 color0 is already the final shade
            uu.common.uParams0[2] = (float)um.cycleCount;
            static const char* freezeStrN64 = getenv("ZELDA3D_FREEZE_NOISE_FRAME");
            uu.common.uParams0[3] = freezeStrN64 != nullptr ? (float)atoi(freezeStrN64) : (float)mFrameCount;
            uu.common.uParams1[0] = mCurrentNoiseScale;
            uu.common.uParams1[1] = 0.0f; // polygonOffset — N64 decal bias is baked into the pipeline
            uu.common.uParams1[2] = 0.0f; // hasSkin — N64 never GPU-skins
            uu.common.uParams1[3] = 1.0f; // alreadyTransformed
            mSoh3dModelUbos.push_back({});
            memcpy(mSoh3dModelUbos.back().data(), &uu, sizeof(uu));
            op.zelda3dDrawIdx = (int)mSoh3dModelUbos.size() - 1;
            op.drawClass = Op::DRAW_MODEL; // reuses the existing common+bones push path unchanged
            op.altVbo = nullptr;           // fall back to the shared mVbo at vboOffset, like N64 today
        }
    }
    if (!unified) {
        vboBytes = (uint32_t)(bufVboLen * sizeof(float));
        aligned = (mVtxUsed + 0xF) & ~0xFu;
        if (mVtxMapped == nullptr || aligned + vboBytes > mVtxCapacity)
            return; // ring exhausted this frame: drop rather than corrupt
        memcpy(mVtxMapped + aligned, bufVbo, vboBytes);
        mVtxUsed = aligned + vboBytes;
        op.pipeline = GetOrCreatePipeline(prg, stateBits);
    }

    op.kind = OP_DRAW;
    op.fb = mCurrentFb;
    op.vboOffset = aligned;
    op.numVerts = numVerts;

    // SDL3 GPU has a hybrid convention: NDC is Y-up (GL-like, so vertices aren't negated — see
    // GetClipParameters), but the viewport/scissor origin is TOP-left (Vulkan/D3D-like). The
    // interpreter, driven by GetClipParameters().invertY == FB's flag, hands us viewport/scissor in
    // GL BOTTOM-left pixel space. Convert the Y origin to top-left here, where the target FB height
    // is known: y_topleft = fbHeight - y_bottomleft - height.
    SDL_GPUViewport vp = mCurrentViewport;
    vp.y = (float)f.height - vp.y - vp.h;
    op.viewport = vp;

    // Clamp the scissor to the target FB (SDL3 GPU validates scissor against the render target).
    SDL_Rect sc = mCurrentScissor;
    sc.y = (int)f.height - sc.y - sc.h;
    // Clamp fully into [0, fb] on both axes (the Y flip can push the box partly off either edge).
    if (sc.x < 0) {
        sc.w += sc.x;
        sc.x = 0;
    }
    if (sc.y < 0) {
        sc.h += sc.y;
        sc.y = 0;
    }
    if (sc.x > (int)f.width)
        sc.x = f.width;
    if (sc.y > (int)f.height)
        sc.y = f.height;
    if (sc.x + sc.w > (int)f.width)
        sc.w = std::max(0, (int)f.width - sc.x);
    if (sc.y + sc.h > (int)f.height)
        sc.h = std::max(0, (int)f.height - sc.y);
    if (sc.w < 0)
        sc.w = 0;
    if (sc.h < 0)
        sc.h = 0;
    op.scissor = sc;

    SgUboData ubo{};
    // Test harnesses (tools/render_unify_corpus_sweep.py, tools/unified_ab_sweep.py) pin this so
    // the frame_count-seeded alpha-dither noise (RAND_NOISE above) is bit-identical between two
    // captures being pixel-diffed; without it, dither alone fails any raw LSB comparison.
    static const char* freezeStr = getenv("ZELDA3D_FREEZE_NOISE_FRAME");
    ubo.frame_count = freezeStr != nullptr ? atoi(freezeStr) : (int32_t)mFrameCount;
    ubo.noise_scale = mCurrentNoiseScale;
    ubo.prim_depth = mCurrentPrimDepth;
    for (int t = 0; t < 2; t++) {
        uint32_t id = mCurrentTextureIds[t];
        if (id < mTextures.size() && mTextures[id].uploaded) {
            ubo.texture_width[t] = (int32_t)mTextures[id].width;
            ubo.texture_height[t] = (int32_t)mTextures[id].height;
            ubo.texture_filtering[t] = (int32_t)mTextures[id].filtering;
        }
    }
    memcpy(op.ubo, &ubo, sizeof(ubo));

    // Build the fragment-sampler bindings in usedSlot order, matching the shader's contiguous set-2
    // binding numbers. Unused-but-declared slots get the dummy texture/sampler.
    uint32_t n = 0;
    for (int s = 0; s < 6; s++) {
        if (!prg->usedSlot[s])
            continue;
        SDL_GPUTexture* tex = DummyTexture();
        SDL_GPUSampler* samp = DummySampler();
        uint32_t id = mCurrentTextureIds[s];
        if (id < mTextures.size() && mTextures[id].uploaded && mTextures[id].tex) {
            tex = mTextures[id].tex;
            samp = GetOrCreateSampler(mTextures[id].linearFilter, mTextures[id].cms, mTextures[id].cmt);
        }
        op.samplers[n].texture = tex;
        op.samplers[n].sampler = samp;
        n++;
    }
    op.numSamplers = n;

    mOps.push_back(op);
}

// ---------------------------------------------------------------------------
// Framebuffers
// ---------------------------------------------------------------------------

void GfxRenderingAPISdl3Gpu::CreateFbResources(FramebufferSDL3& fb, uint32_t width, uint32_t height, bool hasDepth) {
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    fb.width = width;
    fb.height = height;

    SDL_GPUTextureCreateInfo cci{};
    cci.type = SDL_GPU_TEXTURETYPE_2D;
    cci.format = mColorFormat;
    cci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    cci.width = width;
    cci.height = height;
    cci.layer_count_or_depth = 1;
    cci.num_levels = 1;
    fb.color = SDL_CreateGPUTexture(mDevice, &cci);

    // Always allocate depth for drawable framebuffers: every game pipeline declares a depth-stencil
    // target, so the render pass must bind one. (Cheap; the interpreter's pure-color effect FBs get
    // an unused depth texture, which is harmless.)
    fb.hasDepth = true;
    SDL_GPUTextureCreateInfo dci{};
    dci.type = SDL_GPU_TEXTURETYPE_2D;
    dci.format = mDepthFormat;
    dci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    dci.width = width;
    dci.height = height;
    dci.layer_count_or_depth = 1;
    dci.num_levels = 1;
    fb.depth = SDL_CreateGPUTexture(mDevice, &dci);

    // Alias this FB's color into the texture table so combiner draws can sample it (SelectTextureFb)
    // and ImGui can reference it (GetFramebufferTextureId). Does not own the texture.
    if (fb.colorTexId == 0) {
        uint32_t id = mNextTextureId++;
        if (id >= mTextures.size())
            mTextures.resize(id + 1);
        fb.colorTexId = id;
    }
    TextureSDL3& t = mTextures[fb.colorTexId];
    t = TextureSDL3{};
    t.isFbAlias = true;
    t.tex = fb.color;
    t.width = width;
    t.height = height;
    t.uploaded = true;
    t.linearFilter = true;
    t.filtering = FILTER_LINEAR;
    t.cms = G_TX_NOMIRROR | G_TX_CLAMP;
    t.cmt = G_TX_NOMIRROR | G_TX_CLAMP;
}

void GfxRenderingAPISdl3Gpu::DestroyFbResources(FramebufferSDL3& fb) {
    if (NoteGpuRelease(fb.color, "framebuffer color")) {
        SDL_ReleaseGPUTexture(mDevice, fb.color);
    }
    fb.color = nullptr;
    if (NoteGpuRelease(fb.depth, "framebuffer depth")) {
        SDL_ReleaseGPUTexture(mDevice, fb.depth);
    }
    fb.depth = nullptr;
    if (fb.colorTexId != 0 && fb.colorTexId < mTextures.size()) {
        mTextures[fb.colorTexId].tex = nullptr;
        mTextures[fb.colorTexId].uploaded = false;
    }
}

int GfxRenderingAPISdl3Gpu::CreateFramebuffer() {
    int id = (int)mFramebuffers.size();
    mFramebuffers.emplace_back();
    return id;
}

void GfxRenderingAPISdl3Gpu::UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel,
                                                         bool openglInvertY, bool renderTarget, bool hasDepthBuffer,
                                                         bool canExtractDepth) {
    (void)msaaLevel;
    (void)canExtractDepth;
    if (fbId < 0)
        return;
    if (fbId >= (int)mFramebuffers.size())
        mFramebuffers.resize(fbId + 1);
    FramebufferSDL3& fb = mFramebuffers[fbId];
    width = std::max(width, 1u);
    height = std::max(height, 1u);

    fb.invertY = openglInvertY;
    fb.renderTarget = renderTarget || fbId == 0;

    const bool changed = fb.color == nullptr || fb.width != width || fb.height != height;
    if (changed) {
        SDL_WaitForGPUIdle(mDevice);
        uint32_t keepTexId = fb.colorTexId;
        DestroyFbResources(fb);
        fb.colorTexId = keepTexId;
        CreateFbResources(fb, width, height, hasDepthBuffer);
    }
}

void GfxRenderingAPISdl3Gpu::StartDrawToFramebuffer(int fbId, float noiseScale) {
    if (noiseScale != 0.0f)
        mCurrentNoiseScale = 1.0f / noiseScale;
    mCurrentFb = fbId;
}

void GfxRenderingAPISdl3Gpu::ClearFramebuffer(bool color, bool depth) {
    if (mCurrentFb < 0 || mCurrentFb >= (int)mFramebuffers.size())
        return;
    Op op{};
    op.kind = OP_CLEAR;
    op.fb = mCurrentFb;
    op.clearColor = color;
    op.clearDepth = depth;
    mOps.push_back(op);
}

void GfxRenderingAPISdl3Gpu::CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1,
                                             int dstX0, int dstY0, int dstX1, int dstY1) {
    if (fbDstId < 0 || fbSrcId < 0 || fbDstId >= (int)mFramebuffers.size() || fbSrcId >= (int)mFramebuffers.size())
        return;
    // Like the Vulkan backend: no Y compensation — every FB color image is stored top-down upright,
    // so a straight image-space blit is correct (the GL backend's Y-flip must NOT be replicated).
    Op op{};
    op.kind = OP_COPY;
    op.fb = fbDstId;
    op.srcFb = fbSrcId;
    op.srcRect = { srcX0, srcY0, srcX1 - srcX0, srcY1 - srcY0 };
    op.dstRect = { dstX0, dstY0, dstX1 - dstX0, dstY1 - dstY0 };
    op.nearest = false;
    mOps.push_back(op);
}

void GfxRenderingAPISdl3Gpu::ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) {
    if (fbIdTarger < 0 || fbIdSrc < 0 || fbIdTarger >= (int)mFramebuffers.size() ||
        fbIdSrc >= (int)mFramebuffers.size())
        return;
    FramebufferSDL3& dst = mFramebuffers[fbIdTarger];
    FramebufferSDL3& src = mFramebuffers[fbIdSrc];
    if (!src.color || !dst.color)
        return;
    // Single-sample (no true MSAA yet): full-image nearest blit, matching the Vulkan backend.
    Op op{};
    op.kind = OP_COPY;
    op.fb = fbIdTarger;
    op.srcFb = fbIdSrc;
    op.srcRect = { 0, 0, (int)src.width, (int)src.height };
    op.dstRect = { 0, 0, (int)dst.width, (int)dst.height };
    op.nearest = true;
    mOps.push_back(op);
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPISdl3Gpu::GetPixelDepth(int fbId, const std::set<std::pair<float, float>>& coordinates) {
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> res;
    if (fbId < 0 || fbId >= (int)mFramebuffers.size() || coordinates.empty()) {
        for (const auto& c : coordinates)
            res.emplace(c, 0);
        return res;
    }
    FramebufferSDL3& fb = mFramebuffers[fbId];
    if (!fb.depth) {
        for (const auto& c : coordinates)
            res.emplace(c, 0);
        return res;
    }

    // Make all prior depth writes for this frame visible, then copy the requested depth texels
    // (D32_FLOAT) back to the host.
    FlushAndWait();

    const uint32_t size = (uint32_t)(coordinates.size() * sizeof(float));
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);

    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    uint32_t off = 0;
    for (const auto& coord : coordinates) {
        int x = (int)coord.first;
        int y = (int)coord.second;
        if (fb.invertY)
            y = (int)fb.height - y;
        x = std::clamp(x, 0, (int)fb.width - 1);
        y = std::clamp(y, 0, (int)fb.height - 1);
        SDL_GPUTextureRegion reg{};
        reg.texture = fb.depth;
        reg.x = (uint32_t)x;
        reg.y = (uint32_t)y;
        reg.w = 1;
        reg.h = 1;
        reg.d = 1;
        SDL_GPUTextureTransferInfo ti{};
        ti.transfer_buffer = tb;
        ti.offset = off;
        ti.pixels_per_row = 1;
        ti.rows_per_layer = 1;
        SDL_DownloadFromGPUTexture(cp, &reg, &ti);
        off += sizeof(float);
    }
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(c);
    if (fence) {
        SDL_WaitForGPUFences(mDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(mDevice, fence);
    }

    const float* depths = (const float*)SDL_MapGPUTransferBuffer(mDevice, tb, false);
    size_t i = 0;
    for (const auto& coord : coordinates) {
        // Match the GL/Vulkan backends' depth -> N64 16-bit mapping: (d24 >> 10) << 2.
        uint32_t d24 = depths ? (uint32_t)(std::clamp(depths[i], 0.0f, 1.0f) * 16777215.0f) : 0;
        res.emplace(coord, (uint16_t)((d24 >> 10) << 2));
        i++;
    }
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
    return res;
}

void* GfxRenderingAPISdl3Gpu::GetFramebufferTextureId(int fbId) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return nullptr;
    return (void*)(uintptr_t)mFramebuffers[fbId].colorTexId;
}

void GfxRenderingAPISdl3Gpu::SelectTextureFb(int fbId) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return;
    FramebufferSDL3& fb = mFramebuffers[fbId];
    if (!fb.color)
        return;
    // SDL3 GPU auto-synchronizes sampling an FB color texture after it was a render target, so no
    // explicit barrier is needed — just point the tex-0 alias at it.
    if (fb.colorTexId != 0 && fb.colorTexId < mTextures.size()) {
        mTextures[fb.colorTexId].tex = fb.color;
        mTextures[fb.colorTexId].uploaded = (fb.color != nullptr);
    }
    SelectTexture(0, fb.colorTexId);
}

void GfxRenderingAPISdl3Gpu::ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return;
    FramebufferSDL3& fb = mFramebuffers[fbId];
    if (!fb.color)
        return;
    width = std::min(width, fb.width);
    height = std::min(height, fb.height);

    FlushAndWait();

    const uint32_t size = fb.width * fb.height * 4;
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(mDevice, &tci);

    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(mDevice);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureRegion reg{};
    reg.texture = fb.color;
    reg.w = fb.width;
    reg.h = fb.height;
    reg.d = 1;
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.pixels_per_row = fb.width;
    ti.rows_per_layer = fb.height;
    SDL_DownloadFromGPUTexture(cp, &reg, &ti);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(c);
    if (fence) {
        SDL_WaitForGPUFences(mDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(mDevice, fence);
    }

    const uint8_t* px = (const uint8_t*)SDL_MapGPUTransferBuffer(mDevice, tb, false);
    if (px) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t* p = &px[((size_t)y * fb.width + x) * 4]; // R8G8B8A8
                uint8_t r = (p[0] >> 3) & 0x1F;
                uint8_t g = (p[1] >> 3) & 0x1F;
                uint8_t b = (p[2] >> 3) & 0x1F;
                uint8_t a = p[3] ? 1 : 0;
                rgba16Buf[(size_t)y * width + x] = (r << 11) | (g << 6) | (b << 1) | a;
            }
        }
    }
    SDL_UnmapGPUTransferBuffer(mDevice, tb);
    SDL_ReleaseGPUTransferBuffer(mDevice, tb);
}

void GfxRenderingAPISdl3Gpu::SetTextureFilter(FilteringMode mode) {
    mCurrentFilterMode = mode;
}
FilteringMode GfxRenderingAPISdl3Gpu::GetTextureFilter() {
    return mCurrentFilterMode;
}
void GfxRenderingAPISdl3Gpu::SetSrgbMode() {
    mSrgbMode = true;
}
void* GfxRenderingAPISdl3Gpu::GetTextureById(int id) {
    return reinterpret_cast<void*>((uintptr_t)id);
}
void GfxRenderingAPISdl3Gpu::SetCurrentPrimDepth(float depth) {
    mCurrentPrimDepth = depth;
}

// ---------------------------------------------------------------------------
// Zelda3D unified-op hooks (P3): the OoT3D model / HUD / RmlUi content appends its draws as ops into
// the SAME deferred op-list as the N64 triangles, replayed in ONE render pass in FinishRender. The
// legacy BeginZelda3DPass live-command-buffer handshake is gone (the methods below are the only path).
// ---------------------------------------------------------------------------

void GfxRenderingAPISdl3Gpu::GetZelda3DViewportScissor(SDL_GPUViewport& vp, SDL_Rect& sc) {
    int fbId = (mCurrentFb >= 0 && mCurrentFb < (int)mFramebuffers.size()) ? mCurrentFb : 0;
    FramebufferSDL3& f = mFramebuffers[fbId];
    // Same GL-bottom-left -> SDL3-GPU-top-left conversion DrawTriangles applies.
    vp = mCurrentViewport;
    vp.y = (float)f.height - vp.y - vp.h;
    sc = mCurrentScissor;
    sc.y = (int)f.height - sc.y - sc.h;
    if (sc.x < 0) {
        sc.w += sc.x;
        sc.x = 0;
    }
    if (sc.y < 0) {
        sc.h += sc.y;
        sc.y = 0;
    }
    if (sc.x > (int)f.width)
        sc.x = (int)f.width;
    if (sc.y > (int)f.height)
        sc.y = (int)f.height;
    if (sc.x + sc.w > (int)f.width)
        sc.w = std::max(0, (int)f.width - sc.x);
    if (sc.y + sc.h > (int)f.height)
        sc.h = std::max(0, (int)f.height - sc.y);
    if (sc.w < 0)
        sc.w = 0;
    if (sc.h < 0)
        sc.h = 0;
}

void GfxRenderingAPISdl3Gpu::AppendZelda3DModelDraw(SDL_GPUGraphicsPipeline* pipeline, SDL_GPUBuffer* vbo,
                                                    uint32_t first, uint32_t count, const void* ubo,
                                                    SDL_GPUTexture* tex, SDL_GPUSampler* samp, SDL_GPUTexture* tex2,
                                                    SDL_GPUSampler* samp2, SDL_GPUTexture* tex1, SDL_GPUSampler* samp1,
                                                    const SDL_GPUViewport& vp, const SDL_Rect& sc, bool hasBlendConst,
                                                    SDL_FColor blendConst) {
    int idx = (int)mSoh3dModelUbos.size();
    mSoh3dModelUbos.emplace_back();
    memcpy(mSoh3dModelUbos.back().data(), ubo, sizeof(Zelda3DSg::SgUbo));

    Op op{};
    op.kind = OP_DRAW;
    op.drawClass = Op::DRAW_MODEL;
    op.fb = mCurrentFb;
    op.pipeline = pipeline;
    op.altVbo = vbo;
    op.firstVertex = first;
    op.numVerts = count;
    op.viewport = vp;
    op.scissor = sc;
    // slot 0 = model texture, slot 1 = third texture unit (generic TEV, ex-shadow slot),
    // slot 2 = second texture unit (dual-tex mask / generic TEV texture1)
    op.numSamplers = 3;
    op.samplers[0].texture = tex;
    op.samplers[0].sampler = samp;
    op.samplers[1].texture = tex2;
    op.samplers[1].sampler = samp2;
    // Slot 2 must always be backed (the pipeline layout declares 3 fragment samplers): fall back
    // to the dummies when the draw has no second texture — the shader only samples it when the
    // UBO's dual-tex flag (uSheen.y) is set.
    op.samplers[2].texture = tex1 ? tex1 : DummyTexture();
    op.samplers[2].sampler = samp1 ? samp1 : DummySampler();
    op.zelda3dDrawIdx = idx;
    op.useBlendConstants = hasBlendConst;
    op.blendConstants = blendConst;
    mOps.push_back(std::move(op));
}

void GfxRenderingAPISdl3Gpu::AppendZelda3DHudDraw(SDL_GPUGraphicsPipeline* pipeline, SDL_GPUBuffer* vbo, uint32_t first,
                                                  uint32_t count, SDL_GPUTexture* tex, SDL_GPUSampler* samp, float w,
                                                  float h) {
    Op op{};
    op.kind = OP_DRAW;
    op.drawClass = Op::DRAW_HUD;
    op.fb = 0; // HUD composites into fb 0 (the present blit / headless readback target)
    op.pipeline = pipeline;
    op.altVbo = vbo;
    op.firstVertex = first;
    op.numVerts = count;
    op.viewport = SDL_GPUViewport{ 0.0f, 0.0f, w, h, 0.0f, 1.0f };
    op.scissor = SDL_Rect{ 0, 0, (int)w, (int)h };
    op.numSamplers = 1;
    op.samplers[0].texture = tex;
    op.samplers[0].sampler = samp;
    // The vertex shader's UBO is { vec2 uViewport; vec2 pad; } — build it into op.ubo (vertex slot 0).
    float vubo[4] = { w, h, 0.0f, 0.0f };
    memcpy(op.ubo, vubo, sizeof(vubo));
    mOps.push_back(std::move(op));
}

void GfxRenderingAPISdl3Gpu::AppendZelda3DFullscreen(SDL_GPUGraphicsPipeline* pipeline, const void* ubo,
                                                     uint32_t uboLen, SDL_GPUTexture* tex, SDL_GPUSampler* samp,
                                                     const SDL_GPUViewport& vp, const SDL_Rect& sc) {
    Op op{};
    op.kind = OP_DRAW;
    op.drawClass = Op::DRAW_FULLSCREEN;
    op.fb = mCurrentFb;
    op.pipeline = pipeline;
    op.numVerts = 3; // fullscreen triangle from gl_VertexIndex; no vertex buffer
    op.viewport = vp;
    op.scissor = sc;
    op.numSamplers = 1;
    op.samplers[0].texture = tex;
    op.samplers[0].sampler = samp;
    if (uboLen > sizeof(op.ubo))
        uboLen = sizeof(op.ubo);
    op.uboLen = (uint16_t)uboLen;
    memcpy(op.ubo, ubo, uboLen);
    mOps.push_back(std::move(op));
}

void GfxRenderingAPISdl3Gpu::MainFbSize(int& w, int& h) {
    if (!mFramebuffers.empty() && mFramebuffers[0].width > 0) {
        w = (int)mFramebuffers[0].width;
        h = (int)mFramebuffers[0].height;
    } else {
        w = h = 0;
    }
}

void GfxRenderingAPISdl3Gpu::AppendZelda3DOwnPass(std::function<void(SDL_GPUCommandBuffer*)> fn) {
    Op op{};
    op.kind = OP_EXT_OWN_PASS;
    op.fb = mCurrentFb;
    op.extOwn = std::move(fn);
    mOps.push_back(std::move(op));
}

} // namespace Fast

#endif // ENABLE_SDL3GPU
