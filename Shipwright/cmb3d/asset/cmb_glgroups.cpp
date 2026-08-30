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
static_assert(offsetof(CmbVertex, uv1) == offsetof(Zelda3DGlVtx, uv1), "uv1 offset mismatch");
static_assert(offsetof(CmbVertex, uv2) == offsetof(Zelda3DGlVtx, uv2), "uv2 offset mismatch");

// --- Generic TEV packing (render.multi-stage-tev) -------------------------------------------
// GL-DMP enum -> PICA hardware code translation, done once at group build so the shader decodes
// small fixed-width fields. Enum domains validated over the whole ROM corpus
// (tools/tev_corpus_survey.py, 2026-07-22: zero violations).
static unsigned TevSrcCode(uint16_t gl) {
    switch (gl) {
        case 0x8577:
            return 0; // PRIMARY_COLOR (vertex-lit output color)
        case 0x6210:
            return 1; // FRAGMENT_PRIMARY_COLOR_DMP (fragment lighting)
        case 0x6211:
            return 2; // FRAGMENT_SECONDARY_COLOR_DMP
        case 0x84C0:
            return 3; // TEXTURE0
        case 0x84C1:
            return 4; // TEXTURE1
        case 0x84C2:
            return 5; // TEXTURE2
        case 0x84C3:
            return 6; // TEXTURE3 (unsupported unit; shader falls back to TEXTURE0)
        case 0x8579:
            return 13; // PREVIOUS_BUFFER_DMP
        case 0x8576:
            return 14; // CONSTANT
        case 0x8578:
            return 15; // PREVIOUS
        default:
            return 0;
    }
}
static unsigned TevColorModCode(uint16_t gl) {
    switch (gl) {
        case 0x0300:
            return 0; // SRC_COLOR
        case 0x0301:
            return 1; // 1 - SRC_COLOR
        case 0x0302:
            return 2; // SRC_ALPHA
        case 0x0303:
            return 3; // 1 - SRC_ALPHA
        case 0x8580:
            return 4; // SRC_R
        case 0x8581:
            return 5; // 1 - SRC_R
        case 0x8582:
            return 8; // SRC_G
        case 0x8583:
            return 9; // 1 - SRC_G
        case 0x8584:
            return 12; // SRC_B
        case 0x8585:
            return 13; // 1 - SRC_B
        default:
            return 0;
    }
}
static unsigned TevAlphaModCode(uint16_t gl) {
    switch (gl) {
        case 0x0302:
            return 0; // SRC_ALPHA
        case 0x0303:
            return 1; // 1 - SRC_ALPHA
        case 0x8580:
            return 2; // SRC_R
        case 0x8581:
            return 3;
        case 0x8582:
            return 4; // SRC_G
        case 0x8583:
            return 5;
        case 0x8584:
            return 6; // SRC_B
        case 0x8585:
            return 7;
        default:
            return 0; // 0x0300 SRC_COLOR is meaningless in the alpha chain -> alpha
    }
}
static unsigned TevOpCode(uint16_t gl) {
    switch (gl) {
        case 0x1E01:
            return 0; // REPLACE
        case 0x2100:
            return 1; // MODULATE
        case 0x0104:
            return 2; // ADD
        case 0x8574:
            return 3; // ADD_SIGNED
        case 0x8575:
            return 4; // INTERPOLATE (Lerp)
        case 0x84E7:
            return 5; // SUBTRACT
        case 0x86AE:
            return 6; // DOT3_RGB
        case 0x86AF:
            return 7; // DOT3_RGBA
        case 0x6401:
            return 8; // MULT_ADD  ((a*b)+c)
        case 0x6402:
            return 9; // ADD_MULT  (clamp(a+b)*c)
        default:
            return 1;
    }
}
static unsigned TevScaleLog2(uint16_t s) {
    return s == 4 ? 2u : (s == 2 ? 1u : 0u);
}
static void PackTevStage(const CmbMaterial::CombStage& cs, unsigned out[3]) {
    // PICA leaves a four-bit gap between the RGB and alpha source triplets: alpha starts at
    // bit 16, unlike the modifier word where alpha starts at bit 12. This mirrors Azahar's
    // TevStageConfig (alpha_source1/2/3 = bits 16/20/24); packing alpha at 12/16/20 makes
    // the generic evaluator read a different source whenever the alpha chain is consumed.
    out[0] = TevSrcCode(cs.rgb_src[0]) | (TevSrcCode(cs.rgb_src[1]) << 4) | (TevSrcCode(cs.rgb_src[2]) << 8) |
             (TevSrcCode(cs.a_src[0]) << 16) | (TevSrcCode(cs.a_src[1]) << 20) | (TevSrcCode(cs.a_src[2]) << 24);
    out[1] = TevColorModCode(cs.rgb_mod[0]) | (TevColorModCode(cs.rgb_mod[1]) << 4) |
             (TevColorModCode(cs.rgb_mod[2]) << 8) | (TevAlphaModCode(cs.a_mod[0]) << 12) |
             (TevAlphaModCode(cs.a_mod[1]) << 16) | (TevAlphaModCode(cs.a_mod[2]) << 20) |
             ((unsigned)(cs.const_idx & 7) << 24);
    out[2] = TevOpCode(cs.rgb_op) | (TevOpCode(cs.a_op) << 4) | (TevScaleLog2(cs.rgb_scale) << 8) |
             (TevScaleLog2(cs.a_scale) << 10) |
             // Combiner-buffer latch, bits 12/13. 0x8578 PREVIOUS = this stage latches; 0x8579
             // PREVIOUS_BUFFER = leave the buffer alone. See the shift note in the shader's
             // tevRun: the CMB flag sits on the stage the PICA register NAMES (3dbrew labels the
             // GPUREG_TEXENV_UPDATE_BUFFER bits "TEV stage 1..4"), which is one AHEAD of Azahar's
             // 0-based update-mask bit, so the evaluator applies it to stage-1.
             ((cs.buf_rgb == 0x8578 ? 1u : 0u) << 12) | ((cs.buf_a == 0x8578 ? 1u : 0u) << 13);
}

Zelda3DGlGroup MakeGlGroup(const Cmb& cmb, const CmbDrawGroup& g, const CmbVertex* srcVerts, int texBase) {
    const CmbMaterial* mat = (g.material_index >= 0 && g.material_index < (int)cmb.materials().size())
                                 ? &cmb.materials()[g.material_index]
                                 : nullptr;
    Zelda3DGlGroup cg{};
    cg.verts = reinterpret_cast<const Zelda3DGlVtx*>(srcVerts);
    cg.vertCount = (int)g.verts.size();
    cg.texIndex = cmb.materialTexture(g.material_index) + texBase;
    cg.alphaTest = mat && mat->alpha_test ? 1 : 0;
    cg.alphaRef = mat ? mat->alpha_ref : 0.0f;
    cg.alphaFunc = mat ? mat->alpha_func : 0x0206;
    cg.minFilter = mat ? mat->min_filter : 0x2601;
    cg.magFilter = mat ? mat->mag_filter : 0x2601;
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
    cg.depthTest = mat ? (mat->depth_test ? 1 : 0) : 1;
    cg.depthFunc = mat ? mat->depth_func : 0x0201;
    cg.polygonOffset = mat ? mat->polygon_offset : 0.0f;
    // OoT3D backface culling: cull byte 1 = single-sided (cull back), 3 = double-sided.
    // Honor it so the renderer matches N64 G_CULL_BACK (don't show terrain undersides /
    // mesh interiors). Only value 1 culls; everything else (3, none) draws both sides.
    cg.faceCull = (mat && mat->cull == 1) ? 1 : 0;
    cg.meshId = g.mesh_id;
    cg.materialIndex = g.material_index; // key for the facial eye/mouth texture-override channel
    for (int k = 0; k < 4; k++)
        cg.blendColor[k] = mat ? mat->blend_color[k] : (k == 3 ? 1.0f : 0.0f);
    // OoT3D world lighting/combiner port (docs/oot3d_world_lighting_re.md).
    cg.vertexLighting = (mat && mat->vertex_lighting) ? 1 : 0;
    cg.fragmentLighting = (mat && mat->fragment_lighting) ? 1 : 0;
    cg.hasColor = g.has_color ? 1 : 0;
    cg.fogEnabled = (mat && mat->is_fog) ? 1 : 0; // PICA distance fog participates (fog_mode=5)
    cg.combScaleRGB = mat ? mat->comb_scale_rgb : 1.0f;
    for (int k = 0; k < 3; k++) {
        cg.matAmbient[k] = mat ? mat->mat_ambient[k] : 1.0f;
    }
    for (int k = 0; k < 4; k++) {
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
    cg.combUsesConst = (mat && mat->comb_uses_const) ? 1 : 0;
    cg.combConstScaleRGB = mat ? mat->comb_const_scale_rgb : 1.0f;
    // Dual-texture combine: second binding + coordinator-1 transform. dual_tex_mode is a
    // byte-driven classification of the material's own combiner stages (cmb.cpp parseMats) —
    // NOT gated by model name (see CmbMaterial::DualTexMode).
    cg.dualTexMode = mat ? mat->dual_tex_mode : 0;
    cg.dualTexScale2 = mat ? mat->dual_tex_scale2 : 1.0f;
    cg.uv0Scale[0] = mat ? mat->scale_s : 1.0f;
    cg.uv0Scale[1] = mat ? mat->scale_t : 1.0f;
    cg.uv0Trans[0] = mat ? mat->trans_s : 0.0f;
    cg.uv0Trans[1] = mat ? mat->trans_t : 0.0f;
    cg.coord0Mapping = mat ? mat->coord0_mapping : 1;
    cg.tex1Index = (mat && mat->tex1_idx >= 0) ? mat->tex1_idx + texBase : -1;
    cg.min1Filter = mat ? mat->min1_filter : 0x2601;
    cg.mag1Filter = mat ? mat->mag1_filter : 0x2601;
    cg.wrap1S = mat ? mat->wrap1_s : 0x2901;
    cg.wrap1T = mat ? mat->wrap1_t : 0x2901;
    cg.uv1Scale[0] = mat ? mat->scale1_s : 1.0f;
    cg.uv1Scale[1] = mat ? mat->scale1_t : 1.0f;
    cg.uv1Trans[0] = mat ? mat->trans1_s : 0.0f;
    cg.uv1Trans[1] = mat ? mat->trans1_t : 0.0f;
    cg.coord1Mapping = mat ? mat->coord1_mapping : 1;
    // Generic per-stage TEV chain (render.multi-stage-tev): packed shader words + the third
    // texture binding / coordinator-2 transform. tev_generic and dual_tex_mode are mutually
    // exclusive by construction (cmb.cpp parseMats).
    cg.tevGeneric = (mat && mat->tev_generic) ? 1 : 0;
    cg.tevStageCount = mat ? mat->comb_stage_count : 0;
    if (cg.tevStageCount > 6)
        cg.tevStageCount = 6;
    for (int s = 0; s < cg.tevStageCount; s++)
        PackTevStage(mat->comb_stages[s], cg.tevStagePack[s]);
    cg.tex2Index = (mat && mat->tex2_idx >= 0) ? mat->tex2_idx + texBase : -1;
    cg.min2Filter = mat ? mat->min2_filter : 0x2601;
    cg.mag2Filter = mat ? mat->mag2_filter : 0x2601;
    cg.wrap2S = mat ? mat->wrap2_s : 0x2901;
    cg.wrap2T = mat ? mat->wrap2_t : 0x2901;
    cg.uv2Scale[0] = mat ? mat->scale2_s : 1.0f;
    cg.uv2Scale[1] = mat ? mat->scale2_t : 1.0f;
    cg.uv2Trans[0] = mat ? mat->trans2_s : 0.0f;
    cg.uv2Trans[1] = mat ? mat->trans2_t : 0.0f;
    cg.coord2Mapping = mat ? mat->coord2_mapping : 1;
    if (getenv("ZELDA3D_DBG_MAT")) {
        fprintf(stderr,
                "[MAT] mi=%d vlit=%d comb=%.1f amb=(%.2f,%.2f,%.2f) dif=(%.2f,%.2f,%.2f) "
                "constIdx=%d const%d=(%.2f,%.2f,%.2f,%.2f)\n",
                g.material_index, cg.vertexLighting, cg.combScaleRGB, cg.matAmbient[0], cg.matAmbient[1],
                cg.matAmbient[2], cg.matDiffuse[0], cg.matDiffuse[1], cg.matDiffuse[2], cg.combConstIdx,
                cg.combConstIdx, cg.matConstant[cg.combConstIdx][0], cg.matConstant[cg.combConstIdx][1],
                cg.matConstant[cg.combConstIdx][2], cg.matConstant[cg.combConstIdx][3]);
    }
    return cg;
}

int AppendCmbTextures(const Cmb& cmb, std::vector<std::vector<uint8_t>>& texRgba,
                      std::vector<std::pair<int, int>>& dims, std::vector<int>& texLevels) {
    int base = (int)texRgba.size();
    const auto& texs = cmb.textures();
    for (const auto& t : texs) {
        auto raw = cmb.textureRaw(t);
        int w = t.width, h = t.height;
        std::vector<uint8_t> rgba;
        // Look up a hi-res replacement by the texture's Citra legacy hash.
        auto lb = Zelda3D::PicaLegacyHashBytes(t.glFormat(), t.width, t.height, raw);
        uint64_t hash = lb.empty() ? 0 : Zelda3D::CityHash64(reinterpret_cast<const char*>(lb.data()), lb.size());
        int levels = 1;
        if (hash == 0 || !Zelda3D::TexPackLookup(hash, w, h, rgba)) {
            w = t.width;
            h = t.height;
            // Use the texture's AUTHORED mip chain when it has one (7284 of the ROM's 10538 do --
            // claim C018). Decode each level and concatenate; the uploader hands them to the GPU
            // instead of box-filtering its own. A pack replacement never gets here because the pack
            // ships mip0 only, so those keep the synthetic chain.
            rgba.clear();
            int lw = t.width, lh = t.height;
            for (int l = 0; l < t.levels; l++) {
                const uint32_t off = t.levelOffset(l), len = t.levelBytes(l);
                if (off + len > raw.size() || len == 0)
                    break;
                std::vector<uint8_t> lvl(raw.begin() + off, raw.begin() + off + len);
                std::vector<uint8_t> dec = Zelda3D::PicaDecode(t.glFormat(), lw, lh, lvl);
                if (dec.empty())
                    break;
                rgba.insert(rgba.end(), dec.begin(), dec.end());
                levels = l + 1;
                lw = lw > 1 ? lw / 2 : 1;
                lh = lh > 1 ? lh / 2 : 1;
            }
            if (rgba.empty()) { // decode failed outright — fall back to the base level alone
                rgba = Zelda3D::PicaDecode(t.glFormat(), t.width, t.height, raw);
                levels = 1;
            }
        }
        texRgba.push_back(std::move(rgba));
        dims.push_back({ w, h });
        texLevels.push_back(levels);
    }
    return base;
}

} // namespace Zelda3D
