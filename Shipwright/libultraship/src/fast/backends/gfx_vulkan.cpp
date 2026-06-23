#include "ship/window/Window.h"
#ifdef ENABLE_VULKAN

// ============================================================================
// Vulkan rendering backend for Fast3D (soh3d).
//
// Milestone 1 scope: stand up the full Vulkan context (instance / device /
// queues / swapchain / render pass / command + sync objects), integrate it into
// the engine frame loop, and CLEAR the screen each frame. All draw/texture/
// framebuffer entry points are intentionally inert here — they arrive in later
// milestones (M2 pipelines+textures, M3 framebuffers, M5 soh3d pass). The point
// of M1 is to prove the window/surface/present plumbing works on Linux (RADV)
// before any rendering logic is ported.
//
// Frame-loop mapping (see Interpreter::EndFrame):
//   mRapi->StartFrame()    -> acquire swapchain image, begin cmd buffer + render
//                             pass (idempotent: the interpreter calls StartFrame
//                             twice per displayed frame).
//   mRapi->EndFrame()      -> end render pass + command buffer.
//   mWapi->SwapBuffersBegin-> (window) framerate sync only for Vulkan.
//   mRapi->FinishRender()  -> submit + present, then reset frame state.
// ============================================================================

#include "fast/backends/gfx_vulkan.h"
#include "fast/backends/gfx_sdl.h"
#include "fast/interpreter.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include <prism/processor.h>

#include <SDL2/SDL_vulkan.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <unistd.h> // access() for locating the MoltenVK ICD
#endif
#include <spdlog/spdlog.h>

namespace {
// Runtime GLSL -> SPIR-V via glslang. glslang's process must be initialized once
// per process. Returns true on success; on failure fills outLog and returns false.
bool gVkGlslangInited = false;
std::once_flag gVkGlslangOnce;

bool CompileGlslToSpirv(EShLanguage stage, const std::string& src, std::vector<uint32_t>& outSpirv,
                        std::string& outLog) {
    std::call_once(gVkGlslangOnce, []() {
        glslang::InitializeProcess();
        gVkGlslangInited = true;
    });

    glslang::TShader shader(stage);
    const char* str = src.c_str();
    shader.setStrings(&str, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

    const TBuiltInResource* resources = GetDefaultResources();
    const int defaultVersion = 450;
    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

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

// SoH3D frame-dump globals (defined in gfx_sdl2.cpp). The REPL sets these to
// capture the current frame on demand; we honor them from the Vulkan present path
// since glReadPixels is unavailable here.
extern "C" {
extern char gSoh3dDumpPath[1024];
extern volatile int gSoh3dDumpPending;
}

// ============================================================================
// Combiner -> GLSL helpers.
//
// These mirror the per-color-combiner formula generation in gfx_opengl.cpp
// (shader_item_to_str / append_formula). The combiner string output is identical;
// only the surrounding Vulkan template (explicit locations + a UBO for the loose
// uniforms) differs. The bare identifiers these strings emit (frame_count,
// noise_scale) are routed to the UBO via #defines in the Vulkan template, so the
// formula logic is reused verbatim. Kept local to this TU to avoid disturbing the
// GL backend; a shared extraction is a later cleanup.
// ============================================================================
namespace {
using namespace Fast;

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

const char* vk_shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
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
                return first_cycle ? "texVal0.a" : "texVal1.a";
            case SHADER_TEXEL0A:
                return first_cycle ? "texVal0.a" : "texVal1.a";
            case SHADER_TEXEL1A:
                return first_cycle ? "texVal1.a" : "texVal0.a";
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

bool vk_get_bool(prism::ContextTypes* value) {
    if (std::holds_alternative<int>(*value)) {
        return std::get<int>(*value) == 1;
    }
    return false;
}

prism::ContextTypes* vk_append_formula(prism::ContextTypes* _, prism::ContextTypes* a_arg, prism::ContextTypes* a_single,
                                       prism::ContextTypes* a_mult, prism::ContextTypes* a_mix,
                                       prism::ContextTypes* a_with_alpha, prism::ContextTypes* a_only_alpha,
                                       prism::ContextTypes* a_alpha, prism::ContextTypes* a_first_cycle) {
    auto c = std::get<prism::MTDArray<int>>(*a_arg);
    bool do_single = vk_get_bool(a_single);
    bool do_multiply = vk_get_bool(a_mult);
    bool do_mix = vk_get_bool(a_mix);
    bool with_alpha = vk_get_bool(a_with_alpha);
    bool only_alpha = vk_get_bool(a_only_alpha);
    bool opt_alpha = vk_get_bool(a_alpha);
    bool first_cycle = vk_get_bool(a_first_cycle);
    std::string out = "";
    if (do_single) {
        out += vk_shader_item_to_str(c.at(only_alpha, 3), with_alpha, only_alpha, opt_alpha, first_cycle, false);
    } else if (do_multiply) {
        out += vk_shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += " * ";
        out += vk_shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
    } else if (do_mix) {
        out += "mix(";
        out += vk_shader_item_to_str(c.at(only_alpha, 1), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ", ";
        out += vk_shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ", ";
        out += vk_shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
        out += ")";
    } else {
        out += "(";
        out += vk_shader_item_to_str(c.at(only_alpha, 0), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += " - ";
        out += vk_shader_item_to_str(c.at(only_alpha, 1), with_alpha, only_alpha, opt_alpha, first_cycle, false);
        out += ") * ";
        out += vk_shader_item_to_str(c.at(only_alpha, 2), with_alpha, only_alpha, opt_alpha, first_cycle, true);
        out += " + ";
        out += vk_shader_item_to_str(c.at(only_alpha, 3), with_alpha, only_alpha, opt_alpha, first_cycle, false);
    }
    return new prism::ContextTypes{ out };
}

// Running location counters for the Vulkan template's explicit layout(location=)
// qualifiers. Reset from BuildVkShaderSource before each stage build: both stages
// emit varyings in identical order so the per-stage vary counter lines up VS-out
// with FS-in, and the C++ vertex-attribute layout mirrors the attr counter order.
int gVkAttrLoc = 0;
int gVkVaryLoc = 0;
prism::ContextTypes* vk_aloc(prism::ContextTypes*) {
    return new prism::ContextTypes{ gVkAttrLoc++ };
}
prism::ContextTypes* vk_vloc(prism::ContextTypes*) {
    return new prism::ContextTypes{ gVkVaryLoc++ };
}

std::optional<std::string> vk_include_noop(const std::string&) {
    return std::nullopt;
}

// Vulkan-valid (#version 450) variant of shaders/opengl/default.shader.glsl. The
// combiner body is identical; the structural differences are explicit attribute /
// varying locations, the loose uniforms gathered into a std140 UBO (binding 0) with
// #defines so the reused combiner strings resolve, and explicit sampler bindings.
const char* kVkShaderTemplate = R"PRISM(@prism(type='fragment', name='Fast3D Vulkan Shader', version='1.0.0')
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

    @if(o_textures[0]) layout(binding = 1) uniform sampler2D uTex0;
    @if(o_textures[1]) layout(binding = 2) uniform sampler2D uTex1;
    @if(o_masks[0]) layout(binding = 3) uniform sampler2D uTexMask0;
    @if(o_masks[1]) layout(binding = 4) uniform sampler2D uTexMask1;
    @if(o_blend[0]) layout(binding = 5) uniform sampler2D uTexBlend0;
    @if(o_blend[1]) layout(binding = 6) uniform sampler2D uTexBlend1;

    layout(binding = 0, std140) uniform UBO {
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
} // namespace

namespace Fast {

GfxRenderingAPIVulkan* g_activeVulkanApi = nullptr;

#define VK_CHECK(expr)                                                                                               \
    do {                                                                                                               \
        VkResult vk_check_res_ = (expr);                                                                               \
        if (vk_check_res_ != VK_SUCCESS) {                                                                             \
            SPDLOG_ERROR("Vulkan call failed ({}): {} = {}", __LINE__, #expr, (int)vk_check_res_);                     \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

GfxRenderingAPIVulkan::GfxRenderingAPIVulkan(GfxWindowBackendSDL2* windowBackend) : mWindowBackend(windowBackend) {
    const char* val = getenv("SOH3D_VK_VALIDATION");
    mEnableValidation = val != nullptr && val[0] == '1';
}

GfxRenderingAPIVulkan::~GfxRenderingAPIVulkan() {
    if (g_activeVulkanApi == this)
        g_activeVulkanApi = nullptr;
    if (mDevice != VK_NULL_HANDLE) {
        // If the game loop exited mid-frame (window closed between StartFrame and EndFrame/FinishRender),
        // mFrameAcquired is true but the acquired swapchain image was never presented. Destroying the
        // swapchain while an image is in the ACQUIRED state without a corresponding present leaves RADV's
        // Wayland WSI with a dangling wl_surface frame-callback, causing "double free or corruption (out)"
        // in wsi_wl_swapchain_destroy → wl_proxy_marshal_flags on compositor-initiated close (#91).
        //
        // Fix: drain the dangling frame by ending + presenting it before teardown. EndFrame closes any
        // open render pass, blits the main FB to the swapchain image (or just transitions it to PRESENT
        // if the FB is gone), and records the command buffer. FinishRender submits + presents, returning
        // the image to the WSI so RADV can retire its Wayland frame callbacks cleanly.
        if (mFrameAcquired) {
            SPDLOG_INFO("[Vulkan] ~GfxRenderingAPIVulkan: mid-frame teardown (mFrameAcquired=true); "
                        "draining acquired swapchain image before destroy (#91)");
            EndFrame();
            FinishRender();
        }
        vkDeviceWaitIdle(mDevice);
        for (auto& fb : mFramebuffers)
            DestroyFbResources(fb);
        mFramebuffers.clear();
        if (mFbRenderPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(mDevice, mFbRenderPass, nullptr);
        DestroyRenderingResources();
        DestroyDepthResources();
        DestroyPerImageSync();
        DestroySwapchain();
        for (int i = 0; i < kMaxFramesInFlight; i++) {
            if (mImageAvailableSemaphores.size() > (size_t)i)
                vkDestroySemaphore(mDevice, mImageAvailableSemaphores[i], nullptr);
            if (mInFlightFences.size() > (size_t)i)
                vkDestroyFence(mDevice, mInFlightFences[i], nullptr);
        }
        if (mCommandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
        if (mRenderPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
        vkDestroyDevice(mDevice, nullptr);
    }
    if (mSurface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    if (mInstance != VK_NULL_HANDLE)
        vkDestroyInstance(mInstance, nullptr);
}

const char* GfxRenderingAPIVulkan::GetName() {
    return "Vulkan";
}

// ---------------------------------------------------------------------------
// Context creation
// ---------------------------------------------------------------------------

void GfxRenderingAPIVulkan::CreateInstance() {
#if defined(__APPLE__)
    // MoltenVK is loaded via an ICD manifest (MoltenVK_icd.json). If the Vulkan loader has no ICD
    // configured — e.g. the Vulkan SDK env wasn't sourced — it enumerates ZERO devices and the
    // screen stays black (you'll see "vkDeviceWaitIdle: Invalid device" from the null device).
    // Best-effort: point the loader at a MoltenVK ICD we can find, so it works without the user
    // setting VK_ICD_FILENAMES by hand.
    if (getenv("VK_ICD_FILENAMES") == nullptr && getenv("VK_DRIVER_FILES") == nullptr) {
        std::vector<std::string> candidates;
        if (const char* sdk = getenv("VULKAN_SDK")) {
            candidates.push_back(std::string(sdk) + "/share/vulkan/icd.d/MoltenVK_icd.json");
        }
        // Homebrew installs the ICD under its config dir: $(brew --prefix)/etc/vulkan/icd.d/.
        candidates.push_back("/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"); // brew, Apple Silicon
        candidates.push_back("/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json");    // brew, Intel
        candidates.push_back("/opt/homebrew/opt/molten-vk/share/vulkan/icd.d/MoltenVK_icd.json");
        candidates.push_back("/usr/local/opt/molten-vk/share/vulkan/icd.d/MoltenVK_icd.json");
        candidates.push_back("/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json");
        candidates.push_back("/usr/local/share/vulkan/icd.d/MoltenVK_icd.json");
        bool found = false;
        for (const std::string& p : candidates) {
            if (access(p.c_str(), R_OK) == 0) {
                setenv("VK_ICD_FILENAMES", p.c_str(), 1);
                SPDLOG_INFO("[Vulkan] macOS: using MoltenVK ICD {}", p);
                found = true;
                break;
            }
        }
        if (!found) {
            std::string looked;
            for (const std::string& p : candidates) {
                looked += "\n    " + p;
            }
            SPDLOG_WARN("[Vulkan] macOS: no MoltenVK ICD found. Looked in:{}\n  Find yours with: find "
                        "$(brew --prefix) -name MoltenVK_icd.json, then export VK_ICD_FILENAMES=<that "
                        "path> before ./run.sh.",
                        looked);
        }
    }
#endif
    // SDL needs the window to report the instance extensions it requires.
    unsigned int extCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(mWindow, &extCount, nullptr)) {
        SPDLOG_ERROR("SDL_Vulkan_GetInstanceExtensions(count) failed: {}", SDL_GetError());
        abort();
    }
    std::vector<const char*> extensions(extCount);
    if (!SDL_Vulkan_GetInstanceExtensions(mWindow, &extCount, extensions.data())) {
        SPDLOG_ERROR("SDL_Vulkan_GetInstanceExtensions(list) failed: {}", SDL_GetError());
        abort();
    }
#if defined(__APPLE__)
    // MoltenVK is a "portability" (non-conformant) Vulkan implementation. Without enabling this
    // instance extension AND the matching create flag, vkEnumeratePhysicalDevices returns zero
    // devices on macOS. (Added before debug_utils so the validation-retry below can pop only the
    // debug_utils entry without dropping this.)
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
    // debug_utils goes LAST so the validation-retry path can drop it with a single pop_back().
    if (mEnableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    if (mEnableValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Ship of Harkinian (soh3d)";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Fast3D";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();
#if defined(__APPLE__)
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR; // see portability ext above
#endif

    VkResult res = vkCreateInstance(&ci, nullptr, &mInstance);
    if (res != VK_SUCCESS) {
        // Validation layer may be unavailable; retry without it rather than aborting.
        if (mEnableValidation) {
            SPDLOG_WARN("vkCreateInstance with validation failed ({}); retrying without", (int)res);
            mEnableValidation = false;
            ci.enabledLayerCount = 0;
            extensions.pop_back(); // drop debug_utils (it was pushed last); keep portability
            ci.enabledExtensionCount = (uint32_t)extensions.size();
            ci.ppEnabledExtensionNames = extensions.data();
            VK_CHECK(vkCreateInstance(&ci, nullptr, &mInstance));
        } else {
            SPDLOG_ERROR("vkCreateInstance failed: {}", (int)res);
            abort();
        }
    }
    SPDLOG_INFO("Vulkan instance created ({} extensions, validation={})", (unsigned)extensions.size(),
                mEnableValidation);
}

void GfxRenderingAPIVulkan::CreateSurface() {
    if (!SDL_Vulkan_CreateSurface(mWindow, mInstance, &mSurface)) {
        SPDLOG_ERROR("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        abort();
    }
}

void GfxRenderingAPIVulkan::PickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(mInstance, &count, nullptr);
    if (count == 0) {
        SPDLOG_ERROR("No Vulkan physical devices found");
        abort();
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(mInstance, &count, devices.data());

    auto findQueues = [&](VkPhysicalDevice dev, uint32_t& gfx, uint32_t& present) -> bool {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops.data());
        gfx = present = UINT32_MAX;
        for (uint32_t i = 0; i < qcount; i++) {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                if (gfx == UINT32_MAX)
                    gfx = i;
            }
            VkBool32 supportsPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, mSurface, &supportsPresent);
            if (supportsPresent && present == UINT32_MAX)
                present = i;
        }
        return gfx != UINT32_MAX && present != UINT32_MAX;
    };

    // Prefer a discrete GPU that satisfies our queue requirements.
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    uint32_t chosenGfx = UINT32_MAX, chosenPresent = UINT32_MAX;
    for (int passDiscrete = 1; passDiscrete >= 0 && chosen == VK_NULL_HANDLE; passDiscrete--) {
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            bool isDiscrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (passDiscrete && !isDiscrete)
                continue;
            uint32_t gfx, present;
            if (findQueues(dev, gfx, present)) {
                chosen = dev;
                chosenGfx = gfx;
                chosenPresent = present;
                break;
            }
        }
    }
    if (chosen == VK_NULL_HANDLE) {
        SPDLOG_ERROR("No suitable Vulkan device (graphics+present)");
        abort();
    }
    mPhysicalDevice = chosen;
    mGraphicsQueueFamily = chosenGfx;
    mPresentQueueFamily = chosenPresent;
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &mPhysicalDeviceProps);
    SPDLOG_INFO("Vulkan device: {} (gfx queue {}, present queue {})", mPhysicalDeviceProps.deviceName,
                mGraphicsQueueFamily, mPresentQueueFamily);
}

void GfxRenderingAPIVulkan::CreateLogicalDevice() {
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    std::vector<uint32_t> families = { mGraphicsQueueFamily };
    if (mPresentQueueFamily != mGraphicsQueueFamily)
        families.push_back(mPresentQueueFamily);
    for (uint32_t fam : families) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#if defined(__APPLE__)
    // The Vulkan spec REQUIRES enabling VK_KHR_portability_subset on any device that advertises it
    // (MoltenVK always does); vkCreateDevice fails otherwise. Use the string name to avoid pulling
    // in the beta headers (VK_ENABLE_BETA_EXTENSIONS) just for the macro.
    {
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> avail(n);
        vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &n, avail.data());
        for (const auto& e : avail) {
            if (strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) {
                deviceExtensions.push_back("VK_KHR_portability_subset");
                break;
            }
        }
    }
#endif

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_FALSE;
    features.depthClamp = VK_TRUE;     // mirrors the GL backend's glEnable(GL_DEPTH_CLAMP)
    features.fragmentStoresAndAtomics = VK_FALSE;

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = (uint32_t)queueInfos.size();
    ci.pQueueCreateInfos = queueInfos.data();
    ci.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    ci.ppEnabledExtensionNames = deviceExtensions.data();
    ci.pEnabledFeatures = &features;

    VK_CHECK(vkCreateDevice(mPhysicalDevice, &ci, nullptr, &mDevice));
    vkGetDeviceQueue(mDevice, mGraphicsQueueFamily, 0, &mGraphicsQueue);
    vkGetDeviceQueue(mDevice, mPresentQueueFamily, 0, &mPresentQueue);
}

void GfxRenderingAPIVulkan::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface, &caps);

    // Surface format: prefer B8G8R8A8_UNORM.
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &fmtCount, formats.data());
    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = f;
            break;
        }
    }
    mSwapchainFormat = chosenFormat.format;

    // Present mode: FIFO is always available.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    // Extent.
    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(mWindow, &w, &h);
        extent.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    mSwapchainExtent = extent;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // for the frame-dump readback
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // present blit: main FB color -> swapchain

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = mSurface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosenFormat.format;
    ci.imageColorSpace = chosenFormat.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = usage;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    uint32_t queueFamilyIndices[] = { mGraphicsQueueFamily, mPresentQueueFamily };
    if (mGraphicsQueueFamily != mPresentQueueFamily) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(mDevice, &ci, nullptr, &mSwapchain));

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actualCount, nullptr);
    mSwapchainImages.resize(actualCount);
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actualCount, mSwapchainImages.data());

    mSwapchainImageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; i++) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = mSwapchainImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = mSwapchainFormat;
        vi.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY };
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(mDevice, &vi, nullptr, &mSwapchainImageViews[i]));
    }
    SPDLOG_INFO("Vulkan swapchain: {}x{}, {} images, format {}", extent.width, extent.height, actualCount,
                (int)mSwapchainFormat);
}

void GfxRenderingAPIVulkan::CreateRenderPass() {
    VkAttachmentDescription color{};
    color.format = mSwapchainFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth attachment (M2): cleared each frame, used while rendering, discarded.
    VkAttachmentDescription depth{};
    depth.format = mDepthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { color, depth };
    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(mDevice, &ci, nullptr, &mRenderPass));
}

void GfxRenderingAPIVulkan::CreateFramebuffers() {
    mSwapchainFramebuffers.resize(mSwapchainImageViews.size());
    for (size_t i = 0; i < mSwapchainImageViews.size(); i++) {
        VkImageView attachments[] = { mSwapchainImageViews[i], mDepthView };
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = mRenderPass;
        fi.attachmentCount = 2;
        fi.pAttachments = attachments;
        fi.width = mSwapchainExtent.width;
        fi.height = mSwapchainExtent.height;
        fi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(mDevice, &fi, nullptr, &mSwapchainFramebuffers[i]));
    }
}

void GfxRenderingAPIVulkan::CreateDepthResources() {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = mDepthFormat;
    ii.extent = { mSwapchainExtent.width, mSwapchainExtent.height, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(mDevice, &ii, nullptr, &mDepthImage));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(mDevice, mDepthImage, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(mDevice, &ai, nullptr, &mDepthMemory));
    VK_CHECK(vkBindImageMemory(mDevice, mDepthImage, mDepthMemory, 0));

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = mDepthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = mDepthFormat;
    // Combined depth+stencil view so the framebuffer attachment covers the stencil aspect
    // (required for D32_SFLOAT_S8_UINT; game draws only use depth, stencil stays zeroed).
    vi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(mDevice, &vi, nullptr, &mDepthView));
}

void GfxRenderingAPIVulkan::DestroyDepthResources() {
    if (mDepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, mDepthView, nullptr);
        mDepthView = VK_NULL_HANDLE;
    }
    if (mDepthImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice, mDepthImage, nullptr);
        mDepthImage = VK_NULL_HANDLE;
    }
    if (mDepthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, mDepthMemory, nullptr);
        mDepthMemory = VK_NULL_HANDLE;
    }
}

void GfxRenderingAPIVulkan::CreateCommandResources() {
    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = mGraphicsQueueFamily;
    VK_CHECK(vkCreateCommandPool(mDevice, &pi, nullptr, &mCommandPool));

    mCommandBuffers.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = mCommandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kMaxFramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &ai, mCommandBuffers.data()));
}

void GfxRenderingAPIVulkan::CreateSyncObjects() {
    // Per-frame-in-flight: acquire semaphore + submission fence.
    mImageAvailableSemaphores.resize(kMaxFramesInFlight);
    mInFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        VK_CHECK(vkCreateSemaphore(mDevice, &si, nullptr, &mImageAvailableSemaphores[i]));
        VK_CHECK(vkCreateFence(mDevice, &fi, nullptr, &mInFlightFences[i]));
    }

    // Per-swapchain-image: render-finished semaphore (the present waits on this; a
    // binary semaphore can't be re-signaled while a present is still pending it, so
    // it must be owned per image, not per frame-in-flight) + an images-in-flight
    // fence map so we never render to an image whose previous frame is unfinished.
    CreatePerImageSync();
}

void GfxRenderingAPIVulkan::CreatePerImageSync() {
    const size_t n = mSwapchainImages.size();
    mRenderFinishedSemaphores.resize(n);
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (size_t i = 0; i < n; i++) {
        VK_CHECK(vkCreateSemaphore(mDevice, &si, nullptr, &mRenderFinishedSemaphores[i]));
    }
    mImagesInFlight.assign(n, VK_NULL_HANDLE);
}

void GfxRenderingAPIVulkan::DestroyPerImageSync() {
    for (VkSemaphore s : mRenderFinishedSemaphores)
        vkDestroySemaphore(mDevice, s, nullptr);
    mRenderFinishedSemaphores.clear();
    mImagesInFlight.clear();
}

void GfxRenderingAPIVulkan::DestroySwapchain() {
    for (VkFramebuffer fb : mSwapchainFramebuffers)
        vkDestroyFramebuffer(mDevice, fb, nullptr);
    mSwapchainFramebuffers.clear();
    for (VkImageView iv : mSwapchainImageViews)
        vkDestroyImageView(mDevice, iv, nullptr);
    mSwapchainImageViews.clear();
    if (mSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }
}

void GfxRenderingAPIVulkan::RecreateSwapchain() {
    vkDeviceWaitIdle(mDevice);
    DestroyPerImageSync();
    DestroySwapchain();
    DestroyDepthResources();
    CreateSwapchain();
    CreateDepthResources();
    CreateFramebuffers();
    CreatePerImageSync();
}

void GfxRenderingAPIVulkan::Init() {
    mWindow = mWindowBackend ? mWindowBackend->GetSdlWindow() : nullptr;
    if (mWindow == nullptr) {
        SPDLOG_ERROR("Vulkan backend: SDL window is null at Init()");
        abort();
    }
    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapchain();
    CreateDepthResources();
    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandResources();
    CreateSyncObjects();
    CreateFbRenderPass();
    CreateRenderingResources();
    // Slot 0 is the main framebuffer; the interpreter sizes it via
    // UpdateFramebufferParameters(0, ...) right after Init and every frame.
    mFramebuffers.resize(1);
    mFramebuffers[0].renderTarget = true;
    g_activeVulkanApi = this; // the SoH3D Vulkan pass records into this backend
    SPDLOG_INFO("Vulkan backend initialized (Milestone 3: offscreen framebuffers)");
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

void GfxRenderingAPIVulkan::StartFrame() {
    // Idempotent within a displayed frame: the interpreter calls StartFrame twice
    // (Interpreter::StartFrame and Interpreter::Run). Only the first acquires.
    if (mFrameAcquired) {
        return;
    }

    vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE, UINT64_MAX);

    VkResult acq = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX,
                                         mImageAvailableSemaphores[mCurrentFrame], VK_NULL_HANDLE, &mImageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return; // skip this frame
    } else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        SPDLOG_ERROR("vkAcquireNextImageKHR failed: {}", (int)acq);
        abort();
    }

    // If a previous frame-in-flight is still rendering to this image, wait for it.
    if (mImagesInFlight[mImageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(mDevice, 1, &mImagesInFlight[mImageIndex], VK_TRUE, UINT64_MAX);
    }
    mImagesInFlight[mImageIndex] = mInFlightFences[mCurrentFrame];

    vkResetFences(mDevice, 1, &mInFlightFences[mCurrentFrame]);

    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    // Reset the per-frame vertex/uniform rings and descriptor pool for this frame.
    BeginFrameRings();

    // No render pass is begun here. The interpreter binds a target with
    // StartDrawToFramebuffer and the pass for it opens lazily on the first draw/clear
    // (BeginPassIfNeeded), so a frame can target multiple framebuffers in sequence.
    mCurrentFb = 0;
    mPassOpen = false;

    // Default dynamic state covers the whole frame until the game sets a viewport.
    mCurrentViewport = { 0.0f, 0.0f, (float)mSwapchainExtent.width, (float)mSwapchainExtent.height, 0.0f, 1.0f };
    mCurrentScissor = { { 0, 0 }, mSwapchainExtent };

    mFrameCount++;
    mFrameAcquired = true;
}

void GfxRenderingAPIVulkan::EndFrame() {
    if (!mFrameAcquired) {
        return;
    }
    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];
    EndPassIfOpen();

    // Present: blit the main framebuffer's color image onto the acquired swapchain
    // image. (Compositing intermediate buffers onto fb 0 is done by the game's own
    // Fast3D draws / ResolveMSAAColorBuffer; the ImGui overlay is M4. When the game
    // renders to mGameFb under internal-resolution scaling, the final composite onto
    // fb 0 relies on the ImGui pass — until M4 that path shows fb 0's cleared content.)
    VkImage swap = mSwapchainImages[mImageIndex];
    FramebufferVulkan& main = mFramebuffers[0];

    auto swapBarrier = [&](VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA, VkAccessFlags dstA,
                           VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = swap;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    if (main.colorImage != VK_NULL_HANDLE) {
        TransitionImageLayout(cmd, main.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, main.colorLayout,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        swapBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { (int32_t)main.width, (int32_t)main.height, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { (int32_t)mSwapchainExtent.width, (int32_t)mSwapchainExtent.height, 1 };
        vkCmdBlitImage(cmd, main.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        swapBarrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    } else {
        // No main color image yet (e.g. before the first UpdateFramebufferParameters):
        // just put the swapchain image into a presentable layout.
        swapBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_ACCESS_MEMORY_READ_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }

    VK_CHECK(vkEndCommandBuffer(cmd));
}

void GfxRenderingAPIVulkan::FinishRender() {
    if (!mFrameAcquired) {
        return;
    }
    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSems[] = { mImageAvailableSemaphores[mCurrentFrame] };
    // The acquired swapchain image is only touched by the present blit in EndFrame, so
    // the acquire semaphore is waited at the transfer stage that performs that blit.
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitSems;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    // Signal the per-IMAGE render-finished semaphore (the present below waits on it).
    VkSemaphore signalSems[] = { mRenderFinishedSemaphores[mImageIndex] };
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signalSems;

    VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submit, mInFlightFences[mCurrentFrame]));

    // On-demand / scripted frame dump (verification). Reads the rendered swapchain
    // image after waiting for the submit to finish. Slow, only when triggered.
    MaybeDumpFrame();

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = signalSems;
    VkSwapchainKHR swapchains[] = { mSwapchain };
    present.swapchainCount = 1;
    present.pSwapchains = swapchains;
    present.pImageIndices = &mImageIndex;

    VkResult pres = vkQueuePresentKHR(mPresentQueue, &present);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchain();
    } else if (pres != VK_SUCCESS) {
        SPDLOG_ERROR("vkQueuePresentKHR failed: {}", (int)pres);
        abort();
    }

    mCurrentFrame = (mCurrentFrame + 1) % kMaxFramesInFlight;
    mFrameAcquired = false;
}

// ---------------------------------------------------------------------------
// Frame dump (PPM readback of the swapchain image)
// ---------------------------------------------------------------------------

void GfxRenderingAPIVulkan::MaybeDumpFrame() {
    const char* path = nullptr;

    // 1) Scripted single-frame dump: SOH_FRAMEDUMP=<path>, at SOH_FRAMEDUMP_FRAME.
    static const char* envDump = getenv("SOH_FRAMEDUMP");
    static long frame = 0;
    static long targetFrame = getenv("SOH_FRAMEDUMP_FRAME") ? atol(getenv("SOH_FRAMEDUMP_FRAME")) : 300;
    bool exitAfter = false;
    if (envDump != nullptr) {
        ++frame;
        if (frame == targetFrame) {
            path = envDump;
            exitAfter = true;
        }
    }

    // 2) REPL on-demand dump (does not exit). Takes priority if both fire.
    if (gSoh3dDumpPending) {
        path = gSoh3dDumpPath;
        exitAfter = false;
    }

    if (path == nullptr) {
        return;
    }

    // Ensure the render submit for this frame has completed before reading.
    vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE, UINT64_MAX);
    WriteSwapchainPpm(path);
    gSoh3dDumpPending = 0;
    if (exitAfter) {
        exit(0);
    }
}

void GfxRenderingAPIVulkan::WriteSwapchainPpm(const char* path) {
    const uint32_t w = mSwapchainExtent.width;
    const uint32_t h = mSwapchainExtent.height;
    const VkDeviceSize size = (VkDeviceSize)w * h * 4;

    // Host-visible staging buffer.
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(mDevice, &bci, nullptr, &buffer));

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(mDevice, buffer, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memProps);
    uint32_t memType = UINT32_MAX;
    VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want) {
            memType = i;
            break;
        }
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReq.size;
    mai.memoryTypeIndex = memType;
    VK_CHECK(vkAllocateMemory(mDevice, &mai, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(mDevice, buffer, memory, 0));

    // One-shot command buffer: PRESENT_SRC -> TRANSFER_SRC, copy, -> PRESENT_SRC.
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = mCommandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &cai, &cmd));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cbi));

    VkImage srcImage = mSwapchainImages[mImageIndex];
    auto barrier = [&](VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA, VkAccessFlags dstA,
                       VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = srcImage;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_READ_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { w, h, 1 };
    vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(mDevice, &fci, nullptr, &fence));
    VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submit, fence));
    vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(mDevice, fence, nullptr);
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmd);

    // Map and write PPM (top-down).
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(mDevice, memory, 0, size, 0, &mapped));
    const uint8_t* px = static_cast<const uint8_t*>(mapped);
    bool bgra = (mSwapchainFormat == VK_FORMAT_B8G8R8A8_UNORM || mSwapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t* p = &px[((size_t)y * w + x) * 4];
                uint8_t rgb[3];
                if (bgra) {
                    rgb[0] = p[2];
                    rgb[1] = p[1];
                    rgb[2] = p[0];
                } else {
                    rgb[0] = p[0];
                    rgb[1] = p[1];
                    rgb[2] = p[2];
                }
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
        SPDLOG_INFO("Vulkan frame dump written: {} ({}x{})", path, w, h);
    } else {
        SPDLOG_ERROR("Vulkan frame dump: could not open {}", path);
    }
    vkUnmapMemory(mDevice, memory);
    vkDestroyBuffer(mDevice, buffer, nullptr);
    vkFreeMemory(mDevice, memory, nullptr);
}

void GfxRenderingAPIVulkan::OnResize() {
    // Swapchain is recreated lazily on OUT_OF_DATE/SUBOPTIMAL during acquire/present.
}

// ---------------------------------------------------------------------------
// Memory / buffer / image helpers
// ---------------------------------------------------------------------------

uint32_t GfxRenderingAPIVulkan::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    SPDLOG_ERROR("Vulkan: no suitable memory type ({:#x})", typeBits);
    abort();
}

void GfxRenderingAPIVulkan::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                                         VkBuffer& buf, VkDeviceMemory& mem, void** mappedOut) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(mDevice, &bci, nullptr, &buf));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(mDevice, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(mDevice, &ai, nullptr, &mem));
    VK_CHECK(vkBindBufferMemory(mDevice, buf, mem, 0));
    if (mappedOut != nullptr) {
        VK_CHECK(vkMapMemory(mDevice, mem, 0, size, 0, mappedOut));
    }
}

void GfxRenderingAPIVulkan::CreateImageRGBA(uint32_t width, uint32_t height, VkImage& image, VkDeviceMemory& mem,
                                            VkImageView& view) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = { width, height, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(mDevice, &ii, nullptr, &image));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(mDevice, image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(mDevice, &ai, nullptr, &mem));
    VK_CHECK(vkBindImageMemory(mDevice, image, mem, 0));

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(mDevice, &vi, nullptr, &view));
}

void GfxRenderingAPIVulkan::UploadImageRGBA(VkImage image, uint32_t width, uint32_t height, const uint8_t* rgba) {
    const VkDeviceSize size = (VkDeviceSize)width * height * 4;
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* mapped = nullptr;
    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
                 &mapped);
    memcpy(mapped, rgba, size);
    vkUnmapMemory(mDevice, stagingMem);

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = mCommandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &cai, &cmd));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cbi));

    auto barrier = [&](VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA, VkAccessFlags dstA,
                       VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(mDevice, &fci, nullptr, &fence));
    VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submit, fence));
    vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(mDevice, fence, nullptr);
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmd);
    vkDestroyBuffer(mDevice, staging, nullptr);
    vkFreeMemory(mDevice, stagingMem, nullptr);
}

// Std140 UBO matching the Vulkan shader template's UBO block (48 bytes).
struct VkUboData {
    int32_t frame_count;
    float noise_scale;
    float prim_depth;
    int32_t _pad0;
    int32_t texture_width[2];
    int32_t texture_height[2];
    int32_t texture_filtering[2];
    int32_t _pad1[2];
};

// ---------------------------------------------------------------------------
// Rendering resources (descriptor layout, pipeline layout, per-frame rings, dummy)
// ---------------------------------------------------------------------------

void GfxRenderingAPIVulkan::CreateRenderingResources() {
    // One descriptor set layout used by every pipeline: binding 0 = UBO, bindings
    // 1..6 = the six combiner sampler slots (tex0,tex1,mask0,mask1,blend0,blend1).
    std::array<VkDescriptorSetLayoutBinding, 7> binds{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    for (int i = 1; i < 7; i++) {
        binds[i].binding = i;
        binds[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli{};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = (uint32_t)binds.size();
    dli.pBindings = binds.data();
    VK_CHECK(vkCreateDescriptorSetLayout(mDevice, &dli, nullptr, &mDescriptorSetLayout));

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &mDescriptorSetLayout;
    VK_CHECK(vkCreatePipelineLayout(mDevice, &pli, nullptr, &mPipelineLayout));

    // UBO sub-allocation stride (aligned).
    VkDeviceSize align = mPhysicalDeviceProps.limits.minUniformBufferOffsetAlignment;
    if (align == 0)
        align = 1;
    mUboAlignedSize = ((sizeof(VkUboData) + align - 1) / align) * align;

    const VkDeviceSize kVboCapacity = 32 * 1024 * 1024; // 32 MB vertex ring per frame
    const uint32_t kMaxDrawsPerFrame = 32768;
    const VkDeviceSize kUboCapacity = mUboAlignedSize * kMaxDrawsPerFrame;

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        FrameRing& fr = mFrameRings[i];
        CreateBuffer(kVboCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fr.vbo, fr.vboMem,
                     &fr.vboMapped);
        fr.vboCapacity = kVboCapacity;
        CreateBuffer(kUboCapacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fr.ubo, fr.uboMem,
                     &fr.uboMapped);
        fr.uboCapacity = kUboCapacity;

        std::array<VkDescriptorPoolSize, 2> ps{};
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps[0].descriptorCount = kMaxDrawsPerFrame;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[1].descriptorCount = kMaxDrawsPerFrame * 6;
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets = kMaxDrawsPerFrame;
        dpi.poolSizeCount = (uint32_t)ps.size();
        dpi.pPoolSizes = ps.data();
        VK_CHECK(vkCreateDescriptorPool(mDevice, &dpi, nullptr, &fr.descPool));
    }

    // 1x1 white dummy texture for unused sampler slots.
    CreateImageRGBA(1, 1, mDummyImage, mDummyMemory, mDummyView);
    const uint8_t white[4] = { 255, 255, 255, 255 };
    UploadImageRGBA(mDummyImage, 1, 1, white);
    mDummySampler = GetOrCreateSampler(false, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP);
}

void GfxRenderingAPIVulkan::DestroyRenderingResources() {
    for (auto& kv : mPipelineCache)
        vkDestroyPipeline(mDevice, kv.second, nullptr);
    mPipelineCache.clear();
    for (auto& kv : mShaderProgramPool) {
        if (kv.second.vert)
            vkDestroyShaderModule(mDevice, kv.second.vert, nullptr);
        if (kv.second.frag)
            vkDestroyShaderModule(mDevice, kv.second.frag, nullptr);
    }
    mShaderProgramPool.clear();
    for (auto& kv : mSamplerCache)
        vkDestroySampler(mDevice, kv.second, nullptr);
    mSamplerCache.clear();
    for (auto& t : mTextures) {
        if (t.isFbAlias)
            continue; // image/view owned by a FramebufferVulkan, freed in DestroyFbResources
        if (t.view)
            vkDestroyImageView(mDevice, t.view, nullptr);
        if (t.image)
            vkDestroyImage(mDevice, t.image, nullptr);
        if (t.memory)
            vkFreeMemory(mDevice, t.memory, nullptr);
    }
    mTextures.clear();
    if (mDummyView)
        vkDestroyImageView(mDevice, mDummyView, nullptr);
    if (mDummyImage)
        vkDestroyImage(mDevice, mDummyImage, nullptr);
    if (mDummyMemory)
        vkFreeMemory(mDevice, mDummyMemory, nullptr);
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        FrameRing& fr = mFrameRings[i];
        if (fr.descPool)
            vkDestroyDescriptorPool(mDevice, fr.descPool, nullptr);
        if (fr.vbo)
            vkDestroyBuffer(mDevice, fr.vbo, nullptr);
        if (fr.vboMem)
            vkFreeMemory(mDevice, fr.vboMem, nullptr);
        if (fr.ubo)
            vkDestroyBuffer(mDevice, fr.ubo, nullptr);
        if (fr.uboMem)
            vkFreeMemory(mDevice, fr.uboMem, nullptr);
    }
    if (mPipelineLayout)
        vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
    if (mDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);
}

void GfxRenderingAPIVulkan::BeginFrameRings() {
    FrameRing& fr = mFrameRings[mCurrentFrame];
    fr.vboOffset = 0;
    fr.uboOffset = 0;
    vkResetDescriptorPool(mDevice, fr.descPool, 0);
}

VkSampler GfxRenderingAPIVulkan::GetOrCreateSampler(bool linear, uint32_t cms, uint32_t cmt) {
    uint32_t key = (linear ? 1u : 0u) | (cms << 1) | (cmt << 4);
    auto it = mSamplerCache.find(key);
    if (it != mSamplerCache.end())
        return it->second;

    auto wrap = [](uint32_t cm) -> VkSamplerAddressMode {
        switch (cm) {
            case G_TX_NOMIRROR | G_TX_CLAMP:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case G_TX_MIRROR | G_TX_WRAP:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case G_TX_MIRROR | G_TX_CLAMP:
                return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
            case G_TX_NOMIRROR | G_TX_WRAP:
            default:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = wrap(cms);
    si.addressModeV = wrap(cmt);
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = 0.0f;
    si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(mDevice, &si, nullptr, &sampler));
    mSamplerCache[key] = sampler;
    return sampler;
}

// ---------------------------------------------------------------------------
// Shaders / pipelines
// ---------------------------------------------------------------------------

std::string GfxRenderingAPIVulkan::BuildVkShaderSource(const CCFeatures& cc, bool vertex, ShaderProgramVulkan* prg) {
    // Reset the location counters: attribute + varying for the vertex stage, only
    // the varying counter for the fragment stage (so VS-out matches FS-in).
    if (vertex) {
        gVkAttrLoc = 0;
        gVkVaryLoc = 0;
    } else {
        gVkVaryLoc = 0;
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
        { "append_formula", (InvokeFunc)vk_append_formula },
        { "aloc", (InvokeFunc)vk_aloc },
        { "vloc", (InvokeFunc)vk_vloc },
    };
    processor.populate(ctx);
    processor.load(kVkShaderTemplate);
    processor.bind_include_loader(vk_include_noop);
    return processor.process();
}

VkShaderModule GfxRenderingAPIVulkan::CreateShaderModule(const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode = spirv.data();
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(mDevice, &ci, nullptr, &mod));
    return mod;
}

void GfxRenderingAPIVulkan::ClearShaderCache() {
    vkDeviceWaitIdle(mDevice);
    for (auto& kv : mPipelineCache)
        vkDestroyPipeline(mDevice, kv.second, nullptr);
    mPipelineCache.clear();
    for (auto& kv : mShaderProgramPool) {
        if (kv.second.vert)
            vkDestroyShaderModule(mDevice, kv.second.vert, nullptr);
        if (kv.second.frag)
            vkDestroyShaderModule(mDevice, kv.second.frag, nullptr);
    }
    mShaderProgramPool.clear();
}

ShaderProgram* GfxRenderingAPIVulkan::CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) {
    CCFeatures cc{};
    gfx_cc_get_features(shaderId0, shaderId1, &cc);

    ShaderProgramVulkan& prg = mShaderProgramPool[std::make_pair(shaderId0, shaderId1)];
    prg.id0 = shaderId0;
    prg.id1 = shaderId1;
    prg.numInputs = cc.numInputs;
    prg.usedTextures[0] = cc.usedTextures[0];
    prg.usedTextures[1] = cc.usedTextures[1];
    prg.usedMasks[0] = cc.used_masks[0];
    prg.usedMasks[1] = cc.used_masks[1];
    prg.usedBlend[0] = cc.used_blend[0];
    prg.usedBlend[1] = cc.used_blend[1];
    prg.usedSlot[0] = cc.usedTextures[0];
    prg.usedSlot[1] = cc.usedTextures[1];
    prg.usedSlot[2] = cc.used_masks[0];
    prg.usedSlot[3] = cc.used_masks[1];
    prg.usedSlot[4] = cc.used_blend[0];
    prg.usedSlot[5] = cc.used_blend[1];

    // Build the vertex-attribute layout in the SAME order the Vulkan template emits
    // `layout(location=)` qualifiers, so location N == attribs[N].
    uint32_t floatOffset = 0;
    auto add = [&](uint32_t size) {
        prg.attribs.push_back({ size, floatOffset * (uint32_t)sizeof(float) });
        floatOffset += size;
    };
    add(4); // aVtxPos
    for (int i = 0; i < 2; i++) {
        if (cc.usedTextures[i]) {
            add(2); // aTexCoord
            for (int j = 0; j < 2; j++) {
                if (cc.clamp[i][j])
                    add(1); // aTexClampS / aTexClampT
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

    std::string vsSrc = BuildVkShaderSource(cc, true, &prg);
    std::string fsSrc = BuildVkShaderSource(cc, false, &prg);

    std::vector<uint32_t> vsSpv, fsSpv;
    std::string log;
    if (!CompileGlslToSpirv(EShLangVertex, vsSrc, vsSpv, log)) {
        SPDLOG_ERROR("Vulkan VS compile failed (shader {:#x}): {}\n--- source ---\n{}", cc.shader_id, log, vsSrc);
        abort();
    }
    if (!CompileGlslToSpirv(EShLangFragment, fsSrc, fsSpv, log)) {
        SPDLOG_ERROR("Vulkan FS compile failed (shader {:#x}): {}\n--- source ---\n{}", cc.shader_id, log, fsSrc);
        abort();
    }
    prg.vert = CreateShaderModule(vsSpv);
    prg.frag = CreateShaderModule(fsSpv);

    mCurrentShaderProgram = &prg;
    return reinterpret_cast<ShaderProgram*>(&prg);
}

ShaderProgram* GfxRenderingAPIVulkan::LookupShader(uint64_t shaderId0, uint64_t shaderId1) {
    auto it = mShaderProgramPool.find(std::make_pair(shaderId0, shaderId1));
    return it == mShaderProgramPool.end() ? nullptr : reinterpret_cast<ShaderProgram*>(&it->second);
}

void GfxRenderingAPIVulkan::ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    auto* vk = reinterpret_cast<ShaderProgramVulkan*>(prg);
    if (vk == nullptr) {
        *numInputs = 0;
        usedTextures[0] = usedTextures[1] = false;
        return;
    }
    *numInputs = vk->numInputs;
    usedTextures[0] = vk->usedTextures[0];
    usedTextures[1] = vk->usedTextures[1];
}

void GfxRenderingAPIVulkan::LoadShader(ShaderProgram* prg) {
    mCurrentShaderProgram = reinterpret_cast<ShaderProgramVulkan*>(prg);
}
void GfxRenderingAPIVulkan::UnloadShader(ShaderProgram*) {
}

VkPipeline GfxRenderingAPIVulkan::GetOrCreatePipeline(ShaderProgramVulkan* prg, uint32_t stateBits) {
    VulkanPipelineKey key{ prg->id0, prg->id1, stateBits };
    auto it = mPipelineCache.find(key);
    if (it != mPipelineCache.end())
        return it->second;

    const bool depthTest = (stateBits & 1) != 0;
    const bool depthMask = (stateBits & 2) != 0;
    const bool zmodeDecal = (stateBits & 4) != 0;
    const bool useAlpha = (stateBits & 8) != 0;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = prg->vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = prg->frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = prg->numFloats * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs(prg->attribs.size());
    for (size_t i = 0; i < prg->attribs.size(); i++) {
        VkFormat fmt = VK_FORMAT_R32_SFLOAT;
        switch (prg->attribs[i].size) {
            case 1:
                fmt = VK_FORMAT_R32_SFLOAT;
                break;
            case 2:
                fmt = VK_FORMAT_R32G32_SFLOAT;
                break;
            case 3:
                fmt = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case 4:
                fmt = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
        }
        attrs[i].location = (uint32_t)i;
        attrs[i].binding = 0;
        attrs[i].format = fmt;
        attrs[i].offset = prg->attribs[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vin.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_TRUE; // matches GL GL_DEPTH_CLAMP
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // Fast3D culls on the CPU
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = zmodeDecal ? VK_TRUE : VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    if (depthTest || depthMask) {
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = depthMask ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = depthTest ? (zmodeDecal ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS)
                                      : VK_COMPARE_OP_ALWAYS;
    } else {
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    }

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (useAlpha) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        cba.blendEnable = VK_FALSE;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 3;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = mPipelineLayout;
    // All drawable framebuffers (the main FB + the interpreter's effect FBs) share
    // mFbRenderPass, so a single pipeline variant is compatible with every draw target.
    pci.renderPass = mFbRenderPass;
    pci.subpass = 0;

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline));
    mPipelineCache[key] = pipeline;
    return pipeline;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

int GfxRenderingAPIVulkan::GetMaxTextureSize() {
    return mPhysicalDeviceProps.limits.maxImageDimension2D ? (int)mPhysicalDeviceProps.limits.maxImageDimension2D
                                                           : 4096;
}
GfxClipParameters GfxRenderingAPIVulkan::GetClipParameters() {
    // Vulkan clip space: z in [0,1] (z_is_from_0_to_1 = true), and +Y points DOWN (opposite of
    // OpenGL's +Y up). The interpreter negates vertex Y and shifts the viewport/scissor origin
    // when invertY is set (see SetVertices / AdjustVIewportOrScissor), so the value must be chosen
    // per the CURRENTLY-BOUND framebuffer, exactly like the GL backend does
    // (GfxRenderingAPIOGL::GetClipParameters returns the current fb's invertY).
    //
    // Framebuffer openglInvertY flags (set in Interpreter::UpdateFramebuffers): fb 0 (the window)
    // = false; every render-to-texture framebuffer (mGameFb + the game's effect FBs, e.g. the
    // title-screen 3D backdrop and the pause/inventory capture) = true. Because Vulkan's Y is
    // already flipped vs GL, we return the INVERSE of that flag: fb 0 -> true (negate Y so the
    // window image is upright after the present blit, which does not flip), and sampled FBs ->
    // false (don't double-flip, so they read back upright when sampled as a texture).
    //
    // The old code hardcoded {true, true}: correct for fb 0 only, which is why the main scene was
    // fine but every framebuffer-sampled image (title backdrop, inventory) rendered upside down.
    int fb = (mCurrentFb >= 0 && mCurrentFb < (int)mFramebuffers.size()) ? mCurrentFb : 0;
    return { true, !mFramebuffers[fb].invertY };
}
uint32_t GfxRenderingAPIVulkan::NewTexture() {
    uint32_t id = mNextTextureId++;
    if (id >= mTextures.size())
        mTextures.resize(id + 1);
    return id;
}
void GfxRenderingAPIVulkan::SelectTexture(int tile, uint32_t textureId) {
    if (tile < 0 || tile >= 6)
        return;
    mCurrentTextureIds[tile] = textureId;
    mCurrentTile = (uint8_t)tile;
}
void GfxRenderingAPIVulkan::UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return;
    uint32_t id = mCurrentTextureIds[mCurrentTile];
    if (id >= mTextures.size())
        mTextures.resize(id + 1);
    TextureVulkan& t = mTextures[id];
    // (Re)create the image if size changed.
    if (t.image == VK_NULL_HANDLE || t.width != width || t.height != height) {
        if (t.view) {
            // The previous image may still be referenced by an in-flight frame; flush.
            vkDeviceWaitIdle(mDevice);
            vkDestroyImageView(mDevice, t.view, nullptr);
            vkDestroyImage(mDevice, t.image, nullptr);
            vkFreeMemory(mDevice, t.memory, nullptr);
        }
        CreateImageRGBA(width, height, t.image, t.memory, t.view);
        t.width = width;
        t.height = height;
    }
    UploadImageRGBA(t.image, width, height, rgba32Buf);
    t.uploaded = true;
}
void GfxRenderingAPIVulkan::SetSamplerParameters(int tile, bool linearFilter, uint32_t cms, uint32_t cmt) {
    if (tile < 0 || tile >= 6)
        return;
    uint32_t id = mCurrentTextureIds[tile];
    if (id >= mTextures.size())
        return;
    TextureVulkan& t = mTextures[id];
    t.linearFilter = linearFilter && mCurrentFilterMode == FILTER_LINEAR;
    t.filtering = !linearFilter ? FILTER_LINEAR : FILTER_THREE_POINT;
    t.cms = cms;
    t.cmt = cmt;
}
void GfxRenderingAPIVulkan::DeleteTexture(uint32_t texId) {
    if (texId >= mTextures.size())
        return;
    TextureVulkan& t = mTextures[texId];
    if (t.isFbAlias)
        return; // not owned here; freed via DestroyFbResources
    if (t.image == VK_NULL_HANDLE)
        return;
    vkDeviceWaitIdle(mDevice);
    if (t.view)
        vkDestroyImageView(mDevice, t.view, nullptr);
    if (t.image)
        vkDestroyImage(mDevice, t.image, nullptr);
    if (t.memory)
        vkFreeMemory(mDevice, t.memory, nullptr);
    t = TextureVulkan{};
}

// ---------------------------------------------------------------------------
// Render state
// ---------------------------------------------------------------------------

void GfxRenderingAPIVulkan::SetDepthTestAndMask(bool depthTest, bool zUpd) {
    mCurrentDepthTest = depthTest;
    mCurrentDepthMask = zUpd;
}
void GfxRenderingAPIVulkan::SetZmodeDecal(bool decal) {
    mCurrentZmodeDecal = decal;
}
void GfxRenderingAPIVulkan::SetViewport(int x, int y, int width, int height) {
    // Fast3D viewport origin is bottom-left (GL convention); the interpreter already
    // inverts Y in clip space for Vulkan, so use a top-left viewport directly.
    mCurrentViewport.x = (float)x;
    mCurrentViewport.y = (float)y;
    mCurrentViewport.width = (float)width;
    mCurrentViewport.height = (float)height;
    mCurrentViewport.minDepth = 0.0f;
    mCurrentViewport.maxDepth = 1.0f;
}
void GfxRenderingAPIVulkan::SetScissor(int x, int y, int width, int height) {
    int32_t sx = std::max(0, x);
    int32_t sy = std::max(0, y);
    mCurrentScissor.offset = { sx, sy };
    mCurrentScissor.extent = { (uint32_t)std::max(0, width), (uint32_t)std::max(0, height) };
}
void GfxRenderingAPIVulkan::SetUseAlpha(bool useAlpha) {
    mCurrentUseAlpha = useAlpha;
}

void GfxRenderingAPIVulkan::DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) {
    if (!mFrameAcquired || mCurrentShaderProgram == nullptr || mCurrentShaderProgram->vert == VK_NULL_HANDLE)
        return;

    BeginPassIfNeeded();
    if (!mPassOpen)
        return; // current FB has no render target (shouldn't happen for drawable FBs)

    ShaderProgramVulkan* prg = mCurrentShaderProgram;
    FrameRing& fr = mFrameRings[mCurrentFrame];
    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];

    // --- Vertex data into the per-frame ring ---
    const VkDeviceSize vboBytes = bufVboLen * sizeof(float);
    const VkDeviceSize vboAligned = (fr.vboOffset + 0xF) & ~(VkDeviceSize)0xF;
    if (vboAligned + vboBytes > fr.vboCapacity || fr.uboOffset + mUboAlignedSize > fr.uboCapacity) {
        // Ring exhausted this frame: drop the draw rather than corrupt memory.
        return;
    }
    memcpy((uint8_t*)fr.vboMapped + vboAligned, bufVbo, vboBytes);
    fr.vboOffset = vboAligned + vboBytes;

    // --- UBO into the per-frame ring ---
    VkUboData ubo{};
    ubo.frame_count = (int32_t)mFrameCount;
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
    const VkDeviceSize uboOffset = fr.uboOffset;
    memcpy((uint8_t*)fr.uboMapped + uboOffset, &ubo, sizeof(ubo));
    fr.uboOffset += mUboAlignedSize;

    // --- Descriptor set (UBO + 6 sampler slots) ---
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = fr.descPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &mDescriptorSetLayout;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(mDevice, &dai, &set) != VK_SUCCESS)
        return;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = fr.ubo;
    bufInfo.offset = uboOffset;
    bufInfo.range = sizeof(VkUboData);

    VkDescriptorImageInfo imgInfo[6];
    for (int s = 0; s < 6; s++) {
        uint32_t id = mCurrentTextureIds[s];
        VkImageView view = mDummyView;
        VkSampler sampler = mDummySampler;
        if (prg->usedSlot[s] && id < mTextures.size() && mTextures[id].uploaded) {
            const TextureVulkan& t = mTextures[id];
            view = t.view;
            sampler = GetOrCreateSampler(t.linearFilter, t.cms, t.cmt);
        }
        imgInfo[s].sampler = sampler;
        imgInfo[s].imageView = view;
        imgInfo[s].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet writes[7]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &bufInfo;
    for (int s = 0; s < 6; s++) {
        writes[s + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[s + 1].dstSet = set;
        writes[s + 1].dstBinding = s + 1;
        writes[s + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[s + 1].descriptorCount = 1;
        writes[s + 1].pImageInfo = &imgInfo[s];
    }
    vkUpdateDescriptorSets(mDevice, 7, writes, 0, nullptr);

    // --- Record the draw ---
    uint32_t stateBits = (mCurrentDepthTest ? 1u : 0u) | (mCurrentDepthMask ? 2u : 0u) |
                         (mCurrentZmodeDecal ? 4u : 0u) | (mCurrentUseAlpha ? 8u : 0u);
    VkPipeline pipeline = GetOrCreatePipeline(prg, stateBits);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(cmd, 0, 1, &mCurrentViewport);
    vkCmdSetScissor(cmd, 0, 1, &mCurrentScissor);
    // Slope-scaled bias matching the GL backend's default zmode-decal offset.
    vkCmdSetDepthBias(cmd, mCurrentZmodeDecal ? -2.0f : 0.0f, 0.0f, mCurrentZmodeDecal ? -2.0f : 0.0f);
    VkDeviceSize vbOffset = vboAligned;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fr.vbo, &vbOffset);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, 3 * (uint32_t)bufVboNumTris, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// Framebuffers (M3): real per-id offscreen VkImage color+depth render targets.
//
// Every drawable framebuffer shares mFbRenderPass (BGRA color + D32 depth, LOAD/STORE).
// fb 0 is the "main" buffer the whole frame renders into; FinishRender blits its color
// onto the swapchain to present. The interpreter binds a target with
// StartDrawToFramebuffer (which only ends the current pass — the new pass opens lazily
// on the next draw/clear), samples a buffer back with SelectTextureFb, and blits between
// buffers with CopyFramebuffer / ResolveMSAAColorBuffer. Mirrors gfx_opengl.cpp's
// FramebufferOGL path; the Vulkan-specific work is render-pass switching and explicit
// image-layout transitions (vs GL's implicit FBO binding).
// ---------------------------------------------------------------------------

void GfxRenderingAPIVulkan::CreateFbRenderPass() {
    VkAttachmentDescription color{};
    color.format = mSwapchainFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // preserve; clears are explicit (ClearFramebuffer)
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = mDepthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // kept so GetPixelDepth can read it back
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // External dependencies pair the pass with the layout transitions / sampling /
    // blits we record around it (the image is already in the attachment layout via an
    // explicit barrier before begin, so the pass itself performs no layout change).
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    deps[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    VkAttachmentDescription attachments[] = { color, depth };
    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;

    VK_CHECK(vkCreateRenderPass(mDevice, &ci, nullptr, &mFbRenderPass));
}

void GfxRenderingAPIVulkan::CreateFbResources(FramebufferVulkan& fb, uint32_t width, uint32_t height, bool hasDepth) {
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    fb.width = width;
    fb.height = height;
    fb.hasDepth = hasDepth;

    // Color image. Swapchain format so the present/copy blits never swizzle; usage
    // covers attachment, sampling (SelectTextureFb), and both blit directions.
    {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = mSwapchainFormat;
        ii.extent = { width, height, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(mDevice, &ii, nullptr, &fb.colorImage));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(mDevice, fb.colorImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(mDevice, &ai, nullptr, &fb.colorMem));
        VK_CHECK(vkBindImageMemory(mDevice, fb.colorImage, fb.colorMem, 0));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = fb.colorImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = mSwapchainFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(mDevice, &vi, nullptr, &fb.colorView));
        fb.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (hasDepth) {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = mDepthFormat;
        ii.extent = { width, height, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(mDevice, &ii, nullptr, &fb.depthImage));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(mDevice, fb.depthImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(mDevice, &ai, nullptr, &fb.depthMem));
        VK_CHECK(vkBindImageMemory(mDevice, fb.depthImage, fb.depthMem, 0));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = fb.depthImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = mDepthFormat;
        // Combined view: stencil aspect must be included so the framebuffer attachment is valid
        // for D32_SFLOAT_S8_UINT.
        vi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(mDevice, &vi, nullptr, &fb.depthView));
        fb.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    // The VkFramebuffer binds color+depth to mFbRenderPass. Only built for drawable
    // targets; mFbRenderPass requires a depth attachment, so a render target without a
    // depth buffer (e.g. fb 0 during internal-resolution scaling, composited by ImGui in
    // M4) gets no VkFramebuffer and is not directly drawn into here.
    if (fb.renderTarget && hasDepth) {
        VkImageView att[2] = { fb.colorView, fb.depthView };
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = mFbRenderPass;
        fi.attachmentCount = 2;
        fi.pAttachments = att;
        fi.width = width;
        fi.height = height;
        fi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(mDevice, &fi, nullptr, &fb.fb));
    }

    // Alias this FB's color image into the texture table so combiner draws can sample it
    // (SelectTextureFb) and ImGui can reference it (GetFramebufferTextureId). The entry
    // does not own the image — DeleteTexture / teardown skip it (isFbAlias).
    if (fb.colorTexId == 0) {
        uint32_t id = mNextTextureId++;
        if (id >= mTextures.size())
            mTextures.resize(id + 1);
        fb.colorTexId = id;
    }
    TextureVulkan& t = mTextures[fb.colorTexId];
    t = TextureVulkan{};
    t.isFbAlias = true;
    t.view = fb.colorView;
    t.width = width;
    t.height = height;
    t.uploaded = true;
    t.linearFilter = true;
    t.filtering = FILTER_LINEAR; // never the in-shader three-point path
    t.cms = G_TX_NOMIRROR | G_TX_CLAMP;
    t.cmt = G_TX_NOMIRROR | G_TX_CLAMP;
}

void GfxRenderingAPIVulkan::DestroyFbResources(FramebufferVulkan& fb) {
    if (fb.fb != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(mDevice, fb.fb, nullptr);
        fb.fb = VK_NULL_HANDLE;
    }
    if (fb.colorView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, fb.colorView, nullptr);
        fb.colorView = VK_NULL_HANDLE;
    }
    if (fb.colorImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice, fb.colorImage, nullptr);
        fb.colorImage = VK_NULL_HANDLE;
    }
    if (fb.colorMem != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, fb.colorMem, nullptr);
        fb.colorMem = VK_NULL_HANDLE;
    }
    if (fb.depthView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, fb.depthView, nullptr);
        fb.depthView = VK_NULL_HANDLE;
    }
    if (fb.depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice, fb.depthImage, nullptr);
        fb.depthImage = VK_NULL_HANDLE;
    }
    if (fb.depthMem != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, fb.depthMem, nullptr);
        fb.depthMem = VK_NULL_HANDLE;
    }
    // Invalidate the texture alias's dangling view (the id is kept stable across resize).
    if (fb.colorTexId != 0 && fb.colorTexId < mTextures.size()) {
        mTextures[fb.colorTexId].view = VK_NULL_HANDLE;
        mTextures[fb.colorTexId].uploaded = false;
    }
    fb.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    fb.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

namespace {
// Access + pipeline-stage masks for an image layout, used to derive transition barriers.
std::pair<VkAccessFlags, VkPipelineStageFlags> vk_layout_masks(VkImageLayout l) {
    switch (l) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return { VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return { VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return { VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return { VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return { VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return { VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            return { 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
    }
}
} // namespace

void GfxRenderingAPIVulkan::TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                                                  VkImageLayout& tracked, VkImageLayout newLayout) {
    if (tracked == newLayout)
        return;
    auto src = vk_layout_masks(tracked);
    auto dst = vk_layout_masks(newLayout);
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = tracked;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = { aspect, 0, 1, 0, 1 };
    b.srcAccessMask = src.first;
    b.dstAccessMask = dst.first;
    vkCmdPipelineBarrier(cmd, src.second, dst.second, 0, 0, nullptr, 0, nullptr, 1, &b);
    tracked = newLayout;
}

void GfxRenderingAPIVulkan::BeginPassIfNeeded() {
    if (mPassOpen)
        return;
    if (mCurrentFb < 0 || mCurrentFb >= (int)mFramebuffers.size())
        return;
    FramebufferVulkan& fb = mFramebuffers[mCurrentFb];
    if (fb.fb == VK_NULL_HANDLE)
        return; // not a drawable render target (no VkFramebuffer)

    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];
    TransitionImageLayout(cmd, fb.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, fb.colorLayout,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (fb.depthImage != VK_NULL_HANDLE)
        TransitionImageLayout(cmd, fb.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                              fb.depthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = mFbRenderPass;
    rp.framebuffer = fb.fb;
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = { fb.width, fb.height };
    rp.clearValueCount = 0; // LOAD_OP_LOAD; clears come from ClearFramebuffer
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    mPassOpen = true;
}

void GfxRenderingAPIVulkan::EndPassIfOpen() {
    if (!mPassOpen)
        return;
    vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
    mPassOpen = false;
    // The images are now in mFbRenderPass's finalLayout (= the *_ATTACHMENT_OPTIMAL the
    // tracked layouts already hold), so no tracked-layout update is needed.
}

bool GfxRenderingAPIVulkan::BeginSoH3DPass(SoH3DVkContext& out) {
    if (!mFrameAcquired)
        return false;
    BeginPassIfNeeded(); // open the current FB's render pass so the SoH3D draws share its depth
    if (!mPassOpen)
        return false;
    out.device = mDevice;
    out.physicalDevice = mPhysicalDevice;
    out.graphicsQueue = mGraphicsQueue;
    out.commandPool = mCommandPool;
    out.cmd = mCommandBuffers[mCurrentFrame];
    out.renderPass = mFbRenderPass;
    out.viewport = mCurrentViewport;
    out.scissor = mCurrentScissor;
    out.frameIndex = mCurrentFrame;
    out.framesInFlight = kMaxFramesInFlight;
    return true;
}

bool GfxRenderingAPIVulkan::BeginSoH3DOffscreen(SoH3DVkContext& out) {
    if (!mFrameAcquired)
        return false;
    EndPassIfOpen(); // close the FB pass so the offscreen pass can begin its own render pass
    out.device = mDevice;
    out.physicalDevice = mPhysicalDevice;
    out.graphicsQueue = mGraphicsQueue;
    out.commandPool = mCommandPool;
    out.cmd = mCommandBuffers[mCurrentFrame];
    out.renderPass = mFbRenderPass;
    out.viewport = mCurrentViewport;
    out.scissor = mCurrentScissor;
    out.frameIndex = mCurrentFrame;
    out.framesInFlight = kMaxFramesInFlight;
    return true;
}

void GfxRenderingAPIVulkan::FlushCommandsAndWait() {
    if (!mFrameAcquired)
        return;
    EndPassIfOpen();
    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(mDevice, &fci, nullptr, &fence));
    // No semaphores: this split submit touches only offscreen FBs, not the swapchain
    // image, and the GPU executes queue submissions in order, so the later FinishRender
    // submit (which does wait on the acquire semaphore) stays correctly ordered after it.
    VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submit, fence));
    vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(mDevice, fence, nullptr);

    // Resume recording into the same command buffer. The per-frame rings and descriptor
    // pool are intentionally NOT reset — the frame keeps accumulating.
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    mPassOpen = false;
}

int GfxRenderingAPIVulkan::CreateFramebuffer() {
    int id = (int)mFramebuffers.size();
    mFramebuffers.emplace_back();
    return id;
}

void GfxRenderingAPIVulkan::UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel,
                                                        bool openglInvertY, bool renderTarget, bool hasDepthBuffer,
                                                        bool canExtractDepth) {
    (void)msaaLevel;       // single-sample only for now (see FramebufferVulkan note)
    (void)canExtractDepth; // depth is always stored, so always extractable
    if (fbId < 0)
        return;
    if (fbId >= (int)mFramebuffers.size())
        mFramebuffers.resize(fbId + 1);
    FramebufferVulkan& fb = mFramebuffers[fbId];
    width = std::max(width, 1u);
    height = std::max(height, 1u);

    fb.invertY = openglInvertY;
    fb.renderTarget = renderTarget || fbId == 0; // fb 0 is always the main render target

    const bool changed =
        fb.colorImage == VK_NULL_HANDLE || fb.width != width || fb.height != height || fb.hasDepth != hasDepthBuffer;
    if (changed) {
        // Images may still be referenced by an in-flight frame.
        vkDeviceWaitIdle(mDevice);
        uint32_t keepTexId = fb.colorTexId; // stable alias id across resize
        DestroyFbResources(fb);
        fb.colorTexId = keepTexId;
        CreateFbResources(fb, width, height, hasDepthBuffer);
    }
}

void GfxRenderingAPIVulkan::StartDrawToFramebuffer(int fbId, float noiseScale) {
    if (noiseScale != 0.0f)
        mCurrentNoiseScale = 1.0f / noiseScale;
    if (fbId == mCurrentFb)
        return;
    // Switching targets: end the current pass; the new one opens lazily on the next draw.
    EndPassIfOpen();
    mCurrentFb = fbId;
}

void GfxRenderingAPIVulkan::ClearFramebuffer(bool color, bool depth) {
    BeginPassIfNeeded();
    if (!mPassOpen)
        return;
    FramebufferVulkan& fb = mFramebuffers[mCurrentFb];
    VkClearAttachment att[2]{};
    uint32_t n = 0;
    if (color) {
        att[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        att[n].colorAttachment = 0;
        att[n].clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        n++;
    }
    if (depth && fb.hasDepth) {
        att[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        att[n].clearValue.depthStencil = { 1.0f, 0 };
        n++;
    }
    if (n == 0)
        return;
    VkClearRect rect{};
    rect.rect.offset = { 0, 0 };
    rect.rect.extent = { fb.width, fb.height };
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;
    vkCmdClearAttachments(mCommandBuffers[mCurrentFrame], n, att, 1, &rect);
}

void GfxRenderingAPIVulkan::CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1,
                                            int dstX0, int dstY0, int dstX1, int dstY1) {
    if (fbDstId < 0 || fbSrcId < 0 || fbDstId >= (int)mFramebuffers.size() || fbSrcId >= (int)mFramebuffers.size())
        return;
    FramebufferVulkan& src = mFramebuffers[fbSrcId];
    FramebufferVulkan& dst = mFramebuffers[fbDstId];
    if (src.colorImage == VK_NULL_HANDLE || dst.colorImage == VK_NULL_HANDLE)
        return;

    // NO Y compensation, unlike gfx_opengl.cpp::CopyFramebuffer. That GL code flips Y to
    // account for GL's bottom-left framebuffer origin; in Vulkan EVERY framebuffer color
    // image is stored top-down upright (the present blit is a straight copy, and
    // render-to-texture FBs rasterize upright via the GetClipParameters invertY inversion).
    // So all FBs share one storage orientation and a straight image-space blit is correct.
    // The old code mirrored the GL Y-flip verbatim, which injected a spurious vertical flip
    // whenever src.invertY != dst.invertY -- e.g. capturing fb 0 (invertY=false) into the
    // pause/inventory buffer (invertY=true), the #12 upside-down-pause-background bug.
    // (Same root cause class the GetClipParameters comment already fixed: a GL flag/flip that
    // must NOT be replicated for Vulkan.) The interpreter passes top-left rects directly.

    EndPassIfOpen();
    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];
    TransitionImageLayout(cmd, src.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, src.colorLayout,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    TransitionImageLayout(cmd, dst.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, dst.colorLayout,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[0] = { srcX0, srcY0, 0 };
    blit.srcOffsets[1] = { srcX1, srcY1, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[0] = { dstX0, dstY0, 0 };
    blit.dstOffsets[1] = { dstX1, dstY1, 1 };
    vkCmdBlitImage(cmd, src.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.colorImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
}

void GfxRenderingAPIVulkan::ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) {
    if (fbIdTarger < 0 || fbIdSrc < 0 || fbIdTarger >= (int)mFramebuffers.size() ||
        fbIdSrc >= (int)mFramebuffers.size())
        return;
    FramebufferVulkan& dst = mFramebuffers[fbIdTarger];
    FramebufferVulkan& src = mFramebuffers[fbIdSrc];
    if (src.colorImage == VK_NULL_HANDLE || dst.colorImage == VK_NULL_HANDLE)
        return;
    // Single-sample FBs (no true MSAA yet): a plain full-image color blit. Equivalent to
    // the GL backend's resolve-then-present when MSAA is off.
    EndPassIfOpen();
    VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];
    TransitionImageLayout(cmd, src.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, src.colorLayout,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    TransitionImageLayout(cmd, dst.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, dst.colorLayout,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[0] = { 0, 0, 0 };
    blit.srcOffsets[1] = { (int32_t)src.width, (int32_t)src.height, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[0] = { 0, 0, 0 };
    blit.dstOffsets[1] = { (int32_t)dst.width, (int32_t)dst.height, 1 };
    vkCmdBlitImage(cmd, src.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.colorImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPIVulkan::GetPixelDepth(int fbId, const std::set<std::pair<float, float>>& coordinates) {
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> res;
    if (fbId < 0 || fbId >= (int)mFramebuffers.size() || coordinates.empty()) {
        for (const auto& c : coordinates)
            res.emplace(c, 0);
        return res;
    }
    FramebufferVulkan& fb = mFramebuffers[fbId];
    if (!fb.hasDepth || fb.depthImage == VK_NULL_HANDLE) {
        for (const auto& c : coordinates)
            res.emplace(c, 0);
        return res;
    }

    // Make all prior depth writes for this frame visible on the GPU, then copy the
    // requested depth texels (D32_SFLOAT) back to the host via a one-shot transfer.
    FlushCommandsAndWait();

    const VkDeviceSize size = (VkDeviceSize)coordinates.size() * sizeof(float);
    VkBuffer buffer;
    VkDeviceMemory mem;
    void* mapped = nullptr;
    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer, mem, &mapped);

    std::vector<VkBufferImageCopy> regions;
    regions.reserve(coordinates.size());
    {
        VkDeviceSize off = 0;
        for (const auto& c : coordinates) {
            int x = (int)c.first;
            int y = (int)c.second;
            if (fb.invertY)
                y = (int)fb.height - y;
            x = std::clamp(x, 0, (int)fb.width - 1);
            y = std::clamp(y, 0, (int)fb.height - 1);
            VkBufferImageCopy r{};
            r.bufferOffset = off;
            r.imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
            r.imageOffset = { x, y, 0 };
            r.imageExtent = { 1, 1, 1 };
            regions.push_back(r);
            off += sizeof(float);
        }
    }

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = mCommandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &cai, &cmd));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cbi));
    // Transition both aspects (depth+stencil) to TRANSFER_SRC; we only copy the depth aspect.
    TransitionImageLayout(cmd, fb.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                          fb.depthLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdCopyImageToBuffer(cmd, fb.depthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, (uint32_t)regions.size(),
                           regions.data());
    TransitionImageLayout(cmd, fb.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                          fb.depthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(mDevice, &fci, nullptr, &fence));
    VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submit, fence));
    vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(mDevice, fence, nullptr);
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmd);

    const float* depths = static_cast<const float*>(mapped);
    {
        size_t i = 0;
        for (const auto& c : coordinates) {
            // Match the GL backend's 24-bit depth -> N64 16-bit mapping:
            // GL returns ((d24 << 8) >> 18) << 2 == (d24 >> 10) << 2.
            uint32_t d24 = (uint32_t)(std::clamp(depths[i], 0.0f, 1.0f) * 16777215.0f);
            res.emplace(c, (uint16_t)((d24 >> 10) << 2));
            i++;
        }
    }
    vkUnmapMemory(mDevice, mem);
    vkDestroyBuffer(mDevice, buffer, nullptr);
    vkFreeMemory(mDevice, mem, nullptr);
    return res;
}

void* GfxRenderingAPIVulkan::GetFramebufferTextureId(int fbId) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return nullptr;
    return (void*)(uintptr_t)mFramebuffers[fbId].colorTexId;
}

void GfxRenderingAPIVulkan::SelectTextureFb(int fbId) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return;
    FramebufferVulkan& fb = mFramebuffers[fbId];
    if (fb.colorImage == VK_NULL_HANDLE)
        return;
    // Make the FB's color image shader-readable (outside any render pass), then bind its
    // texture alias on tile 0 so the next combiner draw samples it.
    EndPassIfOpen();
    TransitionImageLayout(mCommandBuffers[mCurrentFrame], fb.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, fb.colorLayout,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (fb.colorTexId != 0 && fb.colorTexId < mTextures.size()) {
        mTextures[fb.colorTexId].view = fb.colorView;
        mTextures[fb.colorTexId].uploaded = (fb.colorView != VK_NULL_HANDLE);
    }
    SelectTexture(0, fb.colorTexId);
}

void GfxRenderingAPIVulkan::ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) {
    if (fbId < 0 || fbId >= (int)mFramebuffers.size())
        return;
    FramebufferVulkan& fb = mFramebuffers[fbId];
    if (fb.colorImage == VK_NULL_HANDLE)
        return;
    width = std::min(width, fb.width);
    height = std::min(height, fb.height);

    FlushCommandsAndWait();

    const VkDeviceSize size = (VkDeviceSize)fb.width * fb.height * 4;
    VkBuffer buffer;
    VkDeviceMemory mem;
    void* mapped = nullptr;
    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer, mem, &mapped);

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = mCommandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &cai, &cmd));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &cbi));
    TransitionImageLayout(cmd, fb.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, fb.colorLayout,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { fb.width, fb.height, 1 };
    vkCmdCopyImageToBuffer(cmd, fb.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
    TransitionImageLayout(cmd, fb.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, fb.colorLayout,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(mDevice, &fci, nullptr, &fence));
    VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submit, fence));
    vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(mDevice, fence, nullptr);
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmd);

    // Pack BGRA8 (host byte order of the swapchain-format FB image) into RGBA 5551,
    // matching gfx_opengl.cpp::ReadFramebufferToCPU's r/g/b/a layout.
    const uint8_t* px = static_cast<const uint8_t*>(mapped);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t* p = &px[((size_t)y * fb.width + x) * 4];
            uint8_t r = (p[2] >> 3) & 0x1F;
            uint8_t g = (p[1] >> 3) & 0x1F;
            uint8_t b = (p[0] >> 3) & 0x1F;
            uint8_t a = p[3] ? 1 : 0;
            rgba16Buf[(size_t)y * width + x] = (r << 11) | (g << 6) | (b << 1) | a;
        }
    }
    vkUnmapMemory(mDevice, mem);
    vkDestroyBuffer(mDevice, buffer, nullptr);
    vkFreeMemory(mDevice, mem, nullptr);
}

void GfxRenderingAPIVulkan::SetTextureFilter(FilteringMode mode) {
    mCurrentFilterMode = mode;
}
FilteringMode GfxRenderingAPIVulkan::GetTextureFilter() {
    return mCurrentFilterMode;
}
void GfxRenderingAPIVulkan::SetSrgbMode() {
    mSrgbMode = true;
}
ImTextureID GfxRenderingAPIVulkan::GetTextureById(int id) {
    return reinterpret_cast<ImTextureID>((uintptr_t)id);
}
void GfxRenderingAPIVulkan::SetCurrentPrimDepth(float depth) {
    mCurrentPrimDepth = depth;
}

} // namespace Fast

#endif // ENABLE_VULKAN
