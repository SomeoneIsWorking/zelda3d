// Zelda3D SDL3 GPU renderer — the OoT3D skinned-model renderer and HUD, folded
// into the SDL3 GPU backend (Fast::GfxRenderingAPISdl3Gpu) as MEMBER SUBSYSTEMS.
//
// Zelda3DRenderer and Zelda3DHudRenderer are owned by the backend. The model renderer's C ABI,
// upload/cache, pipeline/shader, pass/diagnostic, and lifecycle implementations live in focused
// sibling modules; this header contains their shared state contract. The extern "C" entry points
// remain stable adapters to the live instance via Fast::g_activeSdl3GpuApi.
#pragma once
#ifdef ENABLE_SDL3GPU

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include <glslang/Public/ShaderLang.h> // EShLanguage (makeShader parameter)

#include "fast/zelda3d_sg_ubo.h"          // Zelda3DSg::SgUbo (per-draw uniform payload size)
#include "fast/backends/unified_shader.h" // Fast::Unified::Variant (render-unification, kanban #131)

namespace Fast {

// ---- model-renderer record types ----

struct SgGroup {
    uint32_t first = 0, count = 0;
    int texIndex = -1;
    int alphaTest = 0;
    float alphaRef = 0.0f;
    unsigned minFilter = 0x2601, magFilter = 0x2601;
    unsigned wrapS = 0x2901, wrapT = 0x2901;
    int blendEnable = 0;
    unsigned bSrcRGB = 0x0302, bDstRGB = 0x0303, bEqRGB = 0x8006;
    unsigned bSrcA = 1, bDstA = 0, bEqA = 0x8006;
    // CmbMaterial.blend_color — the operand of the GL_CONSTANT_COLOR/GL_CONSTANT_ALPHA (0x8001-
    // 0x8004) blend factors. 91 corpus materials (all water/waterfall surfaces) blend with
    // dstRGB = GL_CONSTANT_ALPHA and an authored weight in .a; without carrying it here the draw
    // composites at dst factor ONE, i.e. additively, instead of at that weight.
    float blendColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    int depthWrite = 1;
    // Per-group local AABB, filled at upload alongside the model-wide one. Needed to AIM a
    // camera at a specific draw group: `acam` frames ACTORS, so room geometry -- where most
    // material defects live -- had no way to be brought on screen for verification.
    float gmin[3] = { 0, 0, 0 }, gmax[3] = { 0, 0, 0 };
    bool hasGroupBounds = false;
    unsigned alphaFunc = 0x0206;
    int depthTest = 1;
    unsigned depthFunc = 0x0201; // GL_LESS
    float polygonOffset = 0.0f;
    int cull = 0;
    int faceCull = 0;
    int meshId = -1;
    int materialIndex = -1;
    int vertexLighting = 0;
    int fragmentLighting = 0; // CMB +0x00: enables PICA fixed-function fragment lighting
    int hasColor = 0;         // CmbVShader HasColor draw uniform
    int fogEnabled = 0;       // CMB isFogEnabled (+0x02): PICA distance fog applies to this draw
    float combScaleRGB = 1.0f;
    float matAmbient[3] = { 1.0f, 1.0f, 1.0f };
    float matDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    // PICA200 TEV constant palette + stage-0 selector (see Zelda3DGlGroup::matConstant /
    // combConstIdx). Populated by the model provider; overwritten by the per-actor override
    // channel (Step 2c EnHy body-color port) before submit.
    float matConstant[6][4] = {
        { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 },
    };
    int combConstIdx = 0;
    int combUsesConst = 0;
    // Hardware RGB scale (1/2/4) of the CONSTANT-sourcing stage; applied with the CONSTANT
    // modulate via uMatConst.a (fire-glow ×2 — title_logo_fireglow_cmab.md §3.2 fix 1).
    float combConstScaleRGB = 1.0f;
    // Dual-texture combine: sample tex1Index through coordinator 1 and combine per dualTexMode
    // (0=off, 1=(t0+t1)*t0, 2=(t0+t1)*primary, 3=dualTexScale2*(primary*t0*t1); see
    // CmbMaterial::DualTexMode in cmb.h).
    int dualTexMode = 0;
    float dualTexScale2 = 1.0f;
    float uv0Scale[2] = { 1.0f, 1.0f };
    float uv0Trans[2] = { 0.0f, 0.0f };
    int coord0Mapping = 1; // coordinator-0 mapping method: 1=UV, 3=SphereEnvMap
    int tex1Index = -1;
    unsigned min1Filter = 0x2601, mag1Filter = 0x2601;
    unsigned wrap1S = 0x2901, wrap1T = 0x2901;
    float uv1Scale[2] = { 1.0f, 1.0f };
    float uv1Trans[2] = { 0.0f, 0.0f };
    int coord1Mapping = 1; // coordinator-1 mapping method: 1=UV, 3=SphereEnvMap
    // Generic per-stage TEV chain (render.multi-stage-tev; see Zelda3DGlGroup::tevStagePack).
    int tevGeneric = 0;
    int tevStageCount = 0;
    unsigned tevStagePack[6][3] = {};
    int tex2Index = -1;
    unsigned min2Filter = 0x2601, mag2Filter = 0x2601;
    unsigned wrap2S = 0x2901, wrap2T = 0x2901;
    float uv2Scale[2] = { 1.0f, 1.0f };
    float uv2Trans[2] = { 0.0f, 0.0f };
    int coord2Mapping = 1;
    float dbgColor0[4] = { -1, -1, -1, -1 }; // sample of vertex[first].color (sgdump diagnostics)
    float dbgUv0[2] = { 0, 0 }, dbgUv1[2] = { 0, 0 }, dbgUv2[2] = { 0, 0 }; // sample uvs (sgdump)
};

struct SgModel {
    bool uploaded = false, failed = false;
    SDL_GPUBuffer* vbo = nullptr;
    std::vector<SgGroup> groups;
    std::vector<SDL_GPUTexture*> textures;
    // Model-local AABB over all group vertices, computed once at upload. Used by the geomscan
    // bridge (Zelda3D_GeomScanDump) to flag misrendered geometry by VALUE for the #115/#120 audit.
    bool hasBounds = false;
    float localMin[3] = { 0, 0, 0 }, localMax[3] = { 0, 0, 0 };

    // Render-unification effort (kanban #131), Phase 2: lazily-built UnifiedVtx buffer, same
    // group first/count indices as `vbo` (same source verts, different per-vertex layout — see
    // unified_vtx.h). Built only the first time gUnifiedRenderer routes a draw through it, so the
    // default (unified off) path never pays this cost.
    SDL_GPUBuffer* unifiedVbo = nullptr;
    bool unifiedUploaded = false, unifiedFailed = false;
};

// geomscan capture: each visible model draw appends a world-space AABB record.
struct GeomRec {
    int modelId;
    float wmin[3], wmax[3];
};

// Pipeline cache: key = blend/depth/cull flags + 6 blend params + frontCW.
struct PipeKey {
    std::array<uint32_t, 8> v;
    bool operator<(const PipeKey& o) const {
        return v < o.v;
    }
};

// The OoT3D skinned-model renderer. Holds the model renderer's device resources and per-pass state.
// Methods borrow the backend (device,
// AppendZelda3DModelDraw / AppendZelda3DFullscreen / AppendZelda3DOwnPass, GpuColorFormat, ...) via the
// global Fast::g_activeSdl3GpuApi, exactly as the former free functions did.
class Zelda3DRenderer {
  public:
    // ---- API (former extern "C" Zelda3D_Sg_* bodies; shims forward here) ----
    int GeomScanDump(int* modelIds, float* mins, float* maxs, int maxN);
    void RequestEvictRange(int lo, int hi);
    void BeginPass();
    void DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY, unsigned char r8,
                   unsigned char g8, unsigned char b8, unsigned char a8, float aspectAdj, const float* boneData,
                   int boneCnt, unsigned long long midMask, int sky, float uvOffU, float uvOffV, const void* matTex,
                   const void* matConst, const void* matUv, int forceUnlit, const float* lightDirOv = nullptr,
                   const float* sphereNormalOv = nullptr);
    void EndPass();
    void ClearOverlayDepth(); // #146 item B — fullscreen depth-only reset, in-pass, no color write.

    // Compile the fixed renderer shaders while the backend is initializing. Failure is fatal to the
    // Zelda3D target; deferring this to the first frame would turn an invalid shader into a black
    // world and repeatedly retry it during rendering.
    bool initializeGpuResources();

    // ---- internal helpers (former anonymous-namespace functions) ----
    // Hand every GPU object this renderer OWNS back to the device, before the device is destroyed.
    //
    // Called from ~GfxRenderingAPISdl3Gpu. Until now nothing did: the destructor's comment said these
    // "are owned by the device and freed at SDL_DestroyGPUDevice", and the Vulkan validation layer
    // disagreed -- 409 objects still alive at vkDestroyDevice, 362 of them VkImageView, which is this
    // renderer's per-model textures (docs/issues/0009).
    //
    // `note` is the backend's duplicate-release accounting, passed in because it is file-static over
    // there: it returns false for a handle already released, so a borrowed pointer (the shared dummy
    // texture, say) cannot be freed twice. The gate asserts that count is zero.
    void releaseGpuResources(bool (*note)(const void* handle, const char* what));

    // Local AABB of one uploaded draw group (see Zelda3D_Sg_GroupBounds / REPL `camdraw`).
    bool groupBounds(int modelId, int groupIdx, float* outMin, float* outMax) const;

  private:
    // Focused owners implement these helpers in the resource-cache and pipeline-cache modules.

    SDL_GPUSampler* getSampler(unsigned minFilter, unsigned magFilter, unsigned wrapS, unsigned wrapT);
    SDL_GPUTexture* uploadTexture(int w, int h, const unsigned char* rgba, int srcLevels = 1);
    bool ensureResources();
    SDL_GPUGraphicsPipeline* getPipeline(const SgGroup& g, int frontCW);
    SgModel* ensureUploaded(int modelId);
    // Render-unification effort (kanban #131), Phase 2.
    SgModel* ensureUnifiedUploaded(int modelId);
    SDL_GPUGraphicsPipeline* getUnifiedPipeline(const SgGroup& g, int frontCW, int variant);
    void applyPendingEvict();
    SDL_GPUShader* makeShader(const char* glsl, EShLanguage stage, uint32_t numSamplers, uint32_t numUbo);
    bool ensureOverlayDepthResources(); // #146 item B

    // ---- state (former module globals; names unchanged) ----
    std::unordered_map<int, SgModel> g_models;
    std::vector<GeomRec> g_geomCur, g_geomLast;

    SDL_GPUDevice* g_device = nullptr;
    bool g_resReady = false;
    SDL_GPUShader* g_vert = nullptr;
    SDL_GPUShader* g_frag = nullptr;
    // The 1x1 white dummy texture/sampler (untextured groups + shadow slot when off) and per-wrap
    // samplers now come from the backend's single shared caches (GfxRenderingAPISdl3Gpu::DummyTexture/
    // DummySampler/GetOrCreateSamplerEx), so the model renderer keeps no duplicates of its own.
    std::map<PipeKey, SDL_GPUGraphicsPipeline*> g_pipelines;
    bool g_ctxValid = false;

    // Render-unification effort (kanban #131), Phase 2: unified-shader variant shaders (lazily
    // compiled, one vertex+fragment pair per Fast::Unified::Variant) and the pipeline cache built
    // from them — kept separate from g_vert/g_frag/g_pipelines (the old fixed-shader path) so
    // gUnifiedRenderer==0 never even touches this state.
    SDL_GPUShader* g_uniVert[(int)Fast::Unified::Variant::kCount] = {};
    SDL_GPUShader* g_uniFrag[(int)Fast::Unified::Variant::kCount] = {};
    std::map<PipeKey, SDL_GPUGraphicsPipeline*> g_uniPipelines;

    // Deferred model eviction (mirror of the GL/VK path).
    int g_evictLo = 0, g_evictHi = 0;
    bool g_evictPending = false;

    // #146 item B: fullscreen depth-only reset (Zelda3D_Overlay2D_Begin's depth scope). A minimal
    // pipeline: color_write_mask=0 (composited 3D scene color untouched), depth test ALWAYS +
    // depth write ON, fragment shader unconditionally writes gl_FragDepth=1.0 (far, matching the
    // 0=near/1=far convention the shadow/AO depth passes already use — see kDepthColorFormat's
    // dt.clear_depth=1.0f in replayDepthPass). Reuses kAoCompVert's fullscreen-triangle trick.
    bool g_overlayDepthResReady = false;
    SDL_GPUShader* g_overlayDepthFrag = nullptr;
    SDL_GPUGraphicsPipeline* g_overlayDepthPipe = nullptr;
};

// ---- HUD record types ----

// One HUD vertex. `r,g,b,a` is the N64 PRIM colour for this quad.
//
// `er,eg,eb` is the N64 ENV colour and `mode` selects how the texture combines with them, so the
// native HUD can reproduce the two combiners the N64 HUD actually uses without a pipeline switch
// (both live in the vertex stream, so quads of either kind still coalesce into one draw run):
//   mode 0 — MODULATE: frag = TEXEL0 * PRIM. Item icons, digits, glyphs, keycaps, and (because our
//            HUD textures store intensity in rgb and coverage in a) the N64 MODULATEIA_PRIM cases
//            like the button disc and counter icons.
//   mode 1 — PRIM/ENV LERP: frag.rgb = mix(ENV, PRIM, TEXEL0.r), frag.a = TEXEL0.a * PRIM.a. This is
//            z_lifemeter.c's heart combine ((PRIM-ENV)*TEXEL0+ENV), which is what gives a heart its
//            body/rim shading and its beating/double-defense colour sets.
//   mode 2 — as mode 1 but frag.a = PRIM.a, ignoring the texel. The magic-bar fill's combine takes
//            its alpha from PRIMITIVE alone, so the texel's intensity must not modulate coverage.
struct HudVert {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
    uint8_t er, eg, eb, mode;
};

constexpr uint32_t kMaxQuadsPerFrame = 2048;
constexpr uint32_t kVertsPerQuad = 6;
// One ring slot is consumed per FLUSH, not per frame: a converted HUD element flushes at its own
// marker (#205), so a frame consumes as many slots as it has converted elements. Sized so a slot is
// not reused while a frame that referenced it may still be in flight.
constexpr uint32_t kRingFrames = 24;

// One contiguous run of quads sharing a texture (a single SDL_DrawGPUPrimitives).
// One contiguous run of quads sharing a texture AND a sampler. The sampler varies because most HUD
// quads clamp but a few TILE (the magic bar's middle section repeats a 24px strip across the whole
// bar), and a clamped sampler would smear the last column instead of repeating.
struct DrawRun {
    SDL_GPUTexture* tex;
    SDL_GPUSampler* sampler;
    uint32_t firstVert, vertCount;
};

struct HudSg {
    SDL_GPUDevice* device = nullptr;
    bool ready = false;
    SDL_GPUShader* vs = nullptr;
    SDL_GPUShader* fs = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;       // clamp — the default
    SDL_GPUSampler* samplerRepeat = nullptr; // repeat — used when a quad's UVs leave [0,1]
    SDL_GPUTexture* whiteTex = nullptr;

    // Per-frame vertex ring (host transfer + device vertex buffer).
    struct Ring {
        SDL_GPUTransferBuffer* transfer = nullptr;
        SDL_GPUBuffer* vbo = nullptr;
    } rings[kRingFrames];
    uint32_t ringIdx = 0;

    std::unordered_map<const void*, SDL_GPUTexture*> texCache;

    // Collected this frame.
    bool active = false;
    int w = 0, h = 0;
    std::vector<HudVert> verts;
    std::vector<DrawRun> runs;
};

// The Zelda3D HUD immediate-mode 2D textured-quad renderer. Its state is clearly independent of the model
// renderer (separate pipeline/shaders/buffers), so it is a sibling member subsystem; folding it into
// Zelda3DRenderer would clash on `ensureResources`. Members keep the former global names (`g`,
// `g_idToTex`, `g_nextId`).
class Zelda3DHudRenderer {
  public:
    int Available();
    int Begin(int* outW, int* outH);
    // Upload + append what has been collected so far and keep collecting. Called at a display-list
    // flush marker so the ops land at that point of the interpreter's execution.
    void Flush();
    int Tex(const void* key, const void* rgba, int w, int h);
    void Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
              unsigned int tintRGBA, unsigned int envRGB, int mode);
    void End();

    SDL_GPUTexture* uploadTex(const void* rgba, int w, int h);
    bool ensureResources();

    HudSg g;
    // The Draw ABI takes an int id; resolve the texture at Draw time from a per-frame id->tex table.
    std::unordered_map<int, SDL_GPUTexture*> g_idToTex;
    int g_nextId = 1;
};

} // namespace Fast

#endif // ENABLE_SDL3GPU
