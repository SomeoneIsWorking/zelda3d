// Zelda3D SDL3GPU model, texture, sampler, and upload-cache ownership.
#ifdef ENABLE_SDL3GPU

#include "zelda3d_sdl3gpu_internal.h"

#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/zelda3d_sdl3gpu.h"
#include "fast/unified_vtx.h"
#include "fast/zelda3d_sampler.h"
#include "zelda3d_instrumentation_state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using Fast::SgGroup;
using Fast::SgModel;
using Fast::Zelda3DRenderer;

extern "C" int g_sgDumpModel;

namespace {

Zelda3DModelProvider g_provider = nullptr;
int g_sgDumpTexModel = []() {
    const char* value = getenv("ZELDA3D_SG_DUMPTEX");
    if (value == nullptr)
        return -1;
    if (strcmp(value, "all") == 0)
        return -2;
    return atoi(value);
}();
bool g_sgDumpTexAll = g_sgDumpTexModel == -2;
int g_sgDumpTexActual = g_sgDumpTexAll ? -1 : g_sgDumpTexModel;

SDL_GPUSamplerAddressMode wrapMode(unsigned glWrap) {
    switch (glWrap) {
        case 0x2900: // GL_CLAMP
        case 0x812F: // GL_CLAMP_TO_EDGE
            return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        case 0x8370: // GL_MIRRORED_REPEAT
            return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        default: // 0x2901 GL_REPEAT
            return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }
}

} // namespace

void Fast::Zelda3DSdl3GpuResources::SetModelProvider(Zelda3DModelProvider provider) {
    g_provider = provider;
}

bool Fast::Zelda3DSdl3GpuResources::ModelSource(int modelId, const Zelda3DGlGroup** groups, int* groupCount) {
    const Zelda3DGlTex* textures = nullptr;
    int textureCount = 0;
    return g_provider != nullptr && g_provider(modelId, groups, groupCount, &textures, &textureCount) != 0;
}

SDL_GPUSampler* Fast::Zelda3DRenderer::getSampler(unsigned minFilter, unsigned magFilter, unsigned wrapS,
                                                  unsigned wrapT) {
    const Zelda3DSamplerFilter filter = ResolveZelda3DSamplerFilter(minFilter, magFilter);
    const SDL_GPUFilter minification =
        filter.minification == Zelda3DTextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    const SDL_GPUFilter magnification =
        filter.magnification == Zelda3DTextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    SDL_GPUSamplerMipmapMode mipmap = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    if (filter.mipmap == Zelda3DMipmapFilter::Linear) {
        mipmap = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    }
    const float maxLod = filter.mipmap == Zelda3DMipmapFilter::None ? 0.0f : 1000.0f;
    return Fast::g_activeSdl3GpuApi->GetOrCreateSamplerEx(minification, magnification, mipmap, wrapMode(wrapS),
                                                          wrapMode(wrapT), maxLod);
}

SDL_GPUTexture* Fast::Zelda3DRenderer::uploadTexture(int w, int h, const unsigned char* rgba, int srcLevels) {
    if (w <= 0 || h <= 0)
        w = h = 1;
    // Full mip chain: without it, a repeating/detailed texture viewed at a grazing angle (e.g. a
    // room wall) aliases badly under plain bilinear sampling — the sampler already samples up to
    // max_lod=1000 (see getSampler), but with only 1 level present that has nothing to select.
    // COLOR_TARGET usage is needed alongside SAMPLER because SDL_GenerateMipmapsForGPUTexture
    // downsamples via blit passes, which write each level as a render target.
    // FALSIFIED LEAD (2026-07-22, render.kokiri-near-terrain-overbright): the synthetic chain was
    // the standing suspect for Kokiri's near-terrain +18% (a distance-dependent error with a
    // distance-dependent mechanism). It is NOT. Built with the chain disabled (mipLevels=1 AND
    // sampler max_lod=0 — max_lod=1000 over a single-level texture renders BLACK on this backend),
    // vanilla-on-vanilla at the matched camera, draw d8 moved 1.184 -> 1.181 over its 126682
    // exclusive pixels. Do not re-run this experiment.
    // Full chain length for this size (what the synthetic path needs).
    int fullLevels = 1;
    for (int m = (w > h ? w : h); m > 1; m >>= 1)
        fullLevels++;
    // AUTHORED chain (claim C018): 7284 of the ROM's 10538 textures ship their own mips, and we used
    // to discard them and box-filter our own. Use them when present. They are SHORT chains (3-4
    // levels), not full ones -- so num_levels is the authored count, and the sampler's max_lod=1000
    // clamps to it. That clamp is the risk: max_lod over a texture with ONE level renders BLACK on
    // this backend (recorded in the noMip note below), so a short-but->1 chain is only safe because
    // there is more than one level to select. srcLevels<=1 keeps the synthetic path unchanged.
    const bool authored = (srcLevels > 1 && rgba != nullptr);
    // Count both paths once so "the authored chain is in use" is a measurement, not an assumption.
    if (getenv("ZELDA3D_MIP_LOG")) {
        static int nAuth = 0, nSynth = 0;
        (authored ? nAuth : nSynth)++;
        if ((nAuth + nSynth) % 50 == 0 || nAuth + nSynth < 5)
            fprintf(stderr, "[Zelda3D_MIP] authored=%d synthetic=%d (this one %dx%d levels=%d)\n", nAuth, nSynth, w, h,
                    srcLevels);
    }
    int mipLevels = authored ? srcLevels : fullLevels;
    if (authored && mipLevels > fullLevels)
        mipLevels = fullLevels; // a chain longer than the size allows would be malformed
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = (uint32_t)mipLevels;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(g_device, &ci);
    if (tex == nullptr) {
        fprintf(stderr, "[Zelda3D_SG] CreateGPUTexture %dx%d FAILED: %s\n", w, h, SDL_GetError());
        return nullptr;
    }

    uint32_t size = (uint32_t)w * h * 4;
    if (authored) {
        size = 0;
        int lw = w, lh = h;
        for (int l = 0; l < mipLevels; l++) {
            size += (uint32_t)lw * lh * 4;
            lw = lw > 1 ? lw / 2 : 1;
            lh = lh > 1 ? lh / 2 : 1;
        }
    }
    static const unsigned char white[4] = { 255, 255, 255, 255 };
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = rgba ? size : 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
    if (tb == nullptr) {
        fprintf(stderr, "[Zelda3D_SG] CreateTransferBuffer %u bytes (%dx%d) FAILED: %s\n", tci.size, w, h,
                SDL_GetError());
        SDL_ReleaseGPUTexture(g_device, tex);
        return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(g_device, tb, false);
    if (mapped == nullptr) {
        fprintf(stderr, "[Zelda3D_SG] MapTransferBuffer %u bytes (%dx%d) FAILED: %s\n", tci.size, w, h, SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(g_device, tb);
        SDL_ReleaseGPUTexture(g_device, tex);
        return nullptr;
    }
    memcpy(mapped, rgba ? rgba : white, rgba ? size : 4);
    SDL_UnmapGPUTransferBuffer(g_device, tb);
    SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g_device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = tb;
    ti.pixels_per_row = (uint32_t)w;
    ti.rows_per_layer = (uint32_t)h;
    SDL_GPUTextureRegion reg{};
    reg.texture = tex;
    reg.w = (uint32_t)w;
    reg.h = (uint32_t)h;
    reg.d = 1;
    if (authored) {
        // Walk the concatenated chain, largest first.
        uint32_t off = 0;
        int lw = w, lh = h;
        for (int l = 0; l < mipLevels; l++) {
            ti.offset = off;
            ti.pixels_per_row = (uint32_t)lw;
            ti.rows_per_layer = (uint32_t)lh;
            reg.mip_level = (uint32_t)l;
            reg.w = (uint32_t)lw;
            reg.h = (uint32_t)lh;
            SDL_UploadToGPUTexture(cp, &ti, &reg, false);
            off += (uint32_t)lw * lh * 4;
            lw = lw > 1 ? lw / 2 : 1;
            lh = lh > 1 ? lh / 2 : 1;
        }
        SDL_EndGPUCopyPass(cp);
    } else {
        SDL_UploadToGPUTexture(cp, &ti, &reg, false);
        SDL_EndGPUCopyPass(cp);
        if (mipLevels > 1)
            SDL_GenerateMipmapsForGPUTexture(c, tex);
    }
    SDL_SubmitGPUCommandBuffer(c);
    SDL_ReleaseGPUTransferBuffer(g_device, tb);
    Zelda3DFast::ReportProgress();
    return tex;
}

SgModel* Fast::Zelda3DRenderer::ensureUploaded(int modelId) {
    SgModel& m = g_models[modelId];
    if (m.uploaded)
        return &m;
    if (m.failed)
        return nullptr;
    const Zelda3DGlGroup* groups = nullptr;
    const Zelda3DGlTex* texs = nullptr;
    int groupCount = 0, texCount = 0;
    if (!g_provider || !g_provider(modelId, &groups, &groupCount, &texs, &texCount) || groupCount <= 0) {
        fprintf(stderr, "[Zelda3D_SG] model %d unavailable from provider\n", modelId);
        m.failed = true;
        return nullptr;
    }

    std::vector<Zelda3DGlVtx> all;
    for (int i = 0; i < groupCount; i++) {
        SgGroup g;
        g.first = (uint32_t)all.size();
        g.count = (uint32_t)groups[i].vertCount;
        g.texIndex = groups[i].texIndex;
        g.alphaTest = groups[i].alphaTest;
        g.alphaRef = groups[i].alphaRef;
        g.alphaFunc = groups[i].alphaFunc;
        g.minFilter = groups[i].minFilter;
        g.magFilter = groups[i].magFilter;
        g.wrapS = groups[i].wrapS;
        g.wrapT = groups[i].wrapT;
        g.blendEnable = groups[i].blendEnable;
        g.bSrcRGB = groups[i].blendSrcRGB;
        g.bDstRGB = groups[i].blendDstRGB;
        g.bEqRGB = groups[i].blendEqRGB;
        g.bSrcA = groups[i].blendSrcA;
        g.bDstA = groups[i].blendDstA;
        g.bEqA = groups[i].blendEqA;
        for (int k = 0; k < 4; k++)
            g.blendColor[k] = groups[i].blendColor[k];
        g.depthWrite = groups[i].depthWrite;
        g.depthTest = groups[i].depthTest;
        g.depthFunc = groups[i].depthFunc;
        g.polygonOffset = groups[i].polygonOffset;
        g.cull = groups[i].cull;
        g.faceCull = groups[i].faceCull;
        g.meshId = groups[i].meshId;
        g.materialIndex = groups[i].materialIndex;
        g.vertexLighting = groups[i].vertexLighting;
        g.fragmentLighting = groups[i].fragmentLighting;
        g.hasColor = groups[i].hasColor;
        g.fogEnabled = groups[i].fogEnabled;
        g.combScaleRGB = groups[i].combScaleRGB;
        for (int k = 0; k < 3; k++) {
            g.matAmbient[k] = groups[i].matAmbient[k];
        }
        for (int k = 0; k < 4; k++) {
            g.matDiffuse[k] = groups[i].matDiffuse[k];
        }
        for (int s = 0; s < 6; s++)
            for (int k = 0; k < 4; k++)
                g.matConstant[s][k] = groups[i].matConstant[s][k];
        g.combConstIdx = groups[i].combConstIdx;
        g.combUsesConst = groups[i].combUsesConst;
        g.combConstScaleRGB = (groups[i].combConstScaleRGB > 0.0f) ? groups[i].combConstScaleRGB : 1.0f;
        g.dualTexMode = groups[i].dualTexMode;
        g.tevGeneric = groups[i].tevGeneric;
        g.uv0Scale[0] = groups[i].uv0Scale[0];
        g.uv0Scale[1] = groups[i].uv0Scale[1];
        g.uv0Trans[0] = groups[i].uv0Trans[0];
        g.uv0Trans[1] = groups[i].uv0Trans[1];
        g.coord0Mapping = groups[i].coord0Mapping;
        if (g.dualTexMode || g.tevGeneric) {
            g.dualTexScale2 = groups[i].dualTexScale2;
            g.tex1Index = groups[i].tex1Index;
            g.min1Filter = groups[i].min1Filter;
            g.mag1Filter = groups[i].mag1Filter;
            g.wrap1S = groups[i].wrap1S;
            g.wrap1T = groups[i].wrap1T;
            g.uv1Scale[0] = groups[i].uv1Scale[0];
            g.uv1Scale[1] = groups[i].uv1Scale[1];
            g.uv1Trans[0] = groups[i].uv1Trans[0];
            g.uv1Trans[1] = groups[i].uv1Trans[1];
            g.coord1Mapping = groups[i].coord1Mapping;
        }
        // Generic per-stage TEV chain (render.multi-stage-tev).
        if (g.tevGeneric) {
            g.tevStageCount = groups[i].tevStageCount;
            for (int s = 0; s < 6; s++)
                for (int k = 0; k < 3; k++)
                    g.tevStagePack[s][k] = groups[i].tevStagePack[s][k];
            g.tex2Index = groups[i].tex2Index;
            g.min2Filter = groups[i].min2Filter;
            g.mag2Filter = groups[i].mag2Filter;
            g.wrap2S = groups[i].wrap2S;
            g.wrap2T = groups[i].wrap2T;
            g.uv2Scale[0] = groups[i].uv2Scale[0];
            g.uv2Scale[1] = groups[i].uv2Scale[1];
            g.uv2Trans[0] = groups[i].uv2Trans[0];
            g.uv2Trans[1] = groups[i].uv2Trans[1];
            g.coord2Mapping = groups[i].coord2Mapping;
        }
        if (groups[i].vertCount > 0) {
            for (int k = 0; k < 4; k++)
                g.dbgColor0[k] = groups[i].verts[0].color[k];
            uint32_t vc = groups[i].vertCount;
            for (int k = 0; k < 2; k++) {
                g.dbgUv0[k] = groups[i].verts[0].uv[k];
                g.dbgUv1[k] = groups[i].verts[vc / 2].uv[k];
                g.dbgUv2[k] = groups[i].verts[vc - 1].uv[k];
            }
        }
        if (groups[i].vertCount > 0 && groups[i].verts != nullptr) {
            for (int k = 0; k < 3; k++)
                g.gmin[k] = g.gmax[k] = groups[i].verts[0].pos[k];
            for (int vi = 1; vi < groups[i].vertCount; vi++) {
                for (int k = 0; k < 3; k++) {
                    const float c = groups[i].verts[vi].pos[k];
                    if (c < g.gmin[k])
                        g.gmin[k] = c;
                    if (c > g.gmax[k])
                        g.gmax[k] = c;
                }
            }
            g.hasGroupBounds = true;
        }
        all.insert(all.end(), groups[i].verts, groups[i].verts + groups[i].vertCount);
        m.groups.push_back(g);
    }

    // Model-local AABB over all vertices (geomscan; see Zelda3D_GeomScanDump).
    if (!all.empty()) {
        for (int k = 0; k < 3; k++)
            m.localMin[k] = m.localMax[k] = all[0].pos[k];
        for (const Zelda3DGlVtx& v : all) {
            for (int k = 0; k < 3; k++) {
                if (v.pos[k] < m.localMin[k])
                    m.localMin[k] = v.pos[k];
                if (v.pos[k] > m.localMax[k])
                    m.localMax[k] = v.pos[k];
            }
        }
        m.hasBounds = true;
    }

    // Device vertex buffer via a transfer-buffer copy.
    const uint32_t vbBytes = (uint32_t)(all.size() * sizeof(Zelda3DGlVtx));
    if (vbBytes > 0) {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = vbBytes;
        m.vbo = SDL_CreateGPUBuffer(g_device, &bci);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = vbBytes;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
        void* mapped = SDL_MapGPUTransferBuffer(g_device, tb, false);
        memcpy(mapped, all.data(), vbBytes);
        SDL_UnmapGPUTransferBuffer(g_device, tb);
        SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g_device);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = tb;
        SDL_GPUBufferRegion dst{};
        dst.buffer = m.vbo;
        dst.size = vbBytes;
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(c);
        SDL_ReleaseGPUTransferBuffer(g_device, tb);
    }

    for (int i = 0; i < texCount; i++) {
        m.textures.push_back(uploadTexture(texs[i].w, texs[i].h, texs[i].rgba, texs[i].levels));
        if (modelId == g_sgDumpModel || g_sgDumpTexActual == modelId || g_sgDumpTexAll) {
            if ((g_sgDumpTexActual == modelId || g_sgDumpTexAll) && texs[i].rgba) {
                // One-off raw-pixel dump (PPM, no library needed) so the SOURCE texel data can be
                // eyeballed directly, bypassing the whole render/sampler pipeline.
                char path[256];
                snprintf(path, sizeof(path), "scratch/sgtex_%d_%d.ppm", modelId, i);
                FILE* pf = fopen(path, "wb");
                if (pf) {
                    fprintf(pf, "P6\n%d %d\n255\n", texs[i].w, texs[i].h);
                    for (long p = 0; p < (long)texs[i].w * texs[i].h; p++)
                        fwrite(&texs[i].rgba[p * 4], 1, 3, pf);
                    fclose(pf);
                }
            }
            // Mean RGBA of the source texels (sgdump diagnostics: is the texture itself black?).
            const unsigned char* px = texs[i].rgba;
            long n = (long)texs[i].w * texs[i].h, sr = 0, sg = 0, sb = 0, sa = 0;
            if (px && n > 0)
                for (long p = 0; p < n; p++) {
                    sr += px[p * 4 + 0];
                    sg += px[p * 4 + 1];
                    sb += px[p * 4 + 2];
                    sa += px[p * 4 + 3];
                }
            // GPU readback of the just-uploaded texture: copy it to a download transfer buffer, wait,
            // map, and mean it. If this differs from srcMean, the UPLOAD is broken (not the sample).
            long gr = -1, gg = -1, gb = -1, ga = -1;
            SDL_GPUTexture* gtex = m.textures.back();
            if (gtex && n > 0) {
                SDL_GPUTransferBufferCreateInfo dci{};
                dci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                dci.size = (uint32_t)(n * 4);
                SDL_GPUTransferBuffer* dl = SDL_CreateGPUTransferBuffer(g_device, &dci);
                SDL_GPUCommandBuffer* dc = SDL_AcquireGPUCommandBuffer(g_device);
                SDL_GPUCopyPass* dcp = SDL_BeginGPUCopyPass(dc);
                SDL_GPUTextureRegion dreg{};
                dreg.texture = gtex;
                dreg.w = texs[i].w;
                dreg.h = texs[i].h;
                dreg.d = 1;
                SDL_GPUTextureTransferInfo dti{};
                dti.transfer_buffer = dl;
                dti.pixels_per_row = texs[i].w;
                dti.rows_per_layer = texs[i].h;
                SDL_DownloadFromGPUTexture(dcp, &dreg, &dti);
                SDL_EndGPUCopyPass(dcp);
                SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(dc);
                if (f) {
                    SDL_WaitForGPUFences(g_device, true, &f, 1);
                    SDL_ReleaseGPUFence(g_device, f);
                }
                const unsigned char* gp = (const unsigned char*)SDL_MapGPUTransferBuffer(g_device, dl, false);
                if (gp) {
                    long r2 = 0, g2 = 0, b2 = 0, a2 = 0;
                    for (long p = 0; p < n; p++) {
                        r2 += gp[p * 4 + 0];
                        g2 += gp[p * 4 + 1];
                        b2 += gp[p * 4 + 2];
                        a2 += gp[p * 4 + 3];
                    }
                    gr = r2 / n;
                    gg = g2 / n;
                    gb = b2 / n;
                    ga = a2 / n;
                    SDL_UnmapGPUTransferBuffer(g_device, dl);
                }
                SDL_ReleaseGPUTransferBuffer(g_device, dl);
            }
            fprintf(stderr, "[SG_DUMP] tex%-2d %dx%d srcMeanRGBA=(%ld,%ld,%ld,%ld) gpuMeanRGBA=(%ld,%ld,%ld,%ld) %s\n",
                    i, texs[i].w, texs[i].h, n ? sr / n : -1, n ? sg / n : -1, n ? sb / n : -1, n ? sa / n : -1, gr, gg,
                    gb, ga, px ? "" : "(null rgba!)");
        }
    }

    m.uploaded = true;
    fprintf(stderr, "[Zelda3D_SG] uploaded model %d: %d groups, %d textures, %zu verts\n", modelId, groupCount,
            texCount, all.size());
    Zelda3DFast::ReportProgress();
    return &m;
}

// ---------------------------------------------------------------------------
// Render-unification effort (kanban #131), Phase 2: CMB -> UnifiedVtx/UnifiedMaterial packers +
// the unified vertex buffer / pipeline builders. Only reached when gUnifiedRenderer & 1 (default
// off) — the old ensureUploaded/getPipeline path above is completely untouched.
// ---------------------------------------------------------------------------
namespace {

UnifiedVtx PackUnifiedVtx(const Zelda3DGlVtx& v, float combScaleRGB) {
    UnifiedVtx u{};
    // w=1.0: CMB is model-space, GPU-transformed via uMvp (alreadyTransformed=false) — see
    // unified_vtx.h's pos field doc.
    u.pos[0] = v.pos[0];
    u.pos[1] = v.pos[1];
    u.pos[2] = v.pos[2];
    u.pos[3] = 1.0f;
    u.nrm[0] = v.nrm[0];
    u.nrm[1] = v.nrm[1];
    u.nrm[2] = v.nrm[2];
    u.uv0[0] = v.uv[0];
    u.uv0[1] = v.uv[1];
    u.uv1[0] = v.uv1[0];
    u.uv1[1] = v.uv1[1];
    u.uv2[0] = v.uv2[0];
    u.uv2[1] = v.uv2[1];
    // No per-vertex clamp data on the CMB side (unlike N64) — a large no-op upper bound so the
    // unified fragment shader's clamp(uv, 0.5/texSize, texClamp) never actually clamps.
    u.texClamp[0] = 1e6f;
    u.texClamp[1] = 1e6f;
    u.texClamp[2] = 1e6f;
    u.texClamp[3] = 1e6f;
    // Stage-0 TEV RGB scale (comb_scale_rgb, cmb.h) folded in here since the combiner mux only has
    // room for a two-operand multiply (texel0 * vColor0) — Kokiri grass etc. MODULATE at x2/x4.
    for (int k = 0; k < 3; k++)
        u.color0[k] = (uint8_t)std::lround(std::clamp(v.color[k] * combScaleRGB, 0.0f, 1.0f) * 255.0f);
    u.color0[3] = (uint8_t)std::lround(std::clamp(v.color[3], 0.0f, 1.0f) * 255.0f);
    for (int k = 0; k < 4; k++)
        u.color1[k] = u.color2[k] = u.color3[k] = 0;
    u.fog[0] = 0.0f;
    u.fog[1] = 0.0f;
    for (int k = 0; k < 4; k++) {
        u.boneIds[k] = (uint8_t)std::clamp((int)std::lround(v.boneIds[k]), 0, 255);
        u.boneW[k] = (uint8_t)std::lround(std::clamp(v.weights[k], 0.0f, 1.0f) * 255.0f);
    }
    return u;
}

} // namespace

Fast::SgModel* Fast::Zelda3DRenderer::ensureUnifiedUploaded(int modelId) {
    SgModel* base = ensureUploaded(modelId); // populates m.groups/textures if not already
    if (!base)
        return nullptr;
    SgModel& m = *base;
    if (m.unifiedUploaded)
        return &m;
    if (m.unifiedFailed)
        return nullptr;

    const Zelda3DGlGroup* groups = nullptr;
    const Zelda3DGlTex* texs = nullptr;
    int groupCount = 0, texCount = 0;
    if (!g_provider || !g_provider(modelId, &groups, &groupCount, &texs, &texCount) || groupCount <= 0) {
        m.unifiedFailed = true;
        return nullptr;
    }

    std::vector<UnifiedVtx> all;
    for (int i = 0; i < groupCount; i++) {
        // combScaleRGB is the CMB material's authored TEV stage-0 RGB scale (Kokiri grass ×2, etc)
        // — a static material property that runs unconditionally on the 3DS regardless of PICA
        // fragment-lighting state. Apply it whenever the material declares vertexLighting=1; do
        // NOT gate on gZelda3dWorldLit here, which only controls the ambient+diffuse*NdotL
        // computation (task #16: at title, that compute is off but combScaleRGB must remain).
        float scale = groups[i].vertexLighting ? groups[i].combScaleRGB : 1.0f;
        for (uint32_t k = 0; k < groups[i].vertCount; k++)
            all.push_back(PackUnifiedVtx(groups[i].verts[k], scale));
    }

    const uint32_t vbBytes = (uint32_t)(all.size() * sizeof(UnifiedVtx));
    if (vbBytes > 0) {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = vbBytes;
        m.unifiedVbo = SDL_CreateGPUBuffer(g_device, &bci);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = vbBytes;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(g_device, &tci);
        void* mapped = SDL_MapGPUTransferBuffer(g_device, tb, false);
        memcpy(mapped, all.data(), vbBytes);
        SDL_UnmapGPUTransferBuffer(g_device, tb);
        SDL_GPUCommandBuffer* c = SDL_AcquireGPUCommandBuffer(g_device);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(c);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = tb;
        SDL_GPUBufferRegion dst{};
        dst.buffer = m.unifiedVbo;
        dst.size = vbBytes;
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(c);
        SDL_ReleaseGPUTransferBuffer(g_device, tb);
    }
    m.unifiedUploaded = true;
    fprintf(stderr, "[Zelda3D_SG] unified-uploaded model %d: %zu verts\n", modelId, all.size());
    return &m;
}

void Fast::Zelda3DRenderer::applyPendingEvict() {
    if (!g_evictPending || !g_device)
        return;
    g_evictPending = false;
    SDL_WaitForGPUIdle(g_device);
    for (auto it = g_models.begin(); it != g_models.end();) {
        if (it->first >= g_evictLo && it->first < g_evictHi) {
            SgModel& m = it->second;
            for (auto* t : m.textures)
                if (t)
                    SDL_ReleaseGPUTexture(g_device, t);
            if (m.vbo)
                SDL_ReleaseGPUBuffer(g_device, m.vbo);
            it = g_models.erase(it);
        } else {
            ++it;
        }
    }
}

#endif // ENABLE_SDL3GPU
