#include "cmb_glgroups.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "cityhash.h"
#include "pica_texture.h"
#include "texpack.h"

namespace Zelda3D {

// CmbVertex and Zelda3DGlVtx MUST be layout-identical: MakeGlGroup aliases the CMB's
// vertex array straight into the renderer's vertex pointer. Guard it here so any
// drift in either struct is a compile error, not a silent garbage-render.
static_assert(sizeof(CmbVertex) == sizeof(Zelda3DGlVtx), "CmbVertex/Zelda3DGlVtx size mismatch");
static_assert(offsetof(CmbVertex, nrm) == offsetof(Zelda3DGlVtx, nrm), "nrm offset mismatch");
static_assert(offsetof(CmbVertex, uv) == offsetof(Zelda3DGlVtx, uv), "uv offset mismatch");
static_assert(offsetof(CmbVertex, boneIds) == offsetof(Zelda3DGlVtx, boneIds), "boneIds offset mismatch");
static_assert(offsetof(CmbVertex, weights) == offsetof(Zelda3DGlVtx, weights), "weights offset mismatch");
static_assert(offsetof(CmbVertex, color) == offsetof(Zelda3DGlVtx, color), "color offset mismatch");

Zelda3DGlGroup MakeGlGroup(const Cmb& cmb, const CmbDrawGroup& g, const CmbVertex* srcVerts, int texBase) {
    const CmbMaterial* mat =
        (g.material_index >= 0 && g.material_index < (int)cmb.materials().size()) ? &cmb.materials()[g.material_index]
                                                                                  : nullptr;
    Zelda3DGlGroup cg{};
    cg.verts = reinterpret_cast<const Zelda3DGlVtx*>(srcVerts);
    cg.vertCount = (int)g.verts.size();
    cg.texIndex = cmb.materialTexture(g.material_index) + texBase;
    cg.alphaTest = mat && mat->alpha_test ? 1 : 0;
    cg.alphaRef = mat ? mat->alpha_ref : 0.0f;
    cg.wrapS = mat ? mat->wrap_s : 0x2901;
    cg.wrapT = mat ? mat->wrap_t : 0x2901;
    cg.blendEnable = mat && mat->blend_enable ? 1 : 0;
    cg.blendSrcRGB = mat ? mat->blend_src_rgb : 0x0302;
    cg.blendDstRGB = mat ? mat->blend_dst_rgb : 0x0303;
    cg.blendEqRGB = mat ? mat->blend_eq_rgb : 0x8006;
    cg.blendSrcA = mat ? mat->blend_src_a : 0x0001;
    cg.blendDstA = mat ? mat->blend_dst_a : 0x0000;
    cg.blendEqA = mat ? mat->blend_eq_a : 0x8006;
    cg.depthWrite = mat ? (mat->depth_write ? 1 : 0) : 1;
    cg.polygonOffset = mat ? mat->polygon_offset : 0.0f;
    // OoT3D backface culling: cull byte 1 = single-sided (cull back), 3 = double-sided.
    // Honor it so the renderer matches N64 G_CULL_BACK (don't show terrain undersides /
    // mesh interiors). Only value 1 culls; everything else (3, none) draws both sides.
    cg.faceCull = (mat && mat->cull == 1) ? 1 : 0;
    cg.meshId = g.mesh_id;
    cg.materialIndex = g.material_index; // key for the facial eye/mouth texture-override channel
    for (int k = 0; k < 4; k++) cg.blendColor[k] = mat ? mat->blend_color[k] : (k == 3 ? 1.0f : 0.0f);
    // OoT3D world lighting/combiner port (docs/oot3d_world_lighting_re.md).
    cg.vertexLighting = (mat && mat->vertex_lighting) ? 1 : 0;
    cg.combScaleRGB = mat ? mat->comb_scale_rgb : 1.0f;
    for (int k = 0; k < 3; k++) {
        cg.matAmbient[k] = mat ? mat->mat_ambient[k] : 1.0f;
        cg.matDiffuse[k] = mat ? mat->mat_diffuse[k] : 1.0f;
    }
    // PICA200 TEV constant palette + stage-0 selector. Base defaults from the CMB file; the
    // per-actor override channel (Zelda3D_GL_SetMatConstOverride in Step 2c) rewrites the
    // affected slot(s) before submit for actors like EnHy townsfolk.
    for (int s = 0; s < 6; s++) {
        for (int k = 0; k < 4; k++) {
            cg.matConstant[s][k] = mat ? mat->mat_constant[s][k] : (k == 3 ? 1.0f : 0.0f);
        }
    }
    cg.combConstIdx = mat ? mat->comb_const_idx : 0;
    if (getenv("ZELDA3D_DBG_MAT")) {
        fprintf(stderr, "[MAT] mi=%d vlit=%d comb=%.1f amb=(%.2f,%.2f,%.2f) dif=(%.2f,%.2f,%.2f) "
                        "constIdx=%d const%d=(%.2f,%.2f,%.2f,%.2f)\n",
                g.material_index, cg.vertexLighting, cg.combScaleRGB, cg.matAmbient[0], cg.matAmbient[1],
                cg.matAmbient[2], cg.matDiffuse[0], cg.matDiffuse[1], cg.matDiffuse[2], cg.combConstIdx,
                cg.combConstIdx, cg.matConstant[cg.combConstIdx][0], cg.matConstant[cg.combConstIdx][1],
                cg.matConstant[cg.combConstIdx][2], cg.matConstant[cg.combConstIdx][3]);
    }
    return cg;
}

int AppendCmbTextures(const Cmb& cmb, std::vector<std::vector<uint8_t>>& texRgba,
                      std::vector<std::pair<int, int>>& dims) {
    int base = (int)texRgba.size();
    const auto& texs = cmb.textures();
    for (const auto& t : texs) {
        auto raw = cmb.textureRaw(t);
        int w = t.width, h = t.height;
        std::vector<uint8_t> rgba;
        // Look up a hi-res replacement by the texture's Citra legacy hash.
        auto lb = Zelda3D::PicaLegacyHashBytes(t.glFormat(), t.width, t.height, raw);
        uint64_t hash = lb.empty() ? 0 : Zelda3D::CityHash64(reinterpret_cast<const char*>(lb.data()), lb.size());
        if (hash == 0 || !Zelda3D::TexPackLookup(hash, w, h, rgba)) {
            w = t.width; h = t.height;
            rgba = Zelda3D::PicaDecode(t.glFormat(), t.width, t.height, raw);
        }
        texRgba.push_back(std::move(rgba));
        dims.push_back({ w, h });
    }
    return base;
}

} // namespace Zelda3D
