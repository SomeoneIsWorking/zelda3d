// Zelda3D SDL3GPU pass recording, draw preparation, and diagnostics.
#ifdef ENABLE_SDL3GPU

#include "zelda3d_sdl3gpu_internal.h"

#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/zelda3d_sdl3gpu.h"
#include "fast/zelda3d_material_overrides.h"
#include "fast/zelda3d_instrumentation.h"
#include "fast/zelda3d_sg_ubo.h"
#include "fast/unified_material.h"
#include "fast/unified_ubo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

using Fast::g_activeSdl3GpuApi;
using Fast::GeomRec;
using Fast::GfxRenderingAPISdl3Gpu;
using Fast::SgGroup;
using Fast::SgModel;
using Zelda3DSg::SgUbo;

constexpr uint32_t kSgCommonBytes = Zelda3DSg::kCommonBytes;
constexpr uint32_t kSgBonesBytes = Zelda3DSg::kBonesBytes;

// Title wordmark sheen (title_logo_actor.md §6.3/§6.6): the light-env slot's AMBIENT color is
// (0.18,0.18,0.18,1) and its DIFFUSE color is WHITE (1,1,1,1) — byte-exact from the literal pool
// at 0x004d9924 in code.bin AND read back live from CmbVShader's c81/c82 uniforms at the wordmark
// draw (oracle vsuni log, 2026-07-10: dif0=(1,1,1,1), amb0=(0.18,0.18,0.18,1), one enabled
// light). The per-vertex expression is therefore shade = 0.18 + max(0, dot(N, -L(t))) — the
// DIRECTIONAL term is the dominant one (not a small additive bonus on an ambient of 1; the old
// 0.1834 "diffuse" constant here had the two slot colors swapped). Only the direction L(t) is
// animated per-frame by the caller (Zelda3D_GL_SetLightDirOverride).
constexpr float kWordmarkLightAmbient = 0.18f;

namespace {

void TraceSkinnedClipBounds(int modelId, const float* modelProjection, const float* boneData, int boneCount,
                            unsigned long long visibleMeshMask, float aspectAdjustment) {
    if (modelId != gZelda3dTraceModelId || boneData == nullptr || boneCount <= 0) {
        return;
    }

    const Zelda3DGlGroup* groups = nullptr;
    int groupCount = 0;
    if (!Fast::Zelda3DSdl3GpuResources::ModelSource(modelId, &groups, &groupCount)) {
        return;
    }

    std::array<float, 3> ndcMin = { INFINITY, INFINITY, INFINITY };
    std::array<float, 3> ndcMax = { -INFINITY, -INFINITY, -INFINITY };
    int vertexCount = 0;
    int frontCount = 0;
    int insideCount = 0;
    int invalidBoneCount = 0;
    float minWeightSum = INFINITY;
    float maxWeightSum = -INFINITY;
    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        const Zelda3DGlGroup& group = groups[groupIndex];
        if (group.cull || (group.meshId >= 0 && group.meshId < 64 && ((visibleMeshMask >> group.meshId) & 1ull) == 0)) {
            continue;
        }
        for (int vertexIndex = 0; vertexIndex < group.vertCount; ++vertexIndex) {
            const Zelda3DGlVtx& vertex = group.verts[vertexIndex];
            std::array<float, 4> skinned = { 0.0f, 0.0f, 0.0f, 0.0f };
            float weightSum = 0.0f;
            bool valid = true;
            for (int influence = 0; influence < 4; ++influence) {
                const int boneIndex = static_cast<int>(vertex.boneIds[influence]);
                const float weight = vertex.weights[influence];
                weightSum += weight;
                if (weight == 0.0f) {
                    continue;
                }
                if (boneIndex < 0 || boneIndex >= boneCount) {
                    valid = false;
                    continue;
                }
                const float* bone = boneData + boneIndex * 16;
                for (int row = 0; row < 4; ++row) {
                    skinned[row] += weight * (bone[row * 4] * vertex.pos[0] + bone[row * 4 + 1] * vertex.pos[1] +
                                              bone[row * 4 + 2] * vertex.pos[2] + bone[row * 4 + 3]);
                }
            }
            ++vertexCount;
            minWeightSum = std::min(minWeightSum, weightSum);
            maxWeightSum = std::max(maxWeightSum, weightSum);
            if (!valid) {
                ++invalidBoneCount;
                continue;
            }

            std::array<float, 4> clip{};
            for (int row = 0; row < 4; ++row) {
                const float aspect = row == 0 ? aspectAdjustment : 1.0f;
                clip[row] = aspect * (modelProjection[row] * skinned[0] + modelProjection[4 + row] * skinned[1] +
                                      modelProjection[8 + row] * skinned[2] + modelProjection[12 + row] * skinned[3]);
            }
            if (!(clip[3] > 0.0f) || !std::isfinite(clip[3])) {
                continue;
            }
            ++frontCount;
            const std::array<float, 3> ndc = { clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3] };
            if (!std::isfinite(ndc[0]) || !std::isfinite(ndc[1]) || !std::isfinite(ndc[2])) {
                continue;
            }
            for (int axis = 0; axis < 3; ++axis) {
                ndcMin[axis] = std::min(ndcMin[axis], ndc[axis]);
                ndcMax[axis] = std::max(ndcMax[axis], ndc[axis]);
            }
            if (std::abs(ndc[0]) <= 1.0f && std::abs(ndc[1]) <= 1.0f && std::abs(ndc[2]) <= 1.0f) {
                ++insideCount;
            }
        }
    }
    std::fprintf(stderr,
                 "[MPCLIP] model=%d verts=%d front=%d inside=%d badbone=%d weights=(%.4f,%.4f) "
                 "ndc=(%.3f,%.3f,%.3f)..(%.3f,%.3f,%.3f)\n",
                 modelId, vertexCount, frontCount, insideCount, invalidBoneCount, minWeightSum, maxWeightSum, ndcMin[0],
                 ndcMin[1], ndcMin[2], ndcMax[0], ndcMax[1], ndcMax[2]);
}

} // namespace

// ---- Shared scene/light/effect globals (owned by zelda3d_gl.cpp, set per frame by zelda3d.c) ----
extern "C" float gZelda3dLightDirWorld[3];
extern "C" int gZelda3dFaceCull;
extern "C" int gZelda3dFaceCullFlip;
extern "C" int gZelda3dFogEnable;
extern "C" float gZelda3dFogColor[3];
extern "C" float gZelda3dFogMul;
extern "C" float gZelda3dFogOffset;
extern "C" int gZelda3dFog3dOn;    // OoT3D PICA distance fog (title port) — zelda3d_gl.cpp
extern "C" float gZelda3dFog3d[8]; // { a, b, fogNear, fogFar, fwd.xyz, dot(fwd, eye) }
extern "C" float gZelda3dWorldAmbColor[3];
extern "C" float gZelda3dWorldAmb;
extern "C" int gZelda3dWorldLit;
// Live scene light params fed by z_kankyo via Zelda3D_GL_SetLightParams (zelda3d_gl.cpp).
// Used at UBO fill time to pre-bake scene-modulated matAmbient / matDiffuse for the unified
// vertex-lit shader so it matches OoT3D's real formula sceneAmb*matAmb + sceneDif*matDif*NdotL.
extern "C" float gZelda3dAmbient[3];
extern "C" float gZelda3dLight1Col[3];
extern "C" float gZelda3dLight2Dir[3];
extern "C" float gZelda3dLight2Col[3];
// Enabled-light count for the real per-enabled-light ambient sum (see uAmbient.w fill/consumer
// comments below; zelda3d_gl.cpp, set from live envCtx.lightSettings by zelda3d.c).
extern "C" float gZelda3dAmbientLightCount;
extern "C" int gUnifiedRenderer; // render-unification effort (kanban #131): bit 0 = CMB unified
// REPL `sgdump <modelId>`: arm a one-shot per-group render-state dump for the next draw of that model.
extern "C" int g_sgDumpModel = -1;
extern "C" int gZelda3dHlGroup;

// DRAW ISOLATION — our counterpart of the oracle harness's `drawskip`, and the reason it exists:
// ZELDA3D_SG_FRAGDBG isolates a COMBINER STAGE but applies it to every draw, so its readback is a
// whole-frame composite. That is fine when the draw you care about owns its pixels, and useless when
// it does not: Zora's water layer d9 has ZERO pixels that no other draw also covers, so neither
// `tev_mask_ratio --exclusive` nor a FRAGDBG frame can attribute anything to it, and the oracle's
// per-fragment `PIXEL ...` probe has no like-for-like counterpart on our side (instrument I002).
// Rendering ONE group and nothing else closes that gap: the resulting frame IS that draw's own
// output, so a FRAGDBG mode over it measures the same quantity the oracle's probe reports.
//
// `sgdrawonly <n>` (REPL) / ZELDA3D_SG_DRAWONLY=<n> renders only the n-th Zelda3D group of the frame.
// `sgdrawskip <n>` / ZELDA3D_SG_DRAWSKIP=<n> suppresses only that group, mirroring the oracle's
// drawskip experiment so base-minus-skip comparisons have the same compositing context on both
// sides. `sgdrawlist` / ZELDA3D_SG_DRAWLIST=1 dumps the model-local group and CMB material identity
// needed to map the per-frame draw index. Indices are sequential in append order — the same order
// that matters for translucency — so they are stable for a frozen camera and meaningless across a
// moving one.
// DEFINED HERE, not in the game layer. These controls are read and written by this file, which lives in
// libultraship -- shared by BOTH soh and mm. They used to be defined in soh/src/zelda3d/core/zelda3d.c,
// so linking the mm target failed with "undefined reference to gZelda3dSgDrawList/Only": mm links the
// same libultraship but has no soh globals to satisfy it. A diagnostic owned by the renderer belongs
// to the renderer; the game layers now just extern-declare it for their REPLs.
extern "C" int gZelda3dSgDrawOnly = -1;  // -1 = draw everything (default)
extern "C" int gZelda3dSgDrawSkip = -1;  // -1 = skip nothing; otherwise suppress one group
extern "C" int gZelda3dSgModelOnly = -1; // -1 = all models; otherwise stable model-id isolation
extern "C" int gZelda3dSgDrawList = 0;   // 1 = dump this frame's group list, then self-clears
static int g_sgDrawIdx = 0;              // groups appended so far this frame

extern "C" int Zelda3D_SgDrawIsolationIncludes(int modelId, int drawIndex) {
    return (gZelda3dSgModelOnly < 0 || modelId == gZelda3dSgModelOnly) &&
           (gZelda3dSgDrawOnly < 0 || drawIndex == gZelda3dSgDrawOnly) && drawIndex != gZelda3dSgDrawSkip;
}
// strength/bias the same way the Vulkan path does.

namespace {

bool isAdditiveBlendGroup(const SgGroup& group) {
    return group.blendEnable && group.bSrcRGB == 0x0302 && group.bDstRGB == 0x0001;
}

// One captured per-group draw, replayed inside the unified render pass.
struct DrawGroup {
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUTexture* tex;
    SDL_GPUSampler* samp;
    // Second texture binding (dual-texture detail mask, fire-glow). nullptr = none (the append
    // path substitutes the dummy texture so fragment sampler slot 2 is always backed).
    SDL_GPUTexture* tex1 = nullptr;
    SDL_GPUSampler* samp1 = nullptr;
    // Third texture binding (generic TEV, render.multi-stage-tev) — rides fragment sampler
    // slot 1 (the ex-shadow-map slot). nullptr = dummy-backed.
    SDL_GPUTexture* tex2 = nullptr;
    SDL_GPUSampler* samp2 = nullptr;
    uint32_t first, count;
    int model_group_index = -1;
    int material_index = -1;
    int dual_tex_mode = 0;
    int tev_generic = 0;
    int tex1_index = -1;
    int coord1_mapping = 1;
    // Blend constants (SDL_SetGPUBlendConstants) for a group whose pipeline uses a CONSTANT_COLOR
    // factor. Render-pass state, not pipeline state — so it travels per-draw, next to the pipeline.
    bool hasBlendConst = false;
    SDL_FColor blendConst{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<uint8_t, sizeof(SgUbo)> ubo;
};

} // namespace

void Fast::Zelda3DSdl3GpuPass::RecordSubmissionProbe(int modelId, const float* modelMatrix, int lit, int sky,
                                                     unsigned char red, unsigned char green, unsigned char blue,
                                                     unsigned char alpha, int boneCount) {
    // Zelda3D #140 render-side probe: model 2002 is the sun billboard for sky submits and Navi for
    // non-sky submits. The bounded log distinguishes a missing submission from a filtered or
    // pixel-invisible draw without flooding the runtime log.
    if (modelId != 2002) {
        return;
    }
    static int count = 0;
    if (++count > 20) {
        return;
    }
    const float tx = modelMatrix != nullptr ? modelMatrix[12] : 0.0f;
    const float ty = modelMatrix != nullptr ? modelMatrix[13] : 0.0f;
    const float tz = modelMatrix != nullptr ? modelMatrix[14] : 0.0f;
    fprintf(stderr,
            "[Zelda3D sgDraw #%d] modelId=%d sky=%d lit=%d rgba=(%d,%d,%d,%d) boneCnt=%d "
            "mp_t=(%.1f,%.1f,%.1f)\n",
            count, modelId, sky, lit, red, green, blue, alpha, boneCount, tx, ty, tz);
    fflush(stdout);
}

bool Fast::Zelda3DRenderer::groupBounds(int modelId, int groupIdx, float* outMin, float* outMax) const {
    auto it = g_models.find(modelId);
    if (it == g_models.end() || !it->second.uploaded)
        return false;
    if (groupIdx < 0 || groupIdx >= (int)it->second.groups.size())
        return false;
    const SgGroup& g = it->second.groups[(size_t)groupIdx];
    if (!g.hasGroupBounds)
        return false;
    for (int k = 0; k < 3; k++) {
        if (outMin)
            outMin[k] = g.gmin[k];
        if (outMax)
            outMax[k] = g.gmax[k];
    }
    return true;
}
// geomscan bridge: copy the last completed frame's per-draw world AABBs out to the REPL (zelda3d.c
// `geomscan`, #115/#120). Returns the count written; modelIds[i], mins[i*3..], maxs[i*3..] = draw i.
// (Ported from the removed Vulkan backend; this is the SDL3 GPU definition of Zelda3D_GeomScanDump.)
int Fast::Zelda3DRenderer::GeomScanDump(int* modelIds, float* mins, float* maxs, int maxN) {
    int n = (int)g_geomLast.size();
    if (n > maxN)
        n = maxN;
    for (int i = 0; i < n; i++) {
        modelIds[i] = g_geomLast[i].modelId;
        for (int k = 0; k < 3; k++) {
            mins[i * 3 + k] = g_geomLast[i].wmin[k];
            maxs[i * 3 + k] = g_geomLast[i].wmax[k];
        }
    }
    return n;
}

void Fast::Zelda3DRenderer::RequestEvictRange(int lo, int hi) {
    g_evictLo = lo;
    g_evictHi = hi;
    g_evictPending = true;
}
void Fast::Zelda3DRenderer::ClearOverlayDepth() {
    if (!g_ctxValid || !g_activeSdl3GpuApi)
        return;
    if (!ensureOverlayDepthResources() || !g_overlayDepthPipe)
        return;
    SDL_GPUViewport vp{};
    SDL_Rect sc{};
    g_activeSdl3GpuApi->GetZelda3DViewportScissor(vp, sc);
    // No UBO / texture — the shader is a constant fullscreen write.
    float dummyUbo[4] = { 0, 0, 0, 0 };
    g_activeSdl3GpuApi->AppendZelda3DFullscreen(g_overlayDepthPipe, dummyUbo, sizeof(dummyUbo),
                                                g_activeSdl3GpuApi->DummyTexture(), g_activeSdl3GpuApi->DummySampler(),
                                                vp, sc);
}

void Fast::Zelda3DRenderer::BeginPass() {
    g_ctxValid = false;
    g_sgDrawIdx = 0; // draw-isolation index is per-frame; reset here too in case EndPass was skipped
    // Seed the draw-isolation probe from the environment once, so it can be armed at launch (before
    // the REPL exists) as well as live. The REPL owns the globals afterwards.
    static bool sgProbeSeeded = false;
    if (!sgProbeSeeded) {
        sgProbeSeeded = true;
        if (const char* v = getenv("ZELDA3D_SG_DRAWONLY"))
            gZelda3dSgDrawOnly = atoi(v);
        if (const char* v = getenv("ZELDA3D_SG_DRAWSKIP")) {
            gZelda3dSgDrawSkip = atoi(v);
        }
        if (const char* v = getenv("ZELDA3D_SG_MODELONLY"))
            gZelda3dSgModelOnly = atoi(v);
        if (const char* v = getenv("ZELDA3D_SG_DRAWLIST"))
            gZelda3dSgDrawList = (v[0] != '0');
    }
    // Publish the previous frame's geometry capture; start a fresh one for this frame's draws.
    g_geomLast.swap(g_geomCur);
    g_geomCur.clear();
    if (!g_activeSdl3GpuApi)
        return;
    if (!ensureResources())
        return;
    applyPendingEvict();
    g_ctxValid = true;
}

void Fast::Zelda3DRenderer::DrawModel(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                      unsigned char r8, unsigned char g8, unsigned char b8, unsigned char a8,
                                      float aspectAdj, const float* boneData, int boneCnt, unsigned long long midMask,
                                      int sky, float uvOffU, float uvOffV, const void* matTex, const void* matConst,
                                      const void* matUv, int forceUnlit, const float* lightDirOv,
                                      const float* sphRotOv) {
    const std::unordered_map<int, int>* matTexMap = static_cast<const std::unordered_map<int, int>*>(matTex);
    const std::unordered_map<int, Zelda3DMatConstOv>* matConstMap =
        static_cast<const std::unordered_map<int, Zelda3DMatConstOv>*>(matConst);
    const std::unordered_map<int, Zelda3DMatUvOv>* matUvMap =
        static_cast<const std::unordered_map<int, Zelda3DMatUvOv>*>(matUv);
    if (!g_ctxValid || mp16 == nullptr || mv16 == nullptr) {
        if (modelId == gZelda3dTraceModelId) {
            std::fprintf(stderr, "[MPDROP] model=%d context=%d mp=%d mv=%d\n", modelId, g_ctxValid ? 1 : 0,
                         mp16 != nullptr ? 1 : 0, mv16 != nullptr ? 1 : 0);
        }
        return;
    }
    SgModel* m = ensureUploaded(modelId);
    if (!m || !m->vbo) {
        if (modelId == gZelda3dTraceModelId) {
            std::fprintf(stderr, "[MPDROP] model=%d upload=%d vbo=%d\n", modelId, m != nullptr ? 1 : 0,
                         m != nullptr && m->vbo != nullptr ? 1 : 0);
        }
        return;
    }

    TraceSkinnedClipBounds(modelId, mp16, boneData, boneCnt, midMask, aspectAdj);

    // Geometry-value capture (geomscan): world AABB = local AABB transformed by mv16 (model->world,
    // column-major to match the shader's ubo.uMV * pos). One record per visible draw; the #115/#120
    // sweep reads these to flag misrendered geometry by VALUE (huge/degenerate extent), no diff.
    if (m->hasBounds && mv16 != nullptr && g_geomCur.size() < 4096) {
        GeomRec rec;
        rec.modelId = modelId;
        bool first = true;
        for (int c = 0; c < 8; c++) {
            float lx = (c & 1) ? m->localMax[0] : m->localMin[0];
            float ly = (c & 2) ? m->localMax[1] : m->localMin[1];
            float lz = (c & 4) ? m->localMax[2] : m->localMin[2];
            float wx = mv16[0] * lx + mv16[4] * ly + mv16[8] * lz + mv16[12];
            float wy = mv16[1] * lx + mv16[5] * ly + mv16[9] * lz + mv16[13];
            float wz = mv16[2] * lx + mv16[6] * ly + mv16[10] * lz + mv16[14];
            if (first) {
                rec.wmin[0] = rec.wmax[0] = wx;
                rec.wmin[1] = rec.wmax[1] = wy;
                rec.wmin[2] = rec.wmax[2] = wz;
                first = false;
            } else {
                if (wx < rec.wmin[0])
                    rec.wmin[0] = wx;
                if (wx > rec.wmax[0])
                    rec.wmax[0] = wx;
                if (wy < rec.wmin[1])
                    rec.wmin[1] = wy;
                if (wy > rec.wmax[1])
                    rec.wmax[1] = wy;
                if (wz < rec.wmin[2])
                    rec.wmin[2] = wz;
                if (wz > rec.wmax[2])
                    rec.wmax[2] = wz;
            }
        }
        g_geomCur.push_back(rec);
    }

    GfxRenderingAPISdl3Gpu* api = g_activeSdl3GpuApi;

    // RenderDoc-style per-draw inspection (REPL `sgdump <modelId>`): one-shot dump of every material
    // group's render state for one model, so a missing/invisible group is diagnosed by VALUE (which
    // state — alpha test, blend, cull, texture binding — kills it) instead of eyeballing the frame.
    const bool sgDumpThisDraw = modelId == g_sgDumpModel;
    if (sgDumpThisDraw) {
        g_sgDumpModel = -1; // one-shot
        fprintf(stderr,
                "[SG_DUMP] model=%d groups=%zu lit=%d invertY=%d tint=(%u,%u,%u) a=%u aspectAdj=%.4f "
                "sky=%d worldLit=%d worldAmb=%.3f ambColor=(%.2f,%.2f,%.2f) fogOn=%d\n",
                modelId, m->groups.size(), lit, invertY, r8, g8, b8, a8, aspectAdj, sky, gZelda3dWorldLit,
                gZelda3dWorldAmb, gZelda3dWorldAmbColor[0], gZelda3dWorldAmbColor[1], gZelda3dWorldAmbColor[2],
                gZelda3dFogEnable);
        fprintf(stderr,
                "[SG_DUMP] model=%d fog3dOn=%d a=%.6f b=%.4f fogNear=%.1f fogFar=%.1f fogColor=(%.3f,%.3f,%.3f)\n",
                modelId, gZelda3dFog3dOn, gZelda3dFog3d[0], gZelda3dFog3d[1], gZelda3dFog3d[2], gZelda3dFog3d[3],
                gZelda3dFogColor[0], gZelda3dFogColor[1], gZelda3dFogColor[2]);
        int gi = -1;
        for (const SgGroup& grp : m->groups) {
            gi++;
            const bool hasTex =
                grp.texIndex >= 0 && grp.texIndex < (int)m->textures.size() && m->textures[grp.texIndex];
            fprintf(stderr,
                    "[SG_DUMP]  g%-2d cull=%d faceCull=%d meshId=%d tex=%d%s mat=%d vtxLit=%d combScale=%.3f "
                    "blend=%d(src=%#06x dst=%#06x) aTest=%d aRef=%.3f depthW=%d polyOff=%.4f first=%u count=%u "
                    "vColor0=(%.3f,%.3f,%.3f,%.3f) matAmb=(%.2f,%.2f,%.2f) matDif=(%.2f,%.2f,%.2f) "
                    "uv=[(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)] wrap=(%#06x,%#06x) "
                    "tex1=%d dualMode=%d dualScale2=%.1f constScale=%.1f uv1Xf=(%.2f,%.2f,%.2f,%.4f)\n",
                    gi, grp.cull, grp.faceCull, grp.meshId, grp.texIndex, hasTex ? "" : "(MISSING->dummy)",
                    grp.materialIndex, grp.vertexLighting, grp.combScaleRGB, grp.blendEnable, grp.bSrcRGB, grp.bDstRGB,
                    grp.alphaTest, grp.alphaRef, grp.depthWrite, grp.polygonOffset, grp.first, grp.count,
                    grp.dbgColor0[0], grp.dbgColor0[1], grp.dbgColor0[2], grp.dbgColor0[3], grp.matAmbient[0],
                    grp.matAmbient[1], grp.matAmbient[2], grp.matDiffuse[0], grp.matDiffuse[1], grp.matDiffuse[2],
                    grp.dbgUv0[0], grp.dbgUv0[1], grp.dbgUv1[0], grp.dbgUv1[1], grp.dbgUv2[0], grp.dbgUv2[1], grp.wrapS,
                    grp.wrapT, grp.tex1Index, grp.dualTexMode, grp.dualTexScale2, grp.combConstScaleRGB,
                    grp.uv1Scale[0], grp.uv1Scale[1], grp.uv1Trans[0], grp.uv1Trans[1]);
            // PICA200 TEV constant palette + stage-0 selector — dumped on its own line so the
            // main SG_DUMP row stays parseable by existing tools; format:
            //   [SG_DUMP]   g<n> constIdx=<i> const0..const5=(r,g,b,a) x 6
            fprintf(stderr,
                    "[SG_DUMP]  g%-2d combUsesConst=%d constIdx=%d "
                    "const0=(%.3f,%.3f,%.3f,%.3f) const1=(%.3f,%.3f,%.3f,%.3f) "
                    "const2=(%.3f,%.3f,%.3f,%.3f) const3=(%.3f,%.3f,%.3f,%.3f) "
                    "const4=(%.3f,%.3f,%.3f,%.3f) const5=(%.3f,%.3f,%.3f,%.3f)\n",
                    gi, grp.combUsesConst, grp.combConstIdx, grp.matConstant[0][0], grp.matConstant[0][1],
                    grp.matConstant[0][2], grp.matConstant[0][3], grp.matConstant[1][0], grp.matConstant[1][1],
                    grp.matConstant[1][2], grp.matConstant[1][3], grp.matConstant[2][0], grp.matConstant[2][1],
                    grp.matConstant[2][2], grp.matConstant[2][3], grp.matConstant[3][0], grp.matConstant[3][1],
                    grp.matConstant[3][2], grp.matConstant[3][3], grp.matConstant[4][0], grp.matConstant[4][1],
                    grp.matConstant[4][2], grp.matConstant[4][3], grp.matConstant[5][0], grp.matConstant[5][1],
                    grp.matConstant[5][2], grp.matConstant[5][3]);
            // Generic TEV chain (render.multi-stage-tev): the packed per-stage words the shader
            // will decode (packing documented at Zelda3DGlGroup::tevStagePack).
            if (grp.tevGeneric) {
                fprintf(stderr,
                        "[SG_DUMP]  g%-2d tevGeneric=1 stages=%d tex2=%d coordMap=(%d,%d) "
                        "uv2Xf=(%.2f,%.2f,%.2f,%.4f) pack=",
                        gi, grp.tevStageCount, grp.tex2Index, grp.coord1Mapping, grp.coord2Mapping, grp.uv2Scale[0],
                        grp.uv2Scale[1], grp.uv2Trans[0], grp.uv2Trans[1]);
                for (int s = 0; s < grp.tevStageCount && s < 6; s++)
                    fprintf(stderr, "%s%08x/%08x/%08x", s ? "," : "", grp.tevStagePack[s][0], grp.tevStagePack[s][1],
                            grp.tevStagePack[s][2]);
                fprintf(stderr, "\n");
            }
        }
    }

    // Base UBO shared by all groups (per-group alphaRef/depthOffset/etc. patched below).
    SgUbo base{};
    memcpy(base.uMP, mp16, sizeof(base.uMP));
    base.uMP[0] *= aspectAdj; // mirror Fast3D AdjXForAspectRatio (MP column 0)
    base.uMP[4] *= aspectAdj;
    base.uMP[8] *= aspectAdj;
    base.uMP[12] *= aspectAdj;
    memcpy(base.uMV, mv16, sizeof(base.uMV));
    for (int k = 0; k < ZELDA3D_GL_MAX_BONES; k++)
        for (int e = 0; e < 16; e++)
            base.uBones[k * 16 + e] = (e % 5 == 0) ? 1.0f : 0.0f;
    if (boneData && boneCnt > 0) {
        // boneData is row-major (M*v). std140 mat4 is column-major with no transpose-on-upload, so
        // transpose CPU-side to match the GL path (which uploads with GL_TRUE transpose).
        int nb = boneCnt < ZELDA3D_GL_MAX_BONES ? boneCnt : ZELDA3D_GL_MAX_BONES;
        for (int k = 0; k < nb; k++) {
            const float* s = boneData + k * 16;
            float* d = base.uBones + k * 16;
            for (int rr = 0; rr < 4; rr++)
                for (int col = 0; col < 4; col++)
                    d[col * 4 + rr] = s[rr * 4 + col];
        }
    }
    base.uParams[0] = invertY ? -1.0f : 1.0f;
    base.uParams[1] = lit ? 1.0f : 0.0f;
    base.uTintSkin[0] = r8 / 255.0f;
    base.uTintSkin[1] = g8 / 255.0f;
    base.uTintSkin[2] = b8 / 255.0f;
    base.uTintSkin[3] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;
    if (sphRotOv) {
        // Sphere-map view-rotation override (see zelda3d_sg_ubo.h uSphRot* comment): sphRotOv is
        // the row-major 3x3 view-rotation the caller derived from the live camera. uSphRot0.w is
        // the shader-side gate. base{} zero-init keeps it off for every draw that doesn't set it.
        memcpy(base.uSphRot0, sphRotOv + 0, 3 * sizeof(float));
        memcpy(base.uSphRot1, sphRotOv + 3, 3 * sizeof(float));
        memcpy(base.uSphRot2, sphRotOv + 6, 3 * sizeof(float));
        base.uSphRot0[3] = 1.0f;
    }
    if (lightDirOv) {
        // Wordmark sheen (title_logo_actor.md §6.3): lightDirOv is OBJECT-space (same space as
        // this model's own vertex normals aNrm) — transform by THIS draw's own mv16 (column-major,
        // matching the vertex shader's `vNrmView = mat3(ubo.uMV) * nM`) so the light direction and
        // the geometry it lights land in the same space, then renormalize (the decomp's own
        // formula produces a non-unit vector before its own normalize() call too).
        float wd[3] = {
            mv16[0] * lightDirOv[0] + mv16[4] * lightDirOv[1] + mv16[8] * lightDirOv[2],
            mv16[1] * lightDirOv[0] + mv16[5] * lightDirOv[1] + mv16[9] * lightDirOv[2],
            mv16[2] * lightDirOv[0] + mv16[6] * lightDirOv[1] + mv16[10] * lightDirOv[2],
        };
        float len = std::sqrt(wd[0] * wd[0] + wd[1] * wd[1] + wd[2] * wd[2]);
        if (len > 1e-6f) {
            wd[0] /= len;
            wd[1] /= len;
            wd[2] /= len;
        }
        base.uLightDir[0] = wd[0];
        base.uLightDir[1] = wd[1];
        base.uLightDir[2] = wd[2];
    } else {
        base.uLightDir[0] = gZelda3dLightDirWorld[0];
        base.uLightDir[1] = gZelda3dLightDirWorld[1];
        base.uLightDir[2] = gZelda3dLightDirWorld[2];
    }
    base.uLightDir[3] = sky ? 1.0f : 0.0f;
    // Light slot 2's world direction for the vertex-lit diffuse sum (#153). Normalized by
    // Zelda3D_UpdateLight; a degenerate (0,0,0) slot direction stays zero and nulls only that
    // light's diffuse term (dot = 0), matching FUN_003fa5d0's semantics.
    base.uLightDir2[0] = gZelda3dLight2Dir[0];
    base.uLightDir2[1] = gZelda3dLight2Dir[1];
    base.uLightDir2[2] = gZelda3dLight2Dir[2];
    base.uLightDir2[3] = 0.0f;
    // §6.3/§6.6's light-env ambient (0.18,0.18,0.18,1); the diffuse coefficient is WHITE and
    // lives directly in the shader's ndotl term. uSheen.x doubles as the wordmark-path gate.
    base.uSheen[0] = lightDirOv ? kWordmarkLightAmbient : 0.0f;
    base.uSheen[1] = base.uSheen[2] = base.uSheen[3] = 0.0f;
    base.uExtra[0] = a8 / 255.0f;
    base.uExtra[1] = uvOffU;
    base.uExtra[2] = uvOffV;
    base.uShadow[0] = 0.0f; // shadow-map/SSAO enhancements removed (OoT3D lighting only)
    base.uFog[0] = gZelda3dFogColor[0];
    base.uFog[1] = gZelda3dFogColor[1];
    base.uFog[2] = gZelda3dFogColor[2];
    base.uFog[3] = gZelda3dFogEnable ? 1.0f : 0.0f;
    base.uFog2[0] = gZelda3dFogMul;
    base.uFog2[1] = gZelda3dFogOffset;
    // OoT3D PICA distance fog (title port): frame-level params; the per-group loop below flips
    // uFog.w to 2.0 for materials whose CMB isFogEnabled byte is set.
    memcpy(base.uFog3d0, gZelda3dFog3d, 4 * sizeof(float));
    memcpy(base.uFog3d1, gZelda3dFog3d + 4, 4 * sizeof(float));
    base.uAmbient[0] = gZelda3dWorldAmbColor[0];
    base.uAmbient[1] = gZelda3dWorldAmbColor[1];
    base.uAmbient[2] = gZelda3dWorldAmbColor[2];
    base.uAmbient[3] = 0.0f;
    bool forceBlend = (a8 < 255);

    bool roomHl = (gZelda3dHlGroup >= 0 && gZelda3dHlGroup < (int)m->groups.size());
    // OoT3D winds its front faces CCW; SDL3 GPU never inverts clip-Y (invertY is always 0 here), so
    // the old `invertY ^ flip` term is dead. Front-face = CCW unless gZelda3dFaceCullFlip is set.
    int frontCW = Zelda3DSg::FrontFaceIsCW(gZelda3dFaceCullFlip) ? 1 : 0;

    // Render-unification effort (kanban #131), Phase 2: route through the unified shader/vertex
    // format instead of the fixed CMB shader above when the bit is set. Falls back to the old path
    // if the unified upload fails (never silently drops the draw).
    bool unified = (gUnifiedRenderer & 1) != 0;
    SgModel* um = unified ? ensureUnifiedUploaded(modelId) : nullptr;
    if (unified && (!um || !um->unifiedVbo))
        unified = false;

    std::vector<DrawGroup> dgs;
    dgs.reserve(m->groups.size());
    int gIdx = -1;
    for (const SgGroup& grp : m->groups) {
        gIdx++;
        if (grp.cull)
            continue;
        if (grp.meshId >= 0 && grp.meshId < 64 && !((midMask >> grp.meshId) & 1ull))
            continue;

        SgUbo ubo = base;
        float groupUvU = uvOffU;
        float groupUvV = uvOffV;
        if (matUvMap) {
            auto uvIt = matUvMap->find(grp.materialIndex);
            if (uvIt != matUvMap->end()) {
                groupUvU += uvIt->second.u;
                groupUvV += uvIt->second.v;
            }
        }
        ubo.uExtra[1] = groupUvU;
        ubo.uExtra[2] = groupUvV;
        if (roomHl && gIdx == gZelda3dHlGroup) {
            ubo.uTintSkin[0] = 1.0f;
            ubo.uTintSkin[1] = 0.0f;
            ubo.uTintSkin[2] = 0.0f;
        }
        ubo.uParams[2] = grp.alphaTest ? grp.alphaRef : 0.0f;
        ubo.uParams[3] = grp.polygonOffset;
        // Alpha-test compare, encoded as (GL enum - 0x200) + 1 so 0 means DISABLED. Written
        // unconditionally: uTevCtl[0..2] are only set on the generic-TEV path.
        ubo.uTevCtl[3] = grp.alphaTest ? (float)((grp.alphaFunc & 7u) + 1u) : 0.0f;
        // OoT3D PICA distance fog (title port): per-DRAW enable = the frame-level 3DS-fog state
        // AND this material's CMB isFogEnabled byte (fog_mode=5 on the 3DS; the additive/effect
        // materials opt out). uFog.w == 2.0 selects the 3DS LUT path in the shader, overriding
        // the (default-off) F3DEX ramp; sky is excluded shader-side via uLightDir.w.
        if (gZelda3dFog3dOn && grp.fogEnabled) {
            ubo.uFog[3] = 2.0f;
        }
        // combScaleRGB is the CMB material's authored TEV stage-0 RGB scale — always apply when
        // the material asks for it. Only the additive scene-ambient floor (uAmbient.w below) is
        // gated by gZelda3dWorldLit (task #16: at title we skip the synthetic vertex-lit compute
        // but keep the material's static brightness).
        ubo.uExtra[3] = grp.vertexLighting ? grp.combScaleRGB : 1.0f;
        // KNOWN GAP (measured 2026-07-22, per_draw_light_setup.md §6): we emulate ONE texture
        // through ONE TEV stage. Zora's Domain's water/waterfall materials enable texture1 (and
        // sometimes texture2) and run TEV stage 1/2 with color_op = MultiplyThenAdd; those are
        // exactly the surfaces measuring 0.62-0.88 of the oracle, while every single-texture
        // single-stage surface in the same frame is far closer. Fixing "Zora's water is dark and
        // desaturated" means emulating multi-texture + stages 1..5, NOT touching the lighting.
        //
        // CLOSED 2026-07-22 (later): multi-stage TEV landed, and the follow-on "Zora ground 0.79 /
        // walls 0.86 scene-wide deficit" that survived it WAS NOT A RENDERER DEFECT AT ALL. It was a
        // HI-RES TEXTURE PACK ASYMMETRY in the measurement: the oracle frames were captured by a
        // harness predating 7a1dc7e0 (Azahar with no custom textures) while our side, launched from
        // the repo root where textures/ lives, rendered Henriko's 4K pack — whose Zora rock/ground
        // replacements are ~20% darker than the ROM texels. Controlled A/B at one matched camera,
        // only the pack differing: near ground d11 0.811 -> 0.977, rock walls d3 0.853 -> 0.921; and
        // over d3's EXCLUSIVE pixels (those no translucent draw overlays) 1.002. Every opaque scene
        // surface at Zora is at parity vanilla-on-vanilla.
        //   FALSIFIED, do not retry as causes of that deficit: (a) "a decal-layer draw we drop
        //   entirely" — the oracle's 27 scene draws already map 1:1 onto our 21 room + 6 waterfall
        //   groups, and the exclusive-pixel ratios are 0.99-1.00, so there is no missing layer;
        //   (b) "ETC1 mip/LOD selection" and (c) "vertex-colour interpolation" — both would have to
        //   act on the ground draw, which now measures 0.99. The "non-monotonic depth banding"
        //   (0.92/0.69/0.83/0.77/...) that motivated all three was the pack's per-texture darkening
        //   sampled at different distances, not a depth-dependent shading error.
        // tools/tev_mask_ratio.py now HARD-FAILS on a pack asymmetry so this cannot recur.
        // See debug_journal/2026-07-22-zora-ground-deficit-was-texpack-asymmetry.md.
        // OoT3D scene-vertex-lit path (task #16): feed uAmbient.xyz = sceneAmb * matAmb.
        // Per LIGHTDIAG at pinned title cursor=650: grass room groups have
        // matAmb=(1,1,1) matDif=(0,0,0), combScale=2. So the diffuse term contributes
        // nothing for these materials — the entire lit result rides on
        // t * vColor * sceneAmb * combScale. The remaining ground dimness is a
        // SCENE-AMBIENT mismatch (SoH forces midnight → sceneAmb=(0.16,0.14,0.30)
        // blue-heavy), not a shader defect. The Az-matching fix is to use OoT3D's
        // actual title-demo lightSettings values, not to change the shader math. See
        // debug_journal/2026-07-04-title-parity-pinned650.md.
        // forceUnlit (title logo / self-illuminated overlays, ZELDA3D_HANDLE_FORCE_UNLIT): ignore
        // this material's own vertex_lighting flag so the scene ambient never darkens the draw.
        bool ambGroup = (grp.vertexLighting && gZelda3dWorldLit && !forceUnlit);
        ubo.uAmbient[0] = gZelda3dAmbient[0] * grp.matAmbient[0];
        ubo.uAmbient[1] = gZelda3dAmbient[1] * grp.matAmbient[1];
        ubo.uAmbient[2] = gZelda3dAmbient[2] * grp.matAmbient[2];
        // .w = enabled-light count (title_env_lighting.md §10/§11's per-enabled-light ambient sum;
        // see the kFrag comment at its consumer). 0 keeps the ambient path off exactly as before.
        // CHARACTER draws (lit) apply ambient ONCE: oracle vsuni capture at title cs1575
        // (scratch/title_ab/actor_light_uniforms.log) shows the Epona/Link draw with
        // amb0=(0.408,0.408,0.239) but amb1=(0,0,0) — N64 Lights_BindAll semantics (one scene
        // ambient) — while the same frame's terrain draws carry the identical ambient in BOTH
        // slots. The per-slot duplication is a scene-material binding, not a global rule.
        //
        // RE-CONFIRMED 2026-07-22 against the live oracle at Zora's Domain AND Kokiri Forest, four
        // times of day, using per-draw ISOLATION (oot3d-decomp/docs/per_draw_light_setup.md).
        // OoT3D binds exactly TWO configurations per frame and this file implements both:
        //   SCENE draw: both slots bound; dir = world (0,-1,0); light diffuse (0,0,0) in BOTH
        //               slots; ambient = sceneAmbient in BOTH slots  -> the x2 below.
        //   ACTOR draw: slot0 dir=+sunVec dif=light2Col amb=sceneAmbient;
        //               slot1 dir=-sunVec dif=light1Col amb=(0,0,0)  -> the `lit ? 1.0f` below.
        // Falsified there, do NOT re-chase:
        //   - "draws carrying matDif=(1,1,1) are lit differently": a SCENE slot's light diffuse
        //     is ZERO, so matDiffuse cannot contribute to any room draw whatever its value.
        //   - "our light dirs (+-0.702,+-0.702,+-0.117) differ from the oracle's
        //     (+-0.121,+-0.816,-+0.565)": the PICA c80/c83 registers are VIEW space; mapped back
        //     with right*x + up*y - fwd*z they equal ours to 3 decimals at BOTH scenes, and they
        //     track the same dayTime sun rotation the engine already computes.
        ubo.uAmbient[3] = ambGroup ? (lit ? 1.0f : gZelda3dAmbientLightCount) : 0.0f;
        // Per-light diffuse products for the vertex-lit sum (#153): matDiffuse * sceneLightColor.
        // Terrain materials bake matDiffuse=BLACK, so these are zero there and the light sum
        // reduces to the previously-verified ambient-only value.
        for (int k = 0; k < 3; k++) {
            ubo.uLitDif1[k] = grp.matDiffuse[k] * gZelda3dLight1Col[k];
            ubo.uLitDif2[k] = grp.matDiffuse[k] * gZelda3dLight2Col[k];
        }
        ubo.uLitDif1[3] = 0.0f;
        ubo.uLitDif2[3] = 0.0f;
        // PICA200 TEV CONSTANT modulate: for materials whose combiner sources CONSTANT in any
        // stage, publish the selected slot's RGB with .a = 1 so the shader applies it. Materials
        // that never reference CONSTANT (e.g. plain MODULATE(PRIM, TEX0)) leave .a = 0 and the
        // shader skips the multiply — this matches OoT3D's per-material combiner semantics.
        // Per-actor override channel (EnHy Step 2c, TownsfolkBehavior::applyDrawOverrides):
        // if this actor has an override for grp.materialIndex, its RGB replaces the CMB-file
        // default and .a is forced to 1 so the shader applies it (townsfolk clothing colour).
        {
            int ci = grp.combConstIdx & 7;
            if (ci > 5)
                ci = 0;
            ubo.uMatConst[0] = grp.matConstant[ci][0];
            ubo.uMatConst[1] = grp.matConstant[ci][1];
            ubo.uMatConst[2] = grp.matConstant[ci][2];
            // Only apply the CONSTANT modulation when it's a valid MODULATE colour (non-zero
            // RGB). Some CMB materials list CONSTANT as a stage source but the actual combiner
            // op is REPLACE / ADD (multi-stage full emulation is a documented follow-up); their
            // baked constant is (0,0,0,1) which our MODULATE-only fallback would turn to BLACK.
            // fine_star.cmb is the flagship case: combUsesConst=1 + matConst[0]=(0,0,0,1) is
            // just a stage-source marker, and multiplying by 0 hides every star (task #16).
            bool constBlack =
                (grp.matConstant[ci][0] < 1e-4f && grp.matConstant[ci][1] < 1e-4f && grp.matConstant[ci][2] < 1e-4f);
            // uMatConst.a carries the CONSTANT stage's hardware RGB scale (1/2/4) as the apply
            // value; 0 = skip. The shader multiplies by uMatConst.rgb * uMatConst.a so the PICA
            // per-stage scale (fire-glow ×2) rides the same seam.
            ubo.uMatConst[3] = (grp.combUsesConst && !constBlack) ? grp.combConstScaleRGB : 0.0f;
            if (matConstMap && grp.materialIndex >= 0) {
                auto ov = matConstMap->find(grp.materialIndex * 6 + ci);
                if (ov != matConstMap->end() && ov->second.constIdx == ci) {
                    ubo.uMatConst[0] = ov->second.rgba[0];
                    ubo.uMatConst[1] = ov->second.rgba[1];
                    ubo.uMatConst[2] = ov->second.rgba[2];
                    ubo.uMatConst[3] = grp.combConstScaleRGB; // force apply (townsfolk clothing colour)
                }
            }
        }

        // Dual-texture stage 0 (fire-glow detail mask): enable flag on uSheen.y, coordinator-1
        // transform (+ this draw's CMAB UV-scroll) on uTex1Xf. The per-draw scroll offset routes
        // to coordinator 1 here (the material the CMAB animates — its Translation track is
        // channelIndex 1, title_logo_fireglow_cmab.md §3.2 fix 3) instead of the coordinator-0
        // offset uExtra.yz used by single-texture scroll consumers (sky cloud band, #28b).
        if (grp.dualTexMode) {
            ubo.uSheen[1] = (float)grp.dualTexMode;
            ubo.uSheen[2] = grp.dualTexScale2;
            ubo.uSheen[3] = (grp.dualTexMode == 4) ? 3.0f : (float)grp.coord1Mapping;
            ubo.uTex1Xf[0] = grp.uv1Scale[0];
            ubo.uTex1Xf[1] = grp.uv1Scale[1];
            ubo.uTex1Xf[2] = grp.uv1Trans[0] + groupUvU;
            ubo.uTex1Xf[3] = grp.uv1Trans[1] + groupUvV;
            ubo.uExtra[1] = 0.0f;
            ubo.uExtra[2] = 0.0f;
        }

        // Generic per-stage TEV chain (render.multi-stage-tev): everything that is neither the
        // trivial single-MODULATE legacy shape nor a classified dual-texture title shape
        // (cmb.cpp parseMats routing). uTevCtl.x > 0 switches the fragment shader to tevRun();
        // the legacy combiner knobs (uExtra.w scale / uMatConst / uSheen.y) are bypassed there.
        if (grp.tevGeneric && grp.tevStageCount > 0) {
            ubo.uTevCtl[0] = (float)grp.tevStageCount;
            ubo.uTevCtl[1] = (float)grp.coord1Mapping;
            ubo.uTevCtl[2] = (float)grp.coord2Mapping;
            for (int s = 0; s < grp.tevStageCount && s < 6; s++) {
                ubo.uTevStages[s * 4 + 0] = grp.tevStagePack[s][0];
                ubo.uTevStages[s * 4 + 1] = grp.tevStagePack[s][1];
                ubo.uTevStages[s * 4 + 2] = grp.tevStagePack[s][2];
                ubo.uTevStages[s * 4 + 3] = 0;
            }
            // Constant-color palette, quantized to RGBA8 (PICA's const registers are 8-bit),
            // AFTER the per-actor override channel (EnHy townsfolk clothing colours patch a
            // slot at draw time — same override data the legacy uMatConst path consumes).
            float constSlots[6][4];
            memcpy(constSlots, grp.matConstant, sizeof(constSlots));
            if (matConstMap && grp.materialIndex >= 0) {
                for (int slot = 0; slot < 6; ++slot) {
                    auto ov = matConstMap->find(grp.materialIndex * 6 + slot);
                    if (ov != matConstMap->end() && ov->second.constIdx == slot) {
                        for (int k = 0; k < 4; k++)
                            constSlots[slot][k] = ov->second.rgba[k];
                    }
                }
            }
            for (int s = 0; s < 6; s++) {
                auto q = [](float v) {
                    int i = (int)(v * 255.0f + 0.5f);
                    return (uint32_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
                };
                ubo.uTevConst[s] = q(constSlots[s][0]) | (q(constSlots[s][1]) << 8) | (q(constSlots[s][2]) << 16) |
                                   (q(constSlots[s][3]) << 24);
            }
            if (sgDumpThisDraw) {
                fprintf(stderr,
                        "[SG_DUMP]  g%d appliedConst1=(%.3f,%.3f,%.3f,%.3f) "
                        "appliedConst2=(%.3f,%.3f,%.3f,%.3f) overrides=%zu\n",
                        grp.materialIndex, constSlots[1][0], constSlots[1][1], constSlots[1][2], constSlots[1][3],
                        constSlots[2][0], constSlots[2][1], constSlots[2][2], constSlots[2][3],
                        matConstMap ? matConstMap->size() : 0u);
            }
            // Coordinator-1/2 transforms for the extra units. The vertex shader's uv1 sphere
            // path gates on uSheen.w > 2.5 (shared with the dual-tex title path); mapping 4
            // (ProjectionMap) is not emulated and falls back to plain UV.
            ubo.uSheen[3] = (grp.coord1Mapping == 3) ? 3.0f : 1.0f;
            ubo.uTex1Xf[0] = grp.uv1Scale[0];
            ubo.uTex1Xf[1] = grp.uv1Scale[1];
            ubo.uTex1Xf[2] = grp.uv1Trans[0];
            ubo.uTex1Xf[3] = grp.uv1Trans[1];
            ubo.uTex2Xf[0] = grp.uv2Scale[0];
            ubo.uTex2Xf[1] = grp.uv2Scale[1];
            ubo.uTex2Xf[2] = grp.uv2Trans[0];
            ubo.uTex2Xf[3] = grp.uv2Trans[1];
        }

        // Facial material-anim override.
        int texIndex = grp.texIndex;
        if (matTexMap && grp.materialIndex >= 0) {
            auto ov = matTexMap->find(grp.materialIndex);
            if (ov != matTexMap->end() && ov->second >= 0)
                texIndex = ov->second;
        }
        SDL_GPUTexture* tex = Fast::g_activeSdl3GpuApi->DummyTexture();
        SDL_GPUSampler* samp = Fast::g_activeSdl3GpuApi->DummySampler();
        // noMip NARROWED 2026-07-29. The old rule denied mips to every additive-blend group, which
        // is 790 materials -- but 529 of them are additive dungeon FLOORS and WALLS (Dodongo's
        // Cavern dg_dod_03yuka at 2153x1965 world units on a 256x256 texture, Fire Temple
        // dg05_yuka_06, the wtr_uvwater overlays), repeat-wrapped and viewed at grazing angles,
        // i.e. exactly what mips exist to fix. The rule was argued entirely from small sparse
        // sparkle sprites.
        //
        // The asset itself says which is which, and now that the authored chain is parsed (C018) we
        // can ask it: a texture that ships ONE level is one the artist gave no mips, and
        // synthesising one for it is what crushes the sparkle peak. A texture that ships 3-4 levels
        // was authored WITH mips and should keep them. Among additive materials that splits
        // 414 single-level (fine_star and friends -> no mips) from 361 multi-level (the floors and
        // water overlays -> mips), which is the principled separator the size heuristic was groping
        // for -- and it needs no threshold to tune.
        int texLevels = 1;
        if (texIndex >= 0 && texIndex < (int)m->texLevels.size())
            texLevels = m->texLevels[texIndex];
        bool additive = isAdditiveBlendGroup(grp) && texLevels <= 1;
        if (texIndex >= 0 && texIndex < (int)m->textures.size() && m->textures[texIndex]) {
            tex = m->textures[texIndex];
            samp = getSampler(grp.wrapS, grp.wrapT, additive);
        }
        // Second texture binding: non-dummy when the group's dual-tex mode is on (uSheen.y gates
        // the shader-side sample) or its generic TEV chain can source TEXTURE1. Third binding
        // (uTex2, the repurposed ex-shadow sampler slot): generic TEV only.
        SDL_GPUTexture* tex1 = nullptr;
        SDL_GPUSampler* samp1 = nullptr;
        if ((grp.dualTexMode || grp.tevGeneric) && grp.tex1Index >= 0 && grp.tex1Index < (int)m->textures.size() &&
            m->textures[grp.tex1Index]) {
            tex1 = m->textures[grp.tex1Index];
            samp1 = getSampler(grp.wrap1S, grp.wrap1T, additive);
        }
        SDL_GPUTexture* tex2 = nullptr;
        SDL_GPUSampler* samp2 = nullptr;
        if (grp.tevGeneric && grp.tex2Index >= 0 && grp.tex2Index < (int)m->textures.size() &&
            m->textures[grp.tex2Index]) {
            tex2 = m->textures[grp.tex2Index];
            samp2 = getSampler(grp.wrap2S, grp.wrap2T, additive);
        }

        // Translucent draw over an opaque material: synthesize a standard alpha-over pipeline.
        SgGroup gb = grp;
        if (forceBlend && !grp.blendEnable) {
            gb.blendEnable = 1;
            gb.bSrcRGB = 0x0302;
            gb.bDstRGB = 0x0303;
            gb.bEqRGB = 0x8006;
            gb.bSrcA = 0x0302;
            gb.bDstA = 0x0303;
            gb.bEqA = 0x8006;
        }

        DrawGroup dg;
        dg.tex = tex;
        dg.samp = samp;
        dg.tex1 = tex1;
        dg.samp1 = samp1;
        dg.tex2 = tex2;
        dg.samp2 = samp2;
        dg.first = grp.first;
        dg.count = grp.count;
        dg.model_group_index = gIdx;
        dg.material_index = grp.materialIndex;
        dg.dual_tex_mode = grp.dualTexMode;
        dg.tev_generic = grp.tevGeneric;
        dg.tex1_index = grp.tex1Index;
        dg.coord1_mapping = grp.coord1Mapping;
        dg.hasBlendConst = Fast::Zelda3DSdl3GpuPipeline::BlendConstants(gb, dg.blendConst);
        if (unified) {
            bool hasTex = tex != Fast::g_activeSdl3GpuApi->DummyTexture();
            auto variant = Fast::Zelda3DSdl3GpuPipeline::VariantForGroup(gb, hasTex);
            dg.pipeline = getUnifiedPipeline(gb, frontCW, (int)variant);

            Zelda3DUnified::UnifiedDrawUbo uu{};
            memcpy(uu.common.uMvp, base.uMP, sizeof(uu.common.uMvp));
            memcpy(uu.common.uMv, base.uMV, sizeof(uu.common.uMv));
            // Cycle 0 = texel0 * vColor0 (matches the old fixed shader's `t.rgb * vColor.rgb`); no
            // real per-material TEV data exists on the CMB side yet to derive a richer mux from.
            static const int32_t kCombA[16] = {
                /* cyc0 rgb */ 8 /*TEXEL0*/,
                0 /*0*/,
                1 /*INPUT_1*/,
                0,
                /* cyc0 a   */ 8,
                0,
                1,
                0,
                /* cyc1 rgb */ 0,
                0,
                0,
                0,
                /* cyc1 a   */ 0,
                0,
                0,
                0,
            };
            memcpy(uu.common.uCombA, kCombA, sizeof(uu.common.uCombA));
            uu.common.uPrimColor[0] = uu.common.uPrimColor[1] = uu.common.uPrimColor[2] = uu.common.uPrimColor[3] =
                1.0f;
            uu.common.uEnvColor[0] = uu.common.uEnvColor[1] = uu.common.uEnvColor[2] = uu.common.uEnvColor[3] = 0.0f;
            uu.common.uFogColor[0] = base.uFog[0];
            uu.common.uFogColor[1] = base.uFog[1];
            uu.common.uFogColor[2] = base.uFog[2];
            uu.common.uFogColor[3] = 0.0f;
            uu.common.uParams0[0] = grp.alphaTest ? grp.alphaRef : 0.0f;
            int lightingMode =
                (grp.vertexLighting && gZelda3dWorldLit && !forceUnlit) ? 2 : ((lit && !forceUnlit) ? 1 : 0);
            uu.common.uParams0[1] = (float)lightingMode;
            uu.common.uParams0[2] = 1.0f; // cycleCount — CMB never needs the N64 2-cycle shape
            uu.common.uParams0[3] = 0.0f; // frame_count — CMB draws don't use SHADER_NOISE
            uu.common.uParams1[0] = 0.0f; // noise_scale — unused (no SHADER_NOISE on CMB content)
            uu.common.uParams1[1] = grp.polygonOffset;
            uu.common.uParams1[2] = (boneData && boneCnt > 0) ? 1.0f : 0.0f;
            uu.common.uParams1[3] = 0.0f;
            // Adapt the authoritative native CMB payload instead of re-deriving actor/scene light
            // policy in the optional unified route.
            Zelda3DUnified::CopyCmbVertexLightBank(uu.common, ubo);
            // The legacy CMB UBO above is the authoritative material packer. Carry its complete
            // PICA state into the unified layout so generic-TEV materials execute the same staged
            // combiner, per-actor constant overrides, coordinator transforms, and alpha compare.
            // These fields are byte-compatible vec4/uvec4 blocks by construction (unified_ubo.h).
            memcpy(uu.common.uMatConst, ubo.uMatConst, sizeof(uu.common.uMatConst));
            memcpy(uu.common.uSheen, ubo.uSheen, sizeof(uu.common.uSheen));
            memcpy(uu.common.uTex1Xf, ubo.uTex1Xf, sizeof(uu.common.uTex1Xf));
            memcpy(uu.common.uFog3d0, ubo.uFog3d0, sizeof(uu.common.uFog3d0));
            memcpy(uu.common.uFog3d1, ubo.uFog3d1, sizeof(uu.common.uFog3d1));
            memcpy(uu.common.uSphRot0, ubo.uSphRot0, sizeof(uu.common.uSphRot0));
            memcpy(uu.common.uSphRot1, ubo.uSphRot1, sizeof(uu.common.uSphRot1));
            memcpy(uu.common.uSphRot2, ubo.uSphRot2, sizeof(uu.common.uSphRot2));
            memcpy(uu.common.uTevStages, ubo.uTevStages, sizeof(uu.common.uTevStages));
            memcpy(uu.common.uTevConst, ubo.uTevConst, sizeof(uu.common.uTevConst));
            memcpy(uu.common.uTex2Xf, ubo.uTex2Xf, sizeof(uu.common.uTex2Xf));
            memcpy(uu.common.uTevCtl, ubo.uTevCtl, sizeof(uu.common.uTevCtl));
            memcpy(uu.bones, base.uBones, sizeof(uu.bones));
            static_assert(sizeof(uu) == sizeof(SgUbo), "UnifiedDrawUbo must match DrawGroup::ubo's byte size");
            memcpy(dg.ubo.data(), &uu, sizeof(uu));
        } else {
            dg.pipeline = getPipeline(gb, frontCW);
            memcpy(dg.ubo.data(), &ubo, sizeof(ubo));
        }
        if (dg.pipeline)
            dgs.push_back(dg);
    }
    if (dgs.empty())
        return;

    SDL_GPUViewport vp{};
    SDL_Rect sc{};
    api->GetZelda3DViewportScissor(vp, sc);
    SDL_GPUBuffer* vbo = unified ? um->unifiedVbo : m->vbo;
    // Fragment sampler slot 1 (ex-shadow-map): now the generic-TEV third texture unit (uTex2);
    // dummy-backed for draws without one.
    SDL_GPUTexture* dummyTex = Fast::g_activeSdl3GpuApi->DummyTexture();
    SDL_GPUSampler* dummySamp = Fast::g_activeSdl3GpuApi->DummySampler();

    // Append each group as a FIRST-CLASS OP_DRAW in the unified op-list (no callback indirection):
    // each interleaves with the N64 geometry in this fb's render pass and replays through the backend's
    // single fragment-sampler bind path, exactly like an N64 triangle draw. Group order is preserved by
    // sequential append (matters for translucency).
    for (const DrawGroup& g : dgs) {
        const int drawIdx = g_sgDrawIdx++;
        if (gZelda3dSgDrawList) {
            fprintf(stderr,
                    "[Zelda3D_SG] draw %d model=%d group=%d material=%d first=%u count=%u dual=%d tev=%d "
                    "tex1idx=%d coord1=%d tex=%p tex1=%p tex2=%p\n",
                    drawIdx, modelId, g.model_group_index, g.material_index, g.first, g.count, g.dual_tex_mode,
                    g.tev_generic, g.tex1_index, g.coord1_mapping, (const void*)g.tex, (const void*)g.tex1,
                    (const void*)g.tex2);
        }
        if (!Zelda3D_SgDrawIsolationIncludes(modelId, drawIdx)) {
            continue; // draw-isolation probe: suppress groups excluded by the active controls
        }
        api->AppendZelda3DModelDraw(g.pipeline, vbo, g.first, g.count, g.ubo.data(), g.tex, g.samp,
                                    g.tex2 ? g.tex2 : dummyTex, g.samp2 ? g.samp2 : dummySamp, g.tex1, g.samp1, vp, sc,
                                    g.hasBlendConst, g.blendConst);
    }
}

void Fast::Zelda3DRenderer::EndPass() {
    g_ctxValid = false;
    // HARD-WARN rather than render an empty frame in silence. FRAGDBG spent weeks silently inert
    // because its injection anchor had been deleted, and "the probe shows nothing" is
    // indistinguishable from "the thing I am probing contributes nothing" — so an out-of-range
    // selection must say so.
    if (gZelda3dSgDrawOnly >= 0 && g_sgDrawIdx > 0 && gZelda3dSgDrawOnly >= g_sgDrawIdx) {
        fprintf(stderr, "[Zelda3D_SG] DRAWONLY=%d but this frame appended only %d group(s) — probe inert\n",
                gZelda3dSgDrawOnly, g_sgDrawIdx);
    }
    if (gZelda3dSgDrawSkip >= 0 && g_sgDrawIdx > 0 && gZelda3dSgDrawSkip >= g_sgDrawIdx) {
        fprintf(stderr, "[Zelda3D_SG] DRAWSKIP=%d but this frame appended only %d group(s) — probe inert\n",
                gZelda3dSgDrawSkip, g_sgDrawIdx);
    }
    // One-shot, but only once it has something to SHOW: the first frames after launch append zero
    // groups (the scene has not loaded), and an arm-at-launch that self-cleared on one of those
    // printed an empty list and then went quiet — the same silent-inertness failure FRAGDBG had.
    if (gZelda3dSgDrawList && g_sgDrawIdx > 0) {
        fprintf(stderr, "[Zelda3D_SG] draw list end: %d group(s) this frame\n", g_sgDrawIdx);
        gZelda3dSgDrawList = 0;
    }
    g_sgDrawIdx = 0;
}

#endif // ENABLE_SDL3GPU
