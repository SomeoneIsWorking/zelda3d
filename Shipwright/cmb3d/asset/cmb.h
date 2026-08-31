// Parser for OoT3D CMB models (geometry + skeleton + material/texture refs).
// Port of tools/cmb.py. Pure C++ (no SoH/LUS deps). Produces, per material, an
// assembled triangle-soup of interleaved vertices ready for a GL VBO, plus the
// material/texture metadata the renderer needs. Bind-pose skinning matches cmb.py
// (rigid bone_dim==1 -> bound-bone world matrix; smooth bone_dim>1 -> raw model
// space). Animation (live bone matrices) is a later layer that reuses the skeleton.
#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <array>

namespace Zelda3D {

struct CmbBone {
    int id = 0;
    int parent = -1;
    float scale[3] = { 1, 1, 1 };
    float rot[3] = { 0, 0, 0 };
    float trans[3] = { 0, 0, 0 };
};

struct CmbMaterial {
    int index = 0;
    int tex0_idx = -1;
    uint16_t min_filter = 0x2601, mag_filter = 0x2601; // GL_LINEAR, no mip selection
    uint16_t wrap_s = 0x2901, wrap_t = 0x2901;         // GL enums
    float scale_s = 1, scale_t = 1, trans_s = 0, trans_t = 0, rot = 0;
    // Texture binding 1 (second sampler) + its coordinator-1 UV transform. Most materials leave
    // binding 1 empty (tex1_idx = -1); dual-texture combiners (g_title.cmb's fire-glow
    // `(TEX0+TEX1)*TEX0` ADD_MULT stage, oot3d-decomp/docs/title_logo_fireglow_cmab.md §3.1)
    // sample it through textureCoordinator[1]'s baked scale/translate (DccMaya convention,
    // noclip calcTexMtx: uv' = scale * (uv - trans), rot unsupported here — no OoT3D material
    // observed using coordinator-1 rotation).
    int tex1_idx = -1;
    uint16_t min1_filter = 0x2601, mag1_filter = 0x2601;
    uint16_t wrap1_s = 0x2901, wrap1_t = 0x2901;
    float scale1_s = 1, scale1_t = 1, trans1_s = 0, trans1_t = 0;
    // Coordinator mapping methods (noclip TextureCoordinatorMappingMethod): 0=None, 1=UvCoordinateMap,
    // 2=CameraCubeEnvMap, 3=CameraSphereEnvMap, 4=ProjectionMap. title_logo_us mat4-9 use coord1=3
    // (sphere) for the second texture; mat10-11 use coord0=3 (sphere) for the primary texture.
    int coord0_mapping = 1; // coordinator-0 (primary texture): default UV
    int coord1_mapping = 1; // coordinator-1 (second texture): default UV
    int coord0_source = 0;  // sourceCoordinate: 0/1/2 select CMB texCoord0/1/2
    int coord1_source = 0;
    // Texture binding 2 (third sampler) + coordinator-2 transform (render.multi-stage-tev).
    // Zora's Domain water (spot07_1 mat0-2) binds tex0+tex1+tex2 and combines them through a
    // 3-stage TEV chain — verified identical between the CMB file bytes and the live oracle's
    // per-draw register log (tev1..5 sources/ops), 2026-07-22.
    int tex2_idx = -1;
    uint16_t min2_filter = 0x2601, mag2_filter = 0x2601;
    uint16_t wrap2_s = 0x2901, wrap2_t = 0x2901;
    float scale2_s = 1, scale2_t = 1, trans2_s = 0, trans2_t = 0;
    int coord2_mapping = 1; // coordinator-2 (third texture): default UV
    int coord2_source = 0;
    int cull = 0;
    bool alpha_test = false;
    float alpha_ref = 0;
    // Blend state. The CMB stores GL-ES enum values directly (e.g. 0x0302 GL_SRC_ALPHA,
    // 0x0001 GL_ONE, 0x8006 GL_FUNC_ADD), identical to desktop GL — used verbatim. When
    // blend_enable is false the material is opaque (alpha-test only). Additive light-shaft
    // materials have dst_rgb = GL_ONE; without honoring this they render opaque.
    bool blend_enable = false;
    uint16_t blend_src_rgb = 0x0302, blend_dst_rgb = 0x0303; // GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA
    uint16_t blend_src_a = 0x0001, blend_dst_a = 0x0000;     // GL_ONE / GL_ZERO
    uint16_t blend_eq_rgb = 0x8006, blend_eq_a = 0x8006;     // GL_FUNC_ADD
    float blend_color[4] = { 0, 0, 0, 1 };                   // for CONSTANT_COLOR/ALPHA factors
    bool depth_write = true;                                 // translucent volumes usually disable this
    // Depth TEST enable (+0x134) and compare FUNCTION (+0x136), GL enum verbatim. Both were
    // unparsed until 2026-07-29 while the renderer hardcoded (enabled, LEQUAL) for every draw:
    // corpus-wide that is wrong for 11153 of 11172 materials. 11147 want LESS, 4 want ALWAYS,
    // and 77 disable the test entirely (sky domes, screen flashes, graveyard thunder).
    // Alpha-test COMPARE FUNCTION (+0x132), GL enum. Unparsed until 2026-07-29 while the shader
    // hardcoded 'pass when a >= ref' (GEQUAL) for every material. Corpus: of 913 alpha-test-
    // enabled materials 910 are GREATER, plus one each GEQUAL/LESS/NEVER. Mostly a no-op
    // difference, EXCEPT where ref==0 (GREATER cuts fully transparent texels, GEQUAL keeps
    // them) -- e.g. hairal_niwa's courtyard windows, which also write depth with blending off,
    // so the kept cut-out region renders opaque AND occludes.
    uint16_t alpha_func = 0x0206; // GL_GEQUAL (the old hardcoded rule)
    bool depth_test = true;
    uint16_t depth_func = 0x0201; // GL_LESS
    // Decal depth bias. OoT3D flags coplanar detail surfaces (sand/symbol decals on the
    // ground/walls) with a polygon offset that pulls them slightly toward the camera so
    // they win the depth test cleanly instead of z-fighting the base. Stored as a window-
    // depth offset (polygonOffsetUnit / 0xFFFE, per noclip); 0 = no bias. Applied in the
    // GL fragment shader as gl_FragDepth = gl_FragCoord.z + polygon_offset.
    float polygon_offset = 0.0f;

    // --- OoT3D fragment pipeline (PICA200), ported for pixel-parity world lighting. ---
    // See docs/oot3d_world_lighting_re.md. Scene geometry is VERTEX-lit: the per-vertex lit
    // colour fed to the TEV combiner is
    //   v_Color.rgb = sum(sceneAmb*matAmbient + NdotL*sceneDif*matDiffuse)
    //   v_Color.a   = sum(lightDiffuse.a*matDiffuse.a)
    // and the completed RGBA value is multiplied by the baked CmbVertex.color only when the
    // draw's HasColor uniform is true. Flags at +0x00/+0x01; mat ambient/diffuse
    // at +0xA4/+0xA8 (RGBA8 big-endian). The old renderer ignored all of this and did
    // texture*a_Color*uTint, dropping both the lighting and the combiner scale below.
    bool vertex_lighting = false;
    bool fragment_lighting = false;
    // Nested PICA fixed-function fragment-light descriptor at material +0xCC. The CMB runtime
    // binds this record through OoT3D FUN_004c6364; retain its serialized domains until the host
    // ports the corresponding light/LUT calculation (oot3d-decomp/docs/fragment_lighting.md).
    struct FragmentLightingDescriptor {
        uint16_t enum_10 = 0;
        uint16_t enum_12 = 0;
        bool flag_14 = false;
        uint16_t enum_18 = 0;
        uint16_t enum_1c = 0;
        bool flag_1e = false;
        bool flag_1f = false;
        bool flag_20 = false;
        bool flag_23 = false;
        bool enabled = false;
        uint16_t enum_26 = 0;
        float scale = 1.0f;
    } fragment_lighting_descriptor;
    // isFogEnabled (material +0x02): the PICA distance-fog stage applies to this material's
    // draws (fog_mode=5 with the palette-blended fog color/LUT). See cmb.cpp parse comment.
    bool is_fog = false;
    float mat_ambient[3] = { 1, 1, 1 };
    // CmbVShader c8 MatDiffuseColor. Alpha is live in both the unlit/no-color fallback
    // (words 112--120) and the lit per-enabled-light sum (words 89--110), so preserve RGBA.
    float mat_diffuse[4] = { 1, 1, 1, 1 };
    // Stage-0 TEV combiner. Scene materials are overwhelmingly a single
    // MODULATE(PRIMARY_COLOR=v_Color, TEXTURE0) stage, but the combine op and especially the
    // RGB SCALE (x1/x2/x4) are per-material — Kokiri grass MODULATEs at scaleRGB=x2, the
    // brightness factor the old path dropped. Captured verbatim; the renderer applies the
    // real op+scale. comb_combine_rgb uses CombineResultOpDMP enums (0x2100 MODULATE,
    // 0x0104 ADD, 0x6401 MULT_ADD, 0x1E01 REPLACE...). Multi-stage / non-MODULATE materials
    // are a documented follow-up (docs/oot3d_world_lighting_re.md).
    int comb_stage_count = 0;
    uint16_t comb_combine_rgb = 0x2100;                    // MODULATE
    float comb_scale_rgb = 1.0f;                           // 1 / 2 / 4
    uint16_t comb_src_rgb[3] = { 0x8577, 0x84C0, 0x8576 }; // PRIMARY_COLOR, TEXTURE0, CONSTANT
    // FULL per-stage TEV chain (render.multi-stage-tev): every stage's raw GL-DMP enums as
    // stored in the file's 0x28-byte combiner entries. Layout validated corpus-wide over all
    // 11172 materials in the ROM (tools/tev_corpus_survey.py, 2026-07-22): every field decodes
    // to its legal enum domain with ZERO violations.
    //   ops:  0x1E01 REPLACE, 0x2100 MODULATE, 0x0104 ADD, 0x8574 ADD_SIGNED,
    //         0x8575 INTERPOLATE, 0x84E7 SUBTRACT, 0x86AE/AF DOT3, 0x6401 MULT_ADD,
    //         0x6402 ADD_MULT   (NOTE: 0x8574 is ADD_SIGNED and 0x8575 INTERPOLATE — standard
    //         GL values; an older comment in cmb.cpp had them swapped, and "SUBTRACT 0x8506"
    //         was wrong: the real enum is 0x84E7.)
    //   srcs: 0x8577 PRIMARY, 0x8578 PREVIOUS, 0x8579 PREVIOUS_BUFFER, 0x8576 CONSTANT,
    //         0x84C0..0x84C3 TEXTURE0..3, 0x6210 FRAGMENT_PRIMARY (fragment lighting),
    //         0x6211 FRAGMENT_SECONDARY
    //   mods: 0x0300 SRC_COLOR, 0x0301 1-C, 0x0302 SRC_ALPHA, 0x0303 1-A,
    //         0x8580..0x8585 SRC_R / 1-R / SRC_G / 1-G / SRC_B / 1-B
    struct CombStage {
        uint16_t rgb_op = 0x2100, a_op = 0x2100;
        uint16_t rgb_scale = 1, a_scale = 1; // literal 1/2/4 (entry +0x04 / +0x06)
        uint16_t rgb_src[3] = { 0x8577, 0x84C0, 0x8576 };
        uint16_t rgb_mod[3] = { 0x0300, 0x0300, 0x0300 };
        uint16_t a_src[3] = { 0x8577, 0x84C0, 0x8576 };
        uint16_t a_mod[3] = { 0x0302, 0x0302, 0x0302 };
        uint16_t buf_rgb = 0x8579, buf_a = 0x8579; // entry +0x08/+0x0A (buffer input select)
        uint8_t const_idx = 0;                     // 0..5 (entry +0x24, per STAGE)
    };
    CombStage comb_stages[6];
    // True when the chain needs the generic per-stage TEV evaluator: anything that is not
    // (a) the trivial single-stage MODULATE(PRIMARY, TEXTURE0) shape the renderer's legacy
    // fast path evaluates exactly, or (b) one of the three byte-classified dual-texture title
    // shapes below (kept on their verified legacy path — CLOSED parity rows). Computed in
    // cmb.cpp parseMats AFTER dual_tex_mode classification.
    bool tev_generic = false;
    // PICA200 TEV constant-color selector: index 0..5 chosen from mat_constant[]. Combiner-entry
    // layout (verified empirically from AHG hyliaman2.cmb mat 0 stage 1, which sources
    // CONST[4] — the exact slot EnHy_Draw overrides via colorA per oot3d-decomp
    // build/decomp/001b4944.c): the selector is a u32 at combiner-entry +0x24, NOT +0x14 as
    // noclip's readMatsChunk suggests (misdocumented / different game). comb_const_idx here is
    // the FINAL stage's selector — that's what the "MODULATE(PREV, CONST)" post-tint stage uses
    // to pick the runtime-overridable clothing color; multi-stage full emulation is a follow-up.
    uint8_t comb_const_idx = 0;
    // comb_uses_const == true iff ANY stage's RGB sources include CONSTANT (0x8576). Materials
    // WITHOUT this flag can safely skip the CONSTANT modulate in the shader (no-op); materials
    // WITH this flag get their fragment output multiplied by mat_constant[comb_const_idx].
    bool comb_uses_const = false;
    // Hardware RGB scale (x1/x2/x4) of the CONSTANT-sourcing stage itself. PICA doubles/quadruples
    // that stage's output AFTER the modulate; dropping it is a direct, quantifiable gain gap
    // (g_title.cmb's fire-glow stage 1 is `2.0 * (PREVIOUS * CONSTANT0)` — the "half brightness"
    // root cause, title_logo_fireglow_cmab.md §3.2 fix 1). 1.0 when no stage sources CONSTANT.
    float comb_const_scale_rgb = 1.0f;
    // Dual-texture stage 0: true iff stage 0 is ADD_MULT(TEXTURE0, TEXTURE1, TEXTURE0) —
    // `(t0 + t1) * t0`, the detail-mask brightening combine used by g_title.cmb (§3.1). The
    // renderer samples binding 1 through coordinator 1 and applies the combine. Kept for the
    // existing close-test (cmb_combiner_parse_tests.cpp); dual_tex_mode below is the general
    // classifier (this flag implies dual_tex_mode == kDualTexAddMult).
    bool comb0_dual_addmult = false;
    // General dual-texture combine shape, classified from stage 0 (+ stage 1 when needed) of
    // the material's combiner chain. title_logo_us.cmb's shield/sword glint materials sample
    // binding 1 through a TWO-stage sequence (not g_title.cmb's single ADD_MULT stage) — see
    // cmb.cpp parseMats for the byte-level detection, verified against title_logo_us.cmb
    // 2026-07-10 (debug_journal/2026-07-10-shield-glint-dualtex.md).
    enum DualTexMode {
        kDualTexNone = 0,
        kDualTexAddMult = 1,                // (t0 + t1) * t0            [g_title.cmb fire-glow]
        kDualTexAddThenModulatePrimary = 2, // (t0 + t1) * primary       [shield glint]
        kDualTexModulateThenScale = 3,      // scale2 * (primary*t0*t1) [sword / shield detail]
    };
    int dual_tex_mode = kDualTexNone;
    // Stage-1 hardware RGB scale for kDualTexModulateThenScale (1/2/4); 1.0 for other modes.
    float dual_tex_scale2 = 1.0f;

    // PICA200 TEV constant-color palette: 6 float-RGBA slots per material. Base defaults come
    // from the CMB file (matConstColor[0..5] at material +0xB4..+0xCB, big-endian RGBA8 —
    // verified against real bytes of g_title.cmb / fine_star.cmb 2026-07-10; an earlier +0xB8
    // read was off by one slot, shifting every baked palette down by one). Referenced by the combiner via CONSTANT
    // (0x8576) with the stage's comb_const_idx picking which slot. The game also OVERWRITES these at runtime via
    // Model_SetMaterialConstantColor (see oot3d-decomp/build/decomp/003688a8.c and the EnHy
    // per-type body-color table at oot3d-decomp/data/enhy_body_colors.inc); the port carries
    // that override channel as a per-actor input in the render layer.
    float mat_constant[6][4] = {
        { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 },
    };
};

struct CmbTexture {
    std::string name;
    int width = 0, height = 0;
    uint16_t fmt = 0, data_type = 0;
    bool etc1 = false;
    uint32_t data_offset = 0, data_len = 0; // data_len covers the WHOLE mip chain, not one level
    // Baked mip level count, from the low u16 of the tex entry's +0x04 field. The renderer long
    // assumed the format carried no mips (generalised from fine_star.cmb, which genuinely has one
    // level) and box-filtered its own chain instead. It does carry them: across all 10538 textures
    // in the ROM, data_len == baseLevelBytes * sum(1/4^i) for this count, with the derived base
    // giving a legal bpp -- 0 exceptions. 7284 textures ship authored mips (3 levels x6730,
    // 2 x544, 4 x10). See claim C018.
    int levels = 1;
    uint32_t glFormat() const {
        return ((uint32_t)data_type << 16) | fmt;
    }
    // Byte size of mip level `l` (0 = base). Each level halves both dimensions, floored at 1.
    uint32_t levelBytes(int l) const {
        if (levels <= 0 || data_len == 0)
            return 0;
        // Derive bits-per-pixel from the base level rather than a format table, so this stays
        // correct for every encoding the decoder supports (including ETC1's 4bpp block form).
        double denom = 0.0;
        for (int i = 0; i < levels; i++)
            denom += 1.0 / (double)(1u << (2 * i));
        const double baseBytes = (double)data_len / denom;
        int lw = width, lh = height;
        for (int i = 0; i < l; i++) {
            lw = lw > 1 ? lw / 2 : 1;
            lh = lh > 1 ? lh / 2 : 1;
        }
        if (width <= 0 || height <= 0)
            return 0;
        return (uint32_t)(baseBytes * ((double)(lw * lh) / (double)(width * height)) + 0.5);
    }
    // Byte offset of mip level `l` within this texture's data block.
    uint32_t levelOffset(int l) const {
        uint32_t off = 0;
        for (int i = 0; i < l; i++)
            off += levelBytes(i);
        return off;
    }
};

// Interleaved render vertex: position (model space), normal, source-resolved UVs, and skinning
// bindings (up to 4 bone ids + weights). MUST stay byte-compatible with
// Zelda3DGlVtx (zelda3d_gl.h) — the bridge reinterpret_casts between them.
struct CmbVertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    float boneIds[4] = { 0, 0, 0, 0 };
    float weights[4] = { 0, 0, 0, 0 };
    float color[4] = { 1, 1, 1, 1 }; // per-vertex RGBA (OoT3D baked lighting / falloff)
    // Coordinators 1/2 need separate resolved coordinates because a material can combine
    // TEX0(texCoord0) with TEX1(texCoord1), as valbasiagnd does for its fire detail.
    float uv1[2] = { 0, 0 };
    float uv2[2] = { 0, 0 };
};

// One draw batch: all triangles that use a given (material, mesh_id, HasColor), as a triangle list.
// Batches are split by mesh_id (not just material) so the renderer can toggle per-mesh_id
// visibility at draw time — e.g. Link's childlink_v2 bakes several hand-pose / equipment
// variants onto ONE skin material, distinguished only by mesh_id; the game shows a subset
// per frame. Keeping them in separate groups lets us cull the hidden ones without rebuilding.
struct CmbDrawGroup {
    int material_index = 0;
    int mesh_id = -1;             // CMB mesh_id of the contributing meshes (the visibility-switch key)
    bool has_color = false;       // CmbVShader HasColor uniform for this geometry batch
    std::vector<CmbVertex> verts; // multiple of 3
};

class Cmb {
  public:
    explicit Cmb(std::vector<uint8_t> data);
    bool ok() const {
        return mOk;
    }
    const std::string& error() const {
        return mErr;
    }

    const std::string& name() const {
        return mName;
    }
    uint32_t version() const {
        return mVersion;
    }
    const std::vector<CmbBone>& bones() const {
        return mBones;
    }
    const std::vector<CmbMaterial>& materials() const {
        return mMaterials;
    }
    const std::vector<CmbTexture>& textures() const {
        return mTextures;
    }
    // Bind-pose world matrix per bone id (row-major flat 16-float). Used by CSAB
    // skinning to form skinMatrix = animWorld . inverse(bindWorld).
    const std::vector<std::array<float, 16>>& boneMatrices() const {
        return mBoneMatrix;
    }

    // Sepds that no mesh entry references. A well-formed CMB has ZERO: every sepd's geometry is
    // reachable. A non-zero count means part of the model silently never builds, which is how the
    // MM3D mesh-stride bug hid (27 of 41 sepds orphaned -> the room's ground never drew).
    size_t unreferencedSepdCount() const;

    // Texture index used by a material's primary binding (0 if unknown/none).
    int materialTexture(int matIndex) const;
    // Raw (still-encoded) bytes of a texture, sliced from the CMB texdata block.
    std::vector<uint8_t> textureRaw(const CmbTexture& t) const;

    // Assemble all meshes into per-material draw groups (bind pose).
    std::vector<CmbDrawGroup> buildDrawGroups() const;

    // Diagnostic: histogram of prm.index_type over every prm in the file, as
    // {gl data type -> count}. Exposed because the index element size is the one
    // place our reader and the shipped MM3D engine are known to differ (the engine
    // uploads one flat index buffer sized index_count<<1, i.e. uniform u16).
    std::map<uint16_t, int> indexTypeHistogram() const;

    // Diagnostic: for each sepd, the highest vertex index any of its prms references vs how many
    // vertices its POSITION VATR buffer can actually hold. Lines where max >= capacity are reads
    // past the end of the buffer (the MM3D room defect).
    std::string indexRangeReport() const;

    // Same, but with CSAB skinning applied: skinMats is indexed by bone id and is
    // skinMatrix = animWorld . bindInverse for each bone (see asset/csab). Each
    // vertex is taken to MODEL space exactly as buildDrawGroups() does (rigid:
    // .bindWorld; smooth: raw), then transformed by the weighted blend of its bones'
    // skinMats. skinMats == nullptr (n==0) is identity -> byte-identical to
    // buildDrawGroups() (the bind pose). Mirrors tools/csab.py skinned_triangles.
    std::vector<CmbDrawGroup> buildDrawGroupsSkinned(const std::array<float, 16>* skinMats, size_t n) const;

    // Per-mesh introspection, for selectively culling duplicate VARIANT meshes that share a
    // material and so collapse into one draw group (can't be culled per group). e.g. Link's
    // childlink_v2.cmb bakes several hand-pose variants per hand, all on one skin material.
    size_t meshCount() const {
        return mMeshes.size();
    }
    int meshMaterial(size_t i) const {
        return i < mMeshes.size() ? mMeshes[i].material_index : -1;
    }
    int meshId(size_t i) const {
        return i < mMeshes.size() ? mMeshes[i].mesh_id : -1;
    }
    std::vector<int> meshBones(size_t i) const; // sorted union of bone ids the mesh references
    // As buildDrawGroups[Skinned] but skip every mesh whose index has skipMesh[idx] != 0
    // (skipMesh may be shorter than meshCount(); missing entries = keep).
    std::vector<CmbDrawGroup> buildDrawGroups(const std::vector<uint8_t>& skipMesh) const;
    std::vector<CmbDrawGroup> buildDrawGroupsSkinned(const std::array<float, 16>* skinMats, size_t n,
                                                     const std::vector<uint8_t>& skipMesh) const;

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<uint8_t> mData;

    uint32_t mVersion = 0;
    std::string mName;
    uint32_t mIndexCount = 0;
    uint32_t mSklPtr = 0, mMatsPtr = 0, mTexPtr = 0, mSklmPtr = 0, mVatrPtr = 0, mIdxPtr = 0, mTexdataPtr = 0;

    std::vector<CmbBone> mBones;
    // bind-pose world matrix per bone id (4x4 row-major), as a flat 16-float array.
    std::vector<std::array<float, 16>> mBoneMatrix; // indexed by bone id
    std::vector<CmbMaterial> mMaterials;
    std::vector<CmbTexture> mTextures;

    // VATR: attribute name index -> (abs offset, size)
    struct VatrBuf {
        uint32_t off = 0, size = 0;
    };
    std::vector<VatrBuf> mVatr; // one per attribute slot in attrs def

    struct SepdAttr {
        uint32_t start = 0;
        float scale = 1;
        uint16_t data_type = 0;
        uint16_t mode = 0; // 0 array, 1 constant
        float constant[4] = { 0, 0, 0, 0 };
        bool present = false;
    };
    struct Prm {
        uint16_t index_type = 0;
        uint16_t count = 0;
        uint16_t first = 0;
    };
    struct Prms {
        uint16_t skinning_mode = 0;
        std::vector<uint16_t> bone_table;
        Prm prm;
    };
    struct Sepd {
        std::vector<SepdAttr> attrs; // indexed by attribute slot
        uint16_t prim_count = 0;
        uint16_t bone_dimension = 0;
        std::vector<Prms> prms;
    };
    struct Mesh {
        uint16_t sepd_index = 0;
        uint8_t material_index = 0;
        uint8_t mesh_id = 0;
    };

    std::vector<Sepd> mSepds;
    std::vector<Mesh> mMeshes;

    bool parseSkl();
    void computeBoneMatrices();
    bool parseMats();
    bool parseVatr();
    bool parseTex();
    bool parseSklm();
    Sepd parseSepd(uint32_t p);
    Prms parsePrms(uint32_t p);
    Prm parsePrm(uint32_t p);

    // Read a single attribute value (comps components) at element idx for a sepd attr.
    void readAttr(const SepdAttr& attr, int attrSlot, uint32_t idx, int comps, float* out) const;
    bool attrHasData(const SepdAttr& attr, int attrSlot) const;
};

} // namespace Zelda3D
