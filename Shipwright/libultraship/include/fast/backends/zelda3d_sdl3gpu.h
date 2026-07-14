// Zelda3D SDL3 GPU renderer — the OoT3D skinned-model + shadow/AO renderer and the PC HUD, folded
// into the SDL3 GPU backend (Fast::GfxRenderingAPISdl3Gpu) as MEMBER SUBSYSTEMS.
//
// Both classes used to be a pile of file-scope `g_*` globals + free functions in
// zelda3d_sdl3gpu.cpp / zelda3d_hud_sdl3gpu.cpp; this header pulls that state into two classes
// (Zelda3DRenderer + Zelda3DHudRenderer) owned by the backend. The member names are kept IDENTICAL to
// the former globals so the large function bodies move over unchanged (member access is implicit
// `this->`). The extern "C" C-ABI entry points (Zelda3D_Sg_* / Zelda3D_Hud_*) stay in their .cpp files
// as thin shims that forward to the live instance via Fast::g_activeSdl3GpuApi.
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

#include "fast/zelda3d_sg_ubo.h" // Zelda3DSg::SgUbo (per-draw uniform payload size)
#include "fast/backends/unified_shader.h" // Fast::Unified::Variant (render-unification, kanban #131)

namespace Fast {

// ---- model-renderer record types (verbatim from zelda3d_sdl3gpu.cpp) ----

struct SgGroup {
    uint32_t first = 0, count = 0;
    int texIndex = -1;
    int alphaTest = 0;
    float alphaRef = 0.0f;
    unsigned wrapS = 0x2901, wrapT = 0x2901;
    int blendEnable = 0;
    unsigned bSrcRGB = 0x0302, bDstRGB = 0x0303, bEqRGB = 0x8006;
    unsigned bSrcA = 1, bDstA = 0, bEqA = 0x8006;
    int depthWrite = 1;
    float polygonOffset = 0.0f;
    int cull = 0;
    int faceCull = 0;
    int meshId = -1;
    int materialIndex = -1;
    int vertexLighting = 0;
    int fogEnabled = 0; // CMB isFogEnabled (+0x02): PICA distance fog applies to this draw
    float combScaleRGB = 1.0f;
    float matAmbient[3] = { 1.0f, 1.0f, 1.0f };
    float matDiffuse[3] = { 1.0f, 1.0f, 1.0f };
    // PICA200 TEV constant palette + stage-0 selector (see Zelda3DGlGroup::matConstant /
    // combConstIdx). Populated by the model provider; overwritten by the per-actor override
    // channel (Step 2c EnHy body-color port) before submit.
    float matConstant[6][4] = {
        { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 },
        { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 },
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
    int tex1Index = -1;
    unsigned wrap1S = 0x2901, wrap1T = 0x2901;
    float uv1Scale[2] = { 1.0f, 1.0f };
    float uv1Trans[2] = { 0.0f, 0.0f };
    int coord1Mapping = 1; // coordinator-1 mapping method: 1=UV, 3=SphereEnvMap
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

// One captured depth draw (shadow caster / AO occluder), replayed inside an own offscreen pass.
struct DepthDraw {
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUTexture* tex; // for the alpha-test discard
    SDL_GPUSampler* samp;
    SDL_GPUBuffer* vbo;
    uint32_t first, count;
    std::array<uint8_t, sizeof(Zelda3DSg::SgUbo)> ubo;
};

// The OoT3D skinned-model + dynamic sun-shadow + SSAO renderer. Holds every former file-scope `g_*`
// of zelda3d_sdl3gpu.cpp as a data member with the same name. Methods borrow the backend (device,
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
                   int boneCnt, unsigned long long midMask, int sky, float uvOffU, float uvOffV,
                   const void* matTex, const void* matConst, int forceUnlit, const float* lightDirOv = nullptr,
                   const float* sphRotOv = nullptr);
    void EndPass();
    int BeginShadowPass();
    void ShadowCasterDraw(int modelId, const float* mp16, const float* mv16, const float* boneData, int boneCnt,
                          unsigned long long midMask);
    void ShadowCasterTris(const float* worldXYZ, size_t triCount, const float* lightVP16);
    void EndShadowPass();
    void SetShadow(int on, const float* lightVP16);
    int BeginDepthPrepass();
    void DepthPrepassDraw(int modelId, const float* mp16, const float* mv16, int invertY, float aspectAdj,
                          const float* boneData, int boneCnt, unsigned long long midMask, int sky);
    void EndDepthPrepass();
    void AoComposite();
    void ClearOverlayDepth(); // #146 item B — fullscreen depth-only reset, in-pass, no color write.

    // ---- internal helpers (former anonymous-namespace functions) ----
    SDL_GPUSampler* getSampler(unsigned wrapS, unsigned wrapT, bool noMip = false);
    SDL_GPUTexture* uploadTexture(int w, int h, const unsigned char* rgba);
    bool ensureResources();
    SDL_GPUGraphicsPipeline* getPipeline(const SgGroup& g, int frontCW);
    SgModel* ensureUploaded(int modelId);
    // Render-unification effort (kanban #131), Phase 2.
    SgModel* ensureUnifiedUploaded(int modelId);
    SDL_GPUGraphicsPipeline* getUnifiedPipeline(const SgGroup& g, int frontCW, int variant);
    void applyPendingEvict();
    SDL_GPUShader* makeShader(const char* glsl, EShLanguage stage, uint32_t numSamplers, uint32_t numUbo);
    SDL_GPUGraphicsPipeline* getDepthPipeline(bool doCull, int frontCW);
    bool ensureShadowAoResources();
    bool ensureOverlayDepthResources(); // #146 item B
    SDL_GPUTexture* makeDepthTarget(uint32_t w, uint32_t h, SDL_GPUTextureFormat fmt, SDL_GPUTextureUsageFlags usage);
    bool ensureShadowTargets(uint32_t dim);
    bool ensureAoTargets(uint32_t w, uint32_t h);
    void recordDepthGroups(std::vector<DepthDraw>& out, int modelId, const float* mp16, const float* mv16, int invertY,
                           float aspectAdj, const float* boneData, int boneCnt, unsigned long long midMask);

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
    SDL_GPUShader* g_uniVert[6] = {};
    SDL_GPUShader* g_uniFrag[6] = {};
    std::map<PipeKey, SDL_GPUGraphicsPipeline*> g_uniPipelines;

    // Deferred model eviction (mirror of the GL/VK path).
    int g_evictLo = 0, g_evictHi = 0;
    bool g_evictPending = false;

    // ---- M4 module state (shadow + SSAO) ----
    bool g_sgAoResReady = false;
    SDL_GPUShader* g_depthFrag = nullptr;
    SDL_GPUShader* g_aoCompVert = nullptr;
    SDL_GPUShader* g_aoCompFrag = nullptr;
    SDL_GPUGraphicsPipeline* g_aoCompPipe = nullptr;
    std::map<uint32_t, SDL_GPUGraphicsPipeline*> g_depthPipes; // key (doCull<<1)|frontCW
    SDL_GPUSampler* g_shadowSampler = nullptr;                 // nearest + clamp

    SDL_GPUTexture* g_shadowColor = nullptr; // R32F sun-shadow depth (sampled by the model frag)
    SDL_GPUTexture* g_shadowZ = nullptr;     // transient D32F z-test
    uint32_t g_shadowDim = 0;

    SDL_GPUTexture* g_aoColor = nullptr; // R32F camera depth (sampled by the SSAO composite)
    SDL_GPUTexture* g_aoZ = nullptr;     // transient D32F z-test
    uint32_t g_aoW = 0, g_aoH = 0;

    SDL_GPUBuffer* g_n64CasterBuf = nullptr; // per-frame N64 opaque caster triangle soup (shadow)
    uint32_t g_n64CasterCap = 0;

    // Per-RenderPass shadow state (set by SetShadow, consumed by DrawModel).
    bool g_sgShadowOn = false;
    float g_sgLightVP[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    // Per-pass phase flags + accumulators.
    bool g_sgShadowPassActive = false;
    bool g_sgAoPassActive = false;
    bool g_sgAoReady = false; // an AO depth pass produced occluders this frame -> composite is valid
    std::vector<DepthDraw> g_shadowDraws;
    std::vector<DepthDraw> g_aoDraws;
    SDL_GPUViewport g_aoVp{};
    SDL_Rect g_aoSc{};

    // #146 item B: fullscreen depth-only reset (Zelda3D_Overlay2D_Begin's depth scope). A minimal
    // pipeline: color_write_mask=0 (composited 3D scene color untouched), depth test ALWAYS +
    // depth write ON, fragment shader unconditionally writes gl_FragDepth=1.0 (far, matching the
    // 0=near/1=far convention the shadow/AO depth passes already use — see kDepthColorFormat's
    // dt.clear_depth=1.0f in replayDepthPass). Reuses kAoCompVert's fullscreen-triangle trick.
    bool g_overlayDepthResReady = false;
    SDL_GPUShader* g_overlayDepthFrag = nullptr;
    SDL_GPUGraphicsPipeline* g_overlayDepthPipe = nullptr;
};

// ---- HUD record types (verbatim from zelda3d_hud_sdl3gpu.cpp) ----

struct HudVert {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
};

constexpr uint32_t kMaxQuadsPerFrame = 2048;
constexpr uint32_t kVertsPerQuad = 6;
constexpr uint32_t kRingFrames = 3;

// One contiguous run of quads sharing a texture (a single SDL_DrawGPUPrimitives).
struct DrawRun {
    SDL_GPUTexture* tex;
    uint32_t firstVert, vertCount;
};

struct HudSg {
    SDL_GPUDevice* device = nullptr;
    bool ready = false;
    SDL_GPUShader* vs = nullptr;
    SDL_GPUShader* fs = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
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

// The PC HUD immediate-mode 2D textured-quad renderer. Its state is clearly independent of the model
// renderer (separate pipeline/shaders/buffers), so it is a sibling member subsystem; folding it into
// Zelda3DRenderer would clash on `ensureResources`. Members keep the former global names (`g`,
// `g_idToTex`, `g_nextId`).
class Zelda3DHudRenderer {
  public:
    int Available();
    int Begin(int* outW, int* outH);
    int Tex(const void* key, const void* rgba, int w, int h);
    void Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
              unsigned int tintRGBA);
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
