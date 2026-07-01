// Zelda3D dlist render harness — the SoH-side counterpart to the Azahar decode
// oracle. It drives libultraship's Fast3D interpreter (the REAL render path)
// over a generated CMB->F3DEX2 display list, WITHOUT booting the game or opening
// a window.
//
// Two modes:
//   (default) RECORDING  — a no-GPU GfxRenderingAPI that just logs every texture
//                          LOAD / UPLOAD / triangle draw. Answers "does LUS upload
//                          this model's texture?" in milliseconds. No GL context.
//   --gl                 — the REAL GfxRenderingAPIOGL rasterising into an offscreen
//                          FBO via an EGL *surfaceless* OpenGL context (llvmpipe,
//                          no X server, no Xvfb, fully deterministic software GL),
//                          then glGetTexImage -> PPM. This is the true both-renderers
//                          pixel A/B counterpart to the Azahar oracle render.
//
// Build:  cmake -S Shipwright -B <build> -DLUS_BUILD_DLIST_HARNESS=ON
//         cmake --build <build> --target zelda3d_dlist_harness
// Run:    zelda3d_dlist_harness                 (recording mode)
//         zelda3d_dlist_harness --gl [--out scratch/render/kibako_lus.ppm] [--size 640x480]
//
// GL mode needs the shader archive (shaders/opengl/default.shader.glsl lives in
// soh.o2r) mounted via the ResourceManager — pass --o2r <path> or set ZELDA3D_O2R;
// it otherwise probes a few standard build locations.
//
// It links the generated zelda3d_kibako_model.c directly (raw Vtx[]/Gfx[]/tex
// arrays, no ResourceManager needed for the geometry itself).

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>

#include <ship/Context.h>
#include <fast/interpreter.h>
#include <fast/debug/GfxDebugger.h>
#include <fast/backends/gfx_rendering_api.h>
#include <fast/backends/gfx_window_manager_api.h>
#include <fast/backends/gfx_opengl.h> // GfxRenderingAPIOGL + GL prototypes (SDL_opengl.h on Linux)
#include <libultraship/libultra/gbi.h>
#include <fast/zelda3d_gl.h> // Zelda3D direct-GL renderer (the --zelda3d path under test)

// Zelda3D runtime asset loader (pure C++; reads a model straight from the .3ds).
#include "asset/ctr_rom.h"
#include "asset/zar.h"
#include "asset/cmb.h"
#include "asset/csab.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

// Provider + scale exposed by zelda3d_model.cpp (compiled into the harness).
extern "C" void Zelda3D_EnsureModelProvider(void);
extern "C" void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame);

// The generated models under test (raw C arrays). Declared extern; linked in.
// Each is gated by a HAVE_* define from CMake (only the .c files that exist are
// compiled into the harness, since they are ROM-derived / gitignored).
extern "C" {
#ifdef HAVE_KIBAKO
extern Gfx zelda3d_kibako_model_dl[];
#endif
#ifdef HAVE_POT
extern Gfx zelda3d_pot_model_dl[];
#endif
#ifdef HAVE_GS
extern Gfx zelda3d_gs_model_dl[];
#endif
#ifdef HAVE_HINTSTONE
extern Gfx zelda3d_hintstone_model_dl[];
#endif
#ifdef HAVE_GELDWOMAN
extern Gfx zelda3d_geldwoman_model_dl[];
#endif
#ifdef HAVE_CHILDLINK
extern Gfx zelda3d_childlink_model_dl[];
#endif
}

// name -> model dlist, for --model selection. Only entries whose .c was linked.
static Gfx* SelectModel(const std::string& name) {
#ifdef HAVE_KIBAKO
    if (name == "kibako")
        return zelda3d_kibako_model_dl;
#endif
#ifdef HAVE_POT
    if (name == "pot")
        return zelda3d_pot_model_dl;
#endif
#ifdef HAVE_GS
    if (name == "gs")
        return zelda3d_gs_model_dl;
#endif
#ifdef HAVE_HINTSTONE
    if (name == "hintstone")
        return zelda3d_hintstone_model_dl;
#endif
#ifdef HAVE_GELDWOMAN
    if (name == "geldwoman")
        return zelda3d_geldwoman_model_dl;
#endif
#ifdef HAVE_CHILDLINK
    if (name == "childlink")
        return zelda3d_childlink_model_dl;
#endif
    return nullptr;
}

namespace Fast {
// Free function in interpreter.cpp (Fast namespace) that caches the instance the
// command handlers reach through mInstance.lock().
void GfxSetInstance(std::shared_ptr<Interpreter> gfx);
} // namespace Fast

using namespace Fast;

// ---------------------------------------------------------------------------
// Recording rendering API: no GPU, just logs the calls we care about.
// ---------------------------------------------------------------------------
class RecordingRenderingAPI : public GfxRenderingAPI {
  public:
    uint32_t mTexCounter = 1;
    int mFbCounter = 1;
    int mCurrentTile = -1;
    uint32_t mUploadCount = 0;
    uint32_t mTriDrawCount = 0;

    const char* GetName() override {
        return "recording";
    }
    int GetMaxTextureSize() override {
        return 16384;
    }
    GfxClipParameters GetClipParameters() override {
        return { false, false };
    }
    void UnloadShader(ShaderProgram*) override {
    }
    void LoadShader(ShaderProgram*) override {
    }
    void ClearShaderCache() override {
    }
    // Return a non-null dummy; the interpreter only stores it and asks us about
    // it via ShaderGetInfo (which we answer), it never dereferences it.
    ShaderProgram* CreateAndLoadNewShader(uint64_t, uint64_t) override {
        return reinterpret_cast<ShaderProgram*>(&mShaderDummy);
    }
    ShaderProgram* LookupShader(uint64_t, uint64_t) override {
        return nullptr;
    }
    void ShaderGetInfo(ShaderProgram*, uint8_t* numInputs, bool usedTextures[2]) override {
        *numInputs = 1;
        usedTextures[0] = true;
        usedTextures[1] = false;
    }
    uint32_t NewTexture() override {
        return mTexCounter++;
    }
    void SelectTexture(int tile, uint32_t textureId) override {
        mCurrentTile = tile;
    }
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override {
        mUploadCount++;
        printf("[HARNESS upload] #%u tile=%d %ux%u (%u px) first=%d,%d,%d,%d\n", mUploadCount, mCurrentTile, width,
               height, width * height, rgba32Buf[0], rgba32Buf[1], rgba32Buf[2], rgba32Buf[3]);
        fflush(stdout);
    }
    void SetSamplerParameters(int, bool, uint32_t, uint32_t) override {
    }
    void SetDepthTestAndMask(bool, bool) override {
    }
    void SetZmodeDecal(bool) override {
    }
    void SetViewport(int, int, int, int) override {
    }
    void SetScissor(int, int, int, int) override {
    }
    void SetUseAlpha(bool) override {
    }
    void DrawTriangles(float[], size_t, size_t buf_vbo_num_tris) override {
        mTriDrawCount += (uint32_t)buf_vbo_num_tris;
    }
    void Init() override {
    }
    void OnResize() override {
    }
    void StartFrame() override {
    }
    void EndFrame() override {
    }
    void FinishRender() override {
    }
    int CreateFramebuffer() override {
        return mFbCounter++;
    }
    void UpdateFramebufferParameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) override {
    }
    void StartDrawToFramebuffer(int, float) override {
    }
    void CopyFramebuffer(int, int, int, int, int, int, int, int, int, int) override {
    }
    void ClearFramebuffer(bool, bool) override {
    }
    void ReadFramebufferToCPU(int, uint32_t, uint32_t, uint16_t*) override {
    }
    void ResolveMSAAColorBuffer(int, int) override {
    }
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int, const std::set<std::pair<float, float>>&) override {
        return {};
    }
    void* GetFramebufferTextureId(int) override {
        return nullptr;
    }
    void SelectTextureFb(int) override {
    }
    void DeleteTexture(uint32_t) override {
    }
    void SetTextureFilter(FilteringMode) override {
    }
    FilteringMode GetTextureFilter() override {
        return FILTER_NONE;
    }
    void SetSrgbMode() override {
    }
    ImTextureID GetTextureById(int) override {
        return (ImTextureID)0;
    }
    void SetCurrentPrimDepth(float) override {
    }

  private:
    int mShaderDummy = 0;
};

// ---------------------------------------------------------------------------
// No-op window backend: the interpreter only needs Init + GetDimensions for the
// headless render path. (The EGL context is created by us, not by this backend.)
// ---------------------------------------------------------------------------
class StubWindowBackend : public GfxWindowBackend {
  public:
    uint32_t mW = 640, mH = 480;
    void Init(const char*, const char*, bool, uint32_t, uint32_t, int32_t, int32_t) override {
    }
    void Close() override {
    }
    void SetKeyboardCallbacks(bool (*)(int), bool (*)(int), void (*)()) override {
    }
    void SetMouseCallbacks(bool (*)(int), bool (*)(int)) override {
    }
    void SetFullscreenChangedCallback(void (*)(bool)) override {
    }
    void SetFullscreen(bool) override {
    }
    void GetActiveWindowRefreshRate(uint32_t* r) override {
        *r = 60;
    }
    void SetCursorVisibility(bool) override {
    }
    void SetMousePos(int32_t, int32_t) override {
    }
    void GetMousePos(int32_t* x, int32_t* y) override {
        *x = 0;
        *y = 0;
    }
    void GetMouseDelta(int32_t* x, int32_t* y) override {
        *x = 0;
        *y = 0;
    }
    void GetMouseWheel(float* x, float* y) override {
        *x = 0;
        *y = 0;
    }
    bool GetMouseState(uint32_t) override {
        return false;
    }
    void SetMouseCapture(bool) override {
    }
    bool IsMouseCaptured() override {
        return false;
    }
    void GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) override {
        *width = mW;
        *height = mH;
        *posX = 0;
        *posY = 0;
    }
    void SetDimensions(uint32_t, uint32_t, int32_t, int32_t) override {
    }
    Ship::WindowRect GetPrimaryMonitorRect() override {
        return { 0, 0, (int32_t)mW, (int32_t)mH };
    }
    void HandleEvents() override {
    }
    bool IsFrameReady() override {
        return true;
    }
    void SwapBuffersBegin() override {
    }
    void SwapBuffersEnd() override {
    }
    double GetTime() override {
        return 0.0;
    }
    int GetTargetFps() override {
        return 60;
    }
    void SetTargetFps(int) override {
    }
    void SetMaxFrameLatency(int) override {
    }
    const char* GetKeyName(int) override {
        return "";
    }
    bool CanDisableVsync() override {
        return true;
    }
    bool IsRunning() override {
        return true;
    }
    void Destroy() override {
    }
    bool IsFullscreen() override {
        return false;
    }
};

// ---------------------------------------------------------------------------
// EGL surfaceless OpenGL context (no window, no X server). Renders into FBOs only.
// ---------------------------------------------------------------------------
static EGLDisplay g_eglDpy = EGL_NO_DISPLAY;
static EGLContext g_eglCtx = EGL_NO_CONTEXT;

static bool EglInitSurfaceless() {
    g_eglDpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (g_eglDpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "[HARNESS] eglGetPlatformDisplay(SURFACELESS_MESA) failed\n");
        return false;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_eglDpy, &major, &minor)) {
        fprintf(stderr, "[HARNESS] eglInitialize failed (0x%x)\n", eglGetError());
        return false;
    }
    printf("[HARNESS] EGL %d.%d surfaceless; vendor=%s\n", major, minor, eglQueryString(g_eglDpy, EGL_VENDOR));

    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "[HARNESS] eglBindAPI(OPENGL) failed\n");
        return false;
    }

    // The Mesa surfaceless platform advertises ZERO EGLConfigs (there is no native
    // window system to describe). Create a config-less context instead, via
    // EGL_KHR_no_config_context — we only ever render into FBOs, where the config's
    // colour/depth format is irrelevant.
    const char* ext = eglQueryString(g_eglDpy, EGL_EXTENSIONS);
    if (!ext || !strstr(ext, "EGL_KHR_no_config_context") || !strstr(ext, "EGL_KHR_surfaceless_context")) {
        fprintf(stderr, "[HARNESS] EGL lacks no_config_context / surfaceless_context\n");
        return false;
    }
    EGLConfig cfg = EGL_NO_CONFIG_KHR;

    // Compatibility profile: the GLSL the OGL backend emits on desktop Linux is
    // #version 130 (varying / gl_FragColor / texture2D) and it draws without a VAO
    // — both require a compatibility (non-core) context. This mirrors the de-facto
    // context SoH gets on Linux.
    const EGLint ctxAttribs[] = { EGL_CONTEXT_MAJOR_VERSION,
                                  3,
                                  EGL_CONTEXT_MINOR_VERSION,
                                  3,
                                  EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                  EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
                                  EGL_NONE };
    g_eglCtx = eglCreateContext(g_eglDpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
    if (g_eglCtx == EGL_NO_CONTEXT) {
        fprintf(stderr, "[HARNESS] eglCreateContext failed (0x%x)\n", eglGetError());
        return false;
    }
    // Surfaceless: no draw/read surface; the backend only ever binds FBOs.
    if (!eglMakeCurrent(g_eglDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_eglCtx)) {
        fprintf(stderr, "[HARNESS] eglMakeCurrent(surfaceless) failed (0x%x)\n", eglGetError());
        return false;
    }
    printf("[HARNESS] GL_RENDERER=%s  GL_VERSION=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return true;
}

static void EglShutdown() {
    if (g_eglDpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_eglDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_eglCtx != EGL_NO_CONTEXT)
            eglDestroyContext(g_eglDpy, g_eglCtx);
        eglTerminate(g_eglDpy);
    }
}

// Write an RGB8 buffer (GL bottom-left origin) to a top-to-bottom PPM (P6).
static bool WritePpmFlipped(const std::string& path, const uint8_t* rgb, uint32_t w, uint32_t h) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "[HARNESS] cannot open %s for write\n", path.c_str());
        return false;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (int y = (int)h - 1; y >= 0; y--) // flip: GL row 0 is the bottom
        fwrite(rgb + (size_t)y * w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    return true;
}

// Locate the shader archive (soh.o2r) for GL mode.
static std::string FindO2r(const char* explicitPath) {
    if (explicitPath && *explicitPath && std::filesystem::exists(explicitPath))
        return explicitPath;
    if (const char* env = getenv("ZELDA3D_O2R"); env && *env && std::filesystem::exists(env))
        return env;
    const char* candidates[] = {
        "Shipwright/build-cmake/soh/soh.o2r", "build-cmake/soh/soh.o2r", "../../soh/soh.o2r", "soh.o2r",
    };
    for (const char* c : candidates)
        if (std::filesystem::exists(c))
            return c;
    return {};
}

// ---------------------------------------------------------------------------
// Build the command stream shared by both modes: relocate the model's texture
// pointers high, then a prologue that establishes a viewport / scissor / PRIM
// colour / projection+modelview so the crate actually rasterises on-screen.
// ---------------------------------------------------------------------------
struct BuiltDlist {
    std::vector<Gfx> model;   // relocated copy of the model dlist
    std::vector<Gfx> dl;      // prologue + call(model) + end
    Vp vp{};                  // referenced by gsSPViewport (must outlive Run)
    Mtx projMtx{}, mvMtx{};   // mtx_replacement keys (addresses matter, contents unused)
    std::unordered_map<Mtx*, MtxF> mtxReplacements;
    std::string view = "xy";  // screen plane: xy (front), zy (side), xz (top)
};

static void BuildDlist(BuiltDlist& b, Gfx* modelDl) {
    // Copy the model dlist into a mutable buffer (up to and including G_ENDDL).
    for (int i = 0;; i++) {
        b.model.push_back(modelDl[i]);
        if ((uint8_t)(modelDl[i].words.w0 >> 24) == 0xDF) // G_ENDDL
            break;
    }

    // Relocate every G_SETTIMG (0xFD) texture pointer into a high mmap'd buffer.
    // HARNESS FIXUP, not a workaround for any bug under test: in the real soh.elf
    // (a large PIE) these static textures live at high addresses, so the
    // interpreter's "addr <= 0x0FFFFFFF => unresolved N64 segment, skip" guard in
    // gfx_set_timg_handler_rdp passes. In this small standalone binary the same
    // static data sits at ~6 MB and is falsely rejected, so we copy it high to
    // faithfully exercise the in-game path.
    for (size_t i = 0; i + 1 < b.model.size(); i++) {
        if ((uint8_t)(b.model[i].words.w0 >> 24) != 0xFD) // G_SETTIMG
            continue;
        uint32_t siz = (b.model[i].words.w0 >> 19) & 0x3; // 0:4b 1:8b 2:16b 3:32b
        // Texel count comes from the texture's LoadBlock, which cmb_to_c emits as
        // EITHER the wide form (op 0x47, lrs in full w1) for >4096 texels OR the
        // plain form (op 0xF3, lrs in w1 bits[23:12]) for <=4096. Search forward
        // to this texture's load, stopping at the next G_SETTIMG.
        uint32_t texels = 0;
        for (size_t j = i + 1; j < b.model.size(); j++) {
            uint8_t op = (uint8_t)(b.model[j].words.w0 >> 24);
            if (op == 0xFD) // next G_SETTIMG — this texture had no load (shouldn't happen)
                break;
            if (op == 0x47) { // G_LOADBLOCK_WIDE
                texels = (uint32_t)(b.model[j].words.w1 & 0xFFFFFFFF) + 1;
                break;
            }
            if (op == 0xF3) { // G_LOADBLOCK (plain): lrs = w1[23:12]
                texels = ((uint32_t)(b.model[j].words.w1 >> 12) & 0xFFF) + 1;
                break;
            }
        }
        size_t bytes = (siz == 0) ? (texels + 1) / 2 : (siz == 1) ? texels : (siz == 2) ? texels * 2 : texels * 4;
        const uint8_t* orig = (const uint8_t*)(uintptr_t)b.model[i].words.w1;
        void* hi = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memcpy(hi, orig, bytes);
        printf("[HARNESS] relocated G_SETTIMG[%zu] tex %p -> %p (%zu bytes, siz=%u)\n", i, (void*)orig, hi, bytes, siz);
        b.model[i].words.w1 = (uintptr_t)hi;
    }

    // Auto-fit: scan the model's G_VTX commands for the vertex bbox, then build a
    // modelview that centres + uniformly scales the model into NDC. This avoids
    // per-model magic constants and works for any model (crate/pot/gs/...).
    // F3DEX2 vertex load: opcode G_VTX (0x01 under F3DEX_GBI_2; verified by dumping
    // the generated dlist — index 12 is op 0x01, n=30 for the 30-vertex crate).
    // n = (w0>>12)&0xFF vertices at w1; each Vtx is 16 bytes, int16 ob[3] at offset 0.
    float minp[3] = { 1e30f, 1e30f, 1e30f }, maxp[3] = { -1e30f, -1e30f, -1e30f };
    size_t nverts = 0;
    for (const Gfx& g : b.model) {
        if ((uint8_t)(g.words.w0 >> 24) != G_VTX) // 0x01
            continue;
        uint32_t n = (g.words.w0 >> 12) & 0xFF;
        const uint8_t* vp = (const uint8_t*)(uintptr_t)g.words.w1;
        if (!vp)
            continue;
        for (uint32_t k = 0; k < n; k++) {
            const int16_t* ob = (const int16_t*)(vp + (size_t)k * 16);
            for (int c = 0; c < 3; c++) {
                float v = (float)ob[c];
                minp[c] = std::min(minp[c], v);
                maxp[c] = std::max(maxp[c], v);
            }
            nverts++;
        }
    }
    float ctr[3] = { 0, 0, 0 }, ext[3] = { 1, 1, 1 };
    if (nverts)
        for (int c = 0; c < 3; c++) {
            ctr[c] = (minp[c] + maxp[c]) * 0.5f;
            ext[c] = std::max((maxp[c] - minp[c]) * 0.5f, 1.0f);
        }
    printf("[HARNESS] model bbox: x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f] (%zu verts)\n", minp[0], maxp[0], minp[1],
           maxp[1], minp[2], maxp[2], nverts);

    // Choose which model axis maps to screen-horizontal (aH), screen-vertical (aV)
    // and depth (aD), per the --view plane. Default "xy" (look down -z) suits props
    // authored +Y up; characters whose rest space faces along X want "zy" (look
    // down +x) to see the front. The 3rd axis becomes depth.
    int aH = 0, aV = 1, aD = 2; // xy
    if (b.view == "zy") { aH = 2; aV = 1; aD = 0; }
    else if (b.view == "xz") { aH = 0; aV = 2; aD = 1; }

    // Identity projection; modelview centres the model and scales it to ~80% of NDC.
    // Convention (GfxSpVertex): clip_j = sum_k ob[k]*MP[k][j] + MP[3][j], so the
    // translation lives in row [3][*]. Uniform H/V scale preserves aspect (the
    // 320x240 viewport maps to the 4:3 render target). The depth axis maps into
    // ~[0.1,0.9] so nothing clips against the near/far planes.
    const float fit = 0.8f / std::max(ext[aH], ext[aV]);
    const float fitZ = 0.4f / ext[aD];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            b.mtxReplacements[&b.projMtx].mf[i][j] = (i == j) ? 1.0f : 0.0f;
    MtxF& mv = b.mtxReplacements[&b.mvMtx];
    memset(mv.mf, 0, sizeof(mv.mf));
    mv.mf[aH][0] = fit;
    mv.mf[aV][1] = fit;
    mv.mf[aD][2] = fitZ;
    mv.mf[3][0] = -ctr[aH] * fit;
    mv.mf[3][1] = -ctr[aV] * fit;
    mv.mf[3][2] = 0.5f - ctr[aD] * fitZ;
    mv.mf[3][3] = 1.0f;

    // Standard full-screen 320x240 native viewport (scale = half-dim*4).
    b.vp.vp.vscale[0] = (SCREEN_WIDTH / 2) * 4;
    b.vp.vp.vscale[1] = (SCREEN_HEIGHT / 2) * 4;
    b.vp.vp.vscale[2] = G_MAXZ;
    b.vp.vp.vscale[3] = 0;
    b.vp.vp.vtrans[0] = (SCREEN_WIDTH / 2) * 4;
    b.vp.vp.vtrans[1] = (SCREEN_HEIGHT / 2) * 4;
    b.vp.vp.vtrans[2] = 0;
    b.vp.vp.vtrans[3] = 0;

    // Prologue: viewport + scissor + PRIM(white) + matrices, then call the model.
    // PRIM matters: the crate combiner is MODULATE x PRIM, so PRIM=0 => black.
    // (The gs* macros expand to brace-aggregate initializers, so assign to named
    // locals — a C-style (Gfx){...} compound-literal cast is not valid C++.)
    Gfx cViewport = gsSPViewport(&b.vp);
    Gfx cScissor = gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    Gfx cProj = gsSPMatrix(&b.projMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    Gfx cMv = gsSPMatrix(&b.mvMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Gfx cPrim = gsDPSetPrimColor(0, 0, 255, 255, 255, 255);
    Gfx cCall = gsSPDisplayList(b.model.data());
    Gfx cEnd = gsSPEndDisplayList();
    b.dl.push_back(cViewport);
    b.dl.push_back(cScissor);
    b.dl.push_back(cProj);
    b.dl.push_back(cMv);
    b.dl.push_back(cPrim);
    b.dl.push_back(cCall);
    b.dl.push_back(cEnd);
}

// --- Zelda3D direct-GL path harness ---------------------------------------------
// Reproduces the IN-GAME path headlessly: emit the OTR_G_ZELDA3D_DRAW opcode (which
// runs Zelda3D_GL_Draw) into a dlist, followed by a normal Fast3D triangle. If our GL
// draw corrupts the interpreter's GL state, that trailing triangle's DrawTriangles
// crashes here — the exact in-game symptom, but in ~1s with no game boot.
static Vtx g_canary[3];

static bool BuildZelda3DDlist(BuiltDlist& b, const std::string& zarPath, int modelId, float rx, float ry, float rz) {
    // Load the CMB (same loader the game uses) to get the model bbox for an
    // auto-fit modelview, and to confirm the asset path is good.
    const char* rom = getenv("ZELDA3D_3DS_ROM");
    if (!rom || !*rom) { fprintf(stderr, "[HARNESS] ZELDA3D_3DS_ROM not set\n"); return false; }
    Zelda3D::CtrRom r(rom);
    if (!r.ok()) { fprintf(stderr, "[HARNESS] CtrRom: %s\n", r.error().c_str()); return false; }
    auto zb = r.read(zarPath);
    if (zb.empty()) { fprintf(stderr, "[HARNESS] zar not found: %s\n", zarPath.c_str()); return false; }
    Zelda3D::Zar zar(std::move(zb));
    const Zelda3D::ZarFile* cf = zar.ok() ? zar.firstWithSuffix(".cmb") : nullptr;
    if (!cf) { fprintf(stderr, "[HARNESS] no .cmb in %s\n", zarPath.c_str()); return false; }
    Zelda3D::Cmb cmb(zar.read(*cf));
    if (!cmb.ok()) { fprintf(stderr, "[HARNESS] Cmb: %s\n", cmb.error().c_str()); return false; }
    // Auto-fit bbox from the SKINNED groups when an anim is requested (same env the
    // provider reads), so a deformed pose isn't clipped by a bind-pose-sized fit.
    std::vector<Zelda3D::CmbDrawGroup> bboxGroups;
    const char* animEnv = getenv("ZELDA3D_ANIM");
    if (animEnv && *animEnv) {
        std::string an(animEnv);
        std::string full = (an.rfind("Anim/", 0) == 0) ? an : ("Anim/" + an + ".csab");
        const Zelda3D::ZarFile* af = nullptr;
        for (const auto& f : zar.files()) if (f.name == full) { af = &f; break; }
        if (af) {
            Zelda3D::Csab anim(zar.read(*af));
            float frame = getenv("ZELDA3D_FRAME") ? (float)atof(getenv("ZELDA3D_FRAME")) : 0.0f;
            if (anim.ok()) {
                std::vector<std::array<float, 16>> sm;
                anim.skinMatrices(cmb, frame, sm);
                bboxGroups = cmb.buildDrawGroupsSkinned(sm.data(), sm.size());
            }
        }
    }
    if (bboxGroups.empty()) bboxGroups = cmb.buildDrawGroups();
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const auto& g : bboxGroups)
        for (const auto& v : g.verts)
            for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], v.pos[k]); hi[k] = std::max(hi[k], v.pos[k]); }
    float ctr[3], ext[3];
    for (int k = 0; k < 3; k++) { ctr[k] = (lo[k] + hi[k]) * 0.5f; ext[k] = std::max((hi[k] - lo[k]) * 0.5f, 1.0f); }
    printf("[HARNESS] zelda3d bbox x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);

    // Identity projection; modelview = Scale(fit) * R(rx,ry,rz) about the model
    // centre, into ~80% NDC. Convention clip_j = sum_k pos_k*MV[k][j] + MV[3][j], so
    // MV[k][j] = S_j * R_{jk} and MV[3][j] = bias_j - sum_k MV[k][j]*ctr_k.
    const float fit = 0.8f / std::max(ext[0], ext[1]);
    const float fitZ = 0.4f / ext[2];
    const float S[3] = { fit, fit, fitZ };
    auto rad = [](float d) { return d * 3.14159265358979f / 180.0f; };
    float cx = cosf(rad(rx)), sx = sinf(rad(rx)), cyr = cosf(rad(ry)), syr = sinf(rad(ry)), cz = cosf(rad(rz)),
          sz = sinf(rad(rz));
    float Rx[3][3] = { { 1, 0, 0 }, { 0, cx, -sx }, { 0, sx, cx } };
    float Ry[3][3] = { { cyr, 0, syr }, { 0, 1, 0 }, { -syr, 0, cyr } };
    float Rz[3][3] = { { cz, -sz, 0 }, { sz, cz, 0 }, { 0, 0, 1 } };
    auto mul3 = [](const float A[3][3], const float B[3][3], float O[3][3]) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) { O[i][j] = 0; for (int k = 0; k < 3; k++) O[i][j] += A[i][k] * B[k][j]; }
    };
    float Rzy[3][3], R[3][3];
    mul3(Rz, Ry, Rzy);
    mul3(Rzy, Rx, R); // R = Rz*Ry*Rx
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) b.mtxReplacements[&b.projMtx].mf[i][j] = (i == j) ? 1.0f : 0.0f;
    MtxF& mv = b.mtxReplacements[&b.mvMtx];
    memset(mv.mf, 0, sizeof(mv.mf));
    float bias[3] = { 0.0f, 0.0f, 0.5f };
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < 3; k++) mv.mf[k][j] = S[j] * R[j][k];
        float t = bias[j];
        for (int k = 0; k < 3; k++) t -= mv.mf[k][j] * ctr[k];
        mv.mf[3][j] = t;
    }
    mv.mf[3][3] = 1.0f;

    b.vp.vp.vscale[0] = (SCREEN_WIDTH / 2) * 4;  b.vp.vp.vscale[1] = (SCREEN_HEIGHT / 2) * 4;
    b.vp.vp.vscale[2] = G_MAXZ;                  b.vp.vp.vscale[3] = 0;
    b.vp.vp.vtrans[0] = (SCREEN_WIDTH / 2) * 4;  b.vp.vp.vtrans[1] = (SCREEN_HEIGHT / 2) * 4;
    b.vp.vp.vtrans[2] = 0;                       b.vp.vp.vtrans[3] = 0;

    // A TINY shaded canary triangle tucked in the bbox corner (in-frame but out of
    // the way), to exercise the interpreter's vtx/tri/DrawTriangles path: a PRE one
    // makes the interpreter apply its framebuffer/viewport/shader to GL (it does so
    // lazily on first draw, mimicking the in-game scene drawing before the actor),
    // and a POST one catches GL-state corruption from our draw.
    int16_t d = (int16_t)(ext[0] * 0.05f);
    int16_t bx = (int16_t)lo[0], by = (int16_t)lo[1], bz = (int16_t)ctr[2];
    Vtx mk[3] = {};
    mk[0].v.ob[0] = bx;       mk[0].v.ob[1] = by;     mk[0].v.ob[2] = bz;
    mk[1].v.ob[0] = bx + d;   mk[1].v.ob[1] = by;     mk[1].v.ob[2] = bz;
    mk[2].v.ob[0] = bx;       mk[2].v.ob[1] = by + d; mk[2].v.ob[2] = bz;
    for (int i = 0; i < 3; i++) { mk[i].v.cn[0] = 255; mk[i].v.cn[1] = 0; mk[i].v.cn[2] = 0; mk[i].v.cn[3] = 255; }
    memcpy(g_canary, mk, sizeof(mk));

    Zelda3D_EnsureModelProvider();
    // GPU skinning: set the model's animated pose (uBones) for this frame. The VBO
    // itself is model-space (bind pose); the shader applies these matrices. Same
    // frame as the bbox fit above, so the rendered pose matches the auto-fit.
    if (animEnv && *animEnv) {
        float frame = getenv("ZELDA3D_FRAME") ? (float)atof(getenv("ZELDA3D_FRAME")) : 0.0f;
        Zelda3D_UpdateAnim(modelId, animEnv, frame);
    }

    Gfx cViewport = gsSPViewport(&b.vp);
    Gfx cScissor = gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    Gfx cProj = gsSPMatrix(&b.projMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    Gfx cMv = gsSPMatrix(&b.mvMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Gfx cEnd = gsSPEndDisplayList();
    b.dl.push_back(cViewport);
    b.dl.push_back(cScissor);
    b.dl.push_back(cProj);
    b.dl.push_back(cMv);
    // PRE-canary: a Fast3D draw to make the interpreter apply its framebuffer +
    // viewport + scissor + shader to GL (it does so lazily on first draw), faithfully
    // mimicking in-game where the scene renders before the actor's Zelda3D draw.
    { Gfx g = gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE); b.dl.push_back(g); }
    { Gfx g = gsSPVertex(g_canary, 3, 0); b.dl.push_back(g); }
    { Gfx g = gsSP1Triangle(0, 1, 2, 0); b.dl.push_back(g); }
    // The Zelda3D direct-GL model draw under test.
    { Gfx g; gSPZelda3DDraw(&g, modelId, 255, 255, 255); b.dl.push_back(g); }
    // POST-canary: another Fast3D triangle, to catch GL-state corruption from our draw.
    { Gfx g = gsSPVertex(g_canary, 3, 0); b.dl.push_back(g); }
    { Gfx g = gsSP1Triangle(0, 1, 2, 0); b.dl.push_back(g); }
    b.dl.push_back(cEnd);
    return true;
}

int main(int argc, char** argv) {
    bool glMode = false;
    std::string outPath; // default derived from model below
    std::string o2rArg;
    std::string modelName = "kibako";
    std::string viewPlane = "xy";
    bool zelda3dMode = false;
    std::string zarPath = "/actor/zelda_ge1.zar";
    float rx = 0, ry = 0, rz = 0; // --zelda3d model orientation (degrees)
    uint32_t W = 640, H = 480;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--gl")
            glMode = true;
        else if (a == "--zelda3d") {
            zelda3dMode = true;
            glMode = true; // the Zelda3D direct-GL path requires the real GL backend
        } else if (a == "--zar" && i + 1 < argc)
            zarPath = argv[++i];
        else if (a == "--rotx" && i + 1 < argc)
            rx = (float)atof(argv[++i]);
        else if (a == "--roty" && i + 1 < argc)
            ry = (float)atof(argv[++i]);
        else if (a == "--rotz" && i + 1 < argc)
            rz = (float)atof(argv[++i]);
        else if (a == "--out" && i + 1 < argc)
            outPath = argv[++i];
        else if (a == "--o2r" && i + 1 < argc)
            o2rArg = argv[++i];
        else if (a == "--model" && i + 1 < argc)
            modelName = argv[++i];
        else if (a == "--view" && i + 1 < argc)
            viewPlane = argv[++i];
        else if (a == "--size" && i + 1 < argc) {
            unsigned w, h;
            if (sscanf(argv[++i], "%ux%u", &w, &h) == 2) {
                W = w;
                H = h;
            }
        }
    }
    if (outPath.empty())
        outPath = zelda3dMode ? "scratch/render/zelda3d_gl.ppm" : "scratch/render/" + modelName + "_lus.ppm";

    Gfx* modelDl = nullptr;
    if (!zelda3dMode) {
        modelDl = SelectModel(modelName);
        if (!modelDl) {
            fprintf(stderr, "[HARNESS] unknown/unbuilt model '%s' (have: kibako/pot/gs if their .c was generated)\n",
                    modelName.c_str());
            return 2;
        }
    }

    auto* ctx = Ship::Context::CreateUninitializedInstance("zelda3d_harness", "zelda3d_harness", "");
    ctx->InitLogging();
    ctx->InitConfiguration();
    ctx->InitConsoleVariables();

    std::string o2r;
    if (glMode) {
        o2r = FindO2r(o2rArg.c_str());
        if (o2r.empty()) {
            fprintf(stderr, "[HARNESS] GL mode needs the shader archive (soh.o2r). "
                            "Pass --o2r <path> or set ZELDA3D_O2R.\n");
            return 2;
        }
        // ResourceManager mounts soh.o2r so the OGL backend can load
        // shaders/opengl/default.shader.glsl (compiled per-combiner at draw time).
        ctx->InitResourceManager({ o2r });
        printf("[HARNESS] mounted shader archive: %s\n", o2r.c_str());
        if (!EglInitSurfaceless())
            return 3;
    }

    auto rec = std::make_unique<RecordingRenderingAPI>();
    auto ogl = glMode ? std::make_unique<GfxRenderingAPIOGL>() : nullptr;
    GfxRenderingAPI* rapi = glMode ? (GfxRenderingAPI*)ogl.get() : (GfxRenderingAPI*)rec.get();

    auto wapi = std::make_unique<StubWindowBackend>();
    wapi->mW = W;
    wapi->mH = H;

    auto gfx = std::make_shared<Interpreter>();
    GfxSetInstance(gfx);
    gfx->SetGfxDebugger(std::make_shared<GfxDebugger>());
    gfx->Init(wapi.get(), rapi, "zelda3d_harness", false, W, H, 0, 0);

    BuiltDlist b;
    b.view = viewPlane;
    if (zelda3dMode) {
        if (!BuildZelda3DDlist(b, zarPath, 0, rx, ry, rz))
            return 2;
    } else {
        BuildDlist(b, modelDl);
    }

    printf("[HARNESS] running '%s' dlist through LUS interpreter (%s, %ux%u)...\n",
           zelda3dMode ? zarPath.c_str() : modelName.c_str(), glMode ? "GL" : "recording", W, H);
    fflush(stdout);
    gfx->StartFrame();
    gfx->Run(b.dl.data(), b.mtxReplacements);

    if (glMode) {
        glFinish();
        // mGameFb is deterministically the FIRST framebuffer the interpreter creates
        // in Init() (rapi->Init() reserves index 0 for the screen; Init() then calls
        // CreateFramebuffer() twice -> mGameFb=1, mGameFbMsaaResolved=2). With the
        // default MSAA=1, fb1's colour attachment is a plain RGB8 texture, which is
        // what Run() leaves the rendered crate in (fb 0 is re-cleared at frame end).
        const int kGameFb = 1;
        GLuint tex = (GLuint)(uintptr_t)rapi->GetFramebufferTextureId(kGameFb);
        std::vector<uint8_t> rgb((size_t)W * H * 3);
        glBindTexture(GL_TEXTURE_2D, tex);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        // Quick non-black pixel count so the log carries a quantitative signal.
        size_t nonBlack = 0;
        for (size_t i = 0; i < rgb.size(); i += 3)
            if (rgb[i] | rgb[i + 1] | rgb[i + 2])
                nonBlack++;
        if (WritePpmFlipped(outPath, rgb.data(), W, H))
            printf("[HARNESS] wrote %s (%ux%u, %zu/%u non-black px)\n", outPath.c_str(), W, H, nonBlack, W * H);
        EglShutdown();
    } else {
        printf("[HARNESS] done: %u UploadTexture call(s), %u triangle(s) drawn.\n", rec->mUploadCount,
               rec->mTriDrawCount);
    }
    fflush(stdout);
    return 0;
}
