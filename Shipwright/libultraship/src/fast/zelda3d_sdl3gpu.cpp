// Stable C ABI adapter for the Zelda3D SDL3GPU renderer subsystem.
#ifdef ENABLE_SDL3GPU

#include "fast/zelda3d_sdl3gpu.h"
#include "zelda3d_sdl3gpu_internal.h"

#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/zelda3d_sdl3gpu.h"

// Public C-ABI (Zelda3D_Sg_* / Zelda3D_GeomScanDump). These keep their exact names + signatures and
// adapt callers to the live Zelda3DRenderer. When no SDL3 GPU backend is active the renderer is null
// and the adapter no-ops (int-returning calls return 0), matching the original contract.
// ----------------------------------------------------------------------------------------------------
namespace {
inline Fast::Zelda3DRenderer* sgRenderer() {
    return Fast::g_activeSdl3GpuApi ? Fast::g_activeSdl3GpuApi->Soh3d() : nullptr;
}
} // namespace
// Local-space AABB of one draw group. Scene room CMBs store WORLD-space vertices under an identity
// model matrix, so for room geometry this IS the world AABB -- the case that matters, since actors
// can already be framed with `acam`. 0 if the model is not uploaded or the group has no vertices.
extern "C" int Zelda3D_Sg_GroupBounds(int modelId, int groupIdx, float* outMin, float* outMax) {
    Fast::Zelda3DRenderer* r = sgRenderer();
    if (r == nullptr)
        return 0;
    return r->groupBounds(modelId, groupIdx, outMin, outMax) ? 1 : 0;
}

extern "C" int Zelda3D_Sg_Active(void) {
    return Fast::g_activeSdl3GpuApi != nullptr ? 1 : 0;
}

extern "C" void Zelda3D_Sg_SetProvider(Zelda3DModelProvider fn) {
    Fast::Zelda3DSdl3GpuResources::SetModelProvider(fn);
}

extern "C" int Zelda3D_GeomScanDump(int* modelIds, float* mins, float* maxs, int maxN) {
    if (auto* r = sgRenderer())
        return r->GeomScanDump(modelIds, mins, maxs, maxN);
    return 0;
}
extern "C" void Zelda3D_Sg_RequestEvictRange(int lo, int hi) {
    if (auto* r = sgRenderer())
        r->RequestEvictRange(lo, hi);
}
extern "C" void Zelda3D_Sg_BeginPass(void) {
    if (auto* r = sgRenderer())
        r->BeginPass();
}
extern "C" void Zelda3D_Sg_DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                     unsigned char r8, unsigned char g8, unsigned char b8, unsigned char a8,
                                     float aspectAdj, const float* boneData, int boneCnt, unsigned long long midMask,
                                     int sky, float uvOffU, float uvOffV, const void* matTex, const void* matConst,
                                     const void* matUv, int forceUnlit, const float* lightDirOv,
                                     const float* sphereNormalOv) {
    Fast::Zelda3DSdl3GpuPass::RecordSubmissionProbe(modelId, mp16, lit, sky, r8, g8, b8, a8, boneCnt);
    if (auto* r = sgRenderer())
        r->DrawModel(modelId, mp16, mv16, lit, invertY, r8, g8, b8, a8, aspectAdj, boneData, boneCnt, midMask, sky,
                     uvOffU, uvOffV, matTex, matConst, matUv, forceUnlit, lightDirOv, sphereNormalOv);
}
extern "C" void Zelda3D_Sg_EndPass(void) {
    if (auto* r = sgRenderer())
        r->EndPass();
}
extern "C" void Zelda3D_Sg_ClearOverlayDepth(void) {
    if (auto* r = sgRenderer())
        r->ClearOverlayDepth();
}

#endif // ENABLE_SDL3GPU
