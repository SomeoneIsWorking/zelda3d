#include "cmb.h"
#include "mat4.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>

namespace Zelda3D {

static uint8_t u8(const uint8_t* b, size_t o) {
    return b[o];
}
static int16_t s16(const uint8_t* b, size_t o) {
    int16_t v;
    memcpy(&v, b + o, 2);
    return v;
}
static uint16_t u16(const uint8_t* b, size_t o) {
    uint16_t v;
    memcpy(&v, b + o, 2);
    return v;
}
static uint32_t u32(const uint8_t* b, size_t o) {
    uint32_t v;
    memcpy(&v, b + o, 4);
    return v;
}
static float f32(const uint8_t* b, size_t o) {
    float v;
    memcpy(&v, b + o, 4);
    return v;
}

// GL data types
enum { DT_BYTE = 0x1400, DT_UBYTE, DT_SHORT, DT_USHORT, DT_INT, DT_UINT, DT_FLOAT };
static int dtSize(uint16_t t) {
    switch (t) {
        case DT_BYTE:
        case DT_UBYTE:
            return 1;
        case DT_SHORT:
        case DT_USHORT:
            return 2;
        default:
            return 4; // INT/UINT/FLOAT
    }
}
static float dtRead(const uint8_t* b, size_t o, uint16_t t) {
    switch (t) {
        case DT_BYTE:
            return (float)(int8_t)b[o];
        case DT_UBYTE:
            return (float)b[o];
        case DT_SHORT:
            return (float)s16(b, o);
        case DT_USHORT:
            return (float)u16(b, o);
        case DT_INT: {
            int32_t v;
            memcpy(&v, b + o, 4);
            return (float)v;
        }
        case DT_UINT:
            return (float)u32(b, o);
        case DT_FLOAT:
            return f32(b, o);
        default:
            return 0;
    }
}

// Attribute slots. OoT3D (v6) has no tangent; MM3D (v7) inserts it after normal.
struct AttrDef {
    const char* name;
    int comps;
};
static const AttrDef ATTRS_OOT3D[] = { { "position", 3 },    { "normal", 3 },     { "color", 4 },
                                       { "texCoord0", 2 },   { "texCoord1", 2 },  { "texCoord2", 2 },
                                       { "boneIndices", 0 }, { "boneWeights", 0 } };
static const AttrDef ATTRS_MM3D[] = { { "position", 3 },  { "normal", 3 },      { "tangent", 3 },
                                      { "color", 4 },     { "texCoord0", 2 },   { "texCoord1", 2 },
                                      { "texCoord2", 2 }, { "boneIndices", 0 }, { "boneWeights", 0 } };

// 4x4 row-major matrix helpers live in mat4.h (shared with csab.cpp).

Cmb::Cmb(std::vector<uint8_t> data) : mData(std::move(data)) {
    const uint8_t* b = mData.data();
    if (mData.size() < 0x44 || memcmp(b, "cmb ", 4) != 0) {
        mErr = "not a CMB";
        return;
    }
    mVersion = u32(b, 0x08);
    {
        size_t e = 0x10;
        while (e < 0x20 && b[e])
            e++;
        mName.assign((const char*)b + 0x10, e - 0x10);
    }
    mIndexCount = u32(b, 0x20);
    mSklPtr = u32(b, 0x24);
    // MM3D (version >= 7 / Majora, EverOasis) inserts a "qtrs" chunk pointer at 0x28
    // right after the skeleton, shifting every subsequent chunk pointer by +4 vs the
    // OoT3D (version <= 6) header (noclip cmb.ts: the `qtrs` chunk exists past Ocarina).
    uint32_t d = (mVersion >= 7) ? 4 : 0; // header-pointer shift after skl
    mMatsPtr = u32(b, 0x28 + d);
    mTexPtr = u32(b, 0x2C + d);
    mSklmPtr = u32(b, 0x30 + d);
    // 0x34+d = "luts" chunk (skipped)
    mVatrPtr = u32(b, 0x38 + d);
    mIdxPtr = u32(b, 0x3C + d);
    mTexdataPtr = u32(b, 0x40 + d);
    if (!parseSkl())
        return;
    computeBoneMatrices();
    if (!parseMats())
        return;
    if (!parseVatr())
        return;
    if (!parseTex())
        return;
    if (!parseSklm())
        return;
    mOk = true;
}

static const AttrDef* Cmb_attrsDef(uint32_t version, int* count) {
    if (version >= 7) {
        *count = (int)(sizeof(ATTRS_MM3D) / sizeof(AttrDef));
        return ATTRS_MM3D;
    }
    *count = (int)(sizeof(ATTRS_OOT3D) / sizeof(AttrDef));
    return ATTRS_OOT3D;
}

bool Cmb::parseSkl() {
    const uint8_t* b = mData.data();
    uint32_t p = mSklPtr;
    if (memcmp(b + p, "skl ", 4) != 0) {
        mErr = "bad skl";
        return false;
    }
    uint32_t n = u32(b, p + 8);
    uint32_t o = p + 0x10;
    // Bone stride: OoT3D (v<=6) is 0x28; MM3D (v>=7) inserts an extra u32 at the end of
    // each bone (unknown — perhaps a name-hash / ref-count), making stride 0x2C. Without
    // this bump the parser reads bone[1..] as noise (parent ids in the tens of thousands)
    // and computeBoneMatrices does OOB indexing → segfault. Verified vs zelda2_mm.gar.
    const uint32_t boneStride = (mVersion >= 7) ? 0x2C : 0x28;
    mBones.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        CmbBone& bn = mBones[i];
        bn.id = u8(b, o);
        bn.parent = s16(b, o + 2);
        for (int k = 0; k < 3; k++)
            bn.scale[k] = f32(b, o + 4 + 4 * k);
        for (int k = 0; k < 3; k++)
            bn.rot[k] = f32(b, o + 0x10 + 4 * k);
        for (int k = 0; k < 3; k++)
            bn.trans[k] = f32(b, o + 0x1C + 4 * k);
        o += boneStride;
    }
    return true;
}

void Cmb::computeBoneMatrices() {
    int maxId = 0;
    for (const auto& bn : mBones)
        maxId = std::max(maxId, bn.id);
    mBoneMatrix.assign(maxId + 1, matId());
    std::vector<char> done(maxId + 1, 0);
    std::vector<const CmbBone*> byId(maxId + 1, nullptr);
    for (const auto& bn : mBones)
        byId[bn.id] = &bn;

    // iterative resolve (parents always have lower-or-defined id; recurse via stack)
    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (done[id])
            return mBoneMatrix[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matMul(matT(bn->trans[0], bn->trans[1], bn->trans[2]),
                        matMul(matMul(matRz(bn->rot[2]), matRy(bn->rot[1])), matRx(bn->rot[0])));
        L = matMul(L, matS(bn->scale[0], bn->scale[1], bn->scale[2]));
        Mat4 W = (bn->parent < 0) ? L : matMul(world(bn->parent), L);
        mBoneMatrix[id] = W;
        done[id] = 1;
        return W;
    };
    for (const auto& bn : mBones)
        world(bn.id);
}

bool Cmb::parseMats() {
    const uint8_t* b = mData.data();
    uint32_t p = mMatsPtr;
    if (p == 0 || memcmp(b + p, "mats", 4) != 0)
        return true; // optional
    uint32_t n = u32(b, p + 8);
    uint32_t stride = (mVersion <= 6) ? 0x15C : 0x16C;
    uint32_t o = p + 0x0C;
    // The per-material combiner index tables point into a shared settings table that begins
    // right after all material structs (noclip readMatsChunk). Entry stride 0x28.
    uint32_t combTableBase = p + 0x0C + n * stride;
    mMaterials.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        CmbMaterial& m = mMaterials[i];
        m.index = (int)i;
        m.cull = u8(b, o + 4);
        // Polygon (depth) offset for decal surfaces: enable flag @ +0x05, signed unit @ +0x07
        // (noclip readMatsChunk). polygon_offset = unit/0xFFFE; pulls coplanar decals toward
        // the camera so they don't z-fight the base ground/wall.
        if (u8(b, o + 0x05))
            m.polygon_offset = (float)((int8_t)u8(b, o + 0x07)) / 65534.0f;
        uint32_t bo = o + 0x10;
        m.tex0_idx = s16(b, bo);
        m.min_filter = u16(b, bo + 4);
        m.mag_filter = u16(b, bo + 6);
        m.wrap_s = u16(b, bo + 8);
        m.wrap_t = u16(b, bo + 0x0A);
        // Texture binding 1 (bindings are 3 x 0x18-byte entries at +0x10; noclip readMatsChunk).
        uint32_t bo1 = o + 0x10 + 0x18;
        m.tex1_idx = s16(b, bo1);
        m.min1_filter = u16(b, bo1 + 4);
        m.mag1_filter = u16(b, bo1 + 6);
        m.wrap1_s = u16(b, bo1 + 8);
        m.wrap1_t = u16(b, bo1 + 0x0A);
        // Texture binding 2 (third sampler, render.multi-stage-tev): Zora's water combines
        // tex0+tex1+tex2 through a 3-stage chain (spot07_1 mat0-2).
        uint32_t bo2 = o + 0x10 + 0x30;
        m.tex2_idx = s16(b, bo2);
        m.min2_filter = u16(b, bo2 + 4);
        m.mag2_filter = u16(b, bo2 + 6);
        m.wrap2_s = u16(b, bo2 + 8);
        m.wrap2_t = u16(b, bo2 + 0x0A);
        // Texture coordinators: 3 x 0x18-byte entries at +0x58 (scaleS/T @ +4/+8, transS/T @
        // +0xC/+0x10, rot @ +0x14; noclip readMatsChunk). Coordinator 0 feeds binding 0,
        // coordinator 1 feeds binding 1 (g_title.cmb's mableT detail mask has the baked
        // scale(3,2)/trans(0,0.9433) transform on coordinator 1).
        uint32_t co = o + 0x58;
        m.scale_s = f32(b, co + 4);
        m.scale_t = f32(b, co + 8);
        m.trans_s = f32(b, co + 12);
        m.trans_t = f32(b, co + 16);
        m.rot = f32(b, co + 20);
        m.coord0_source = u8(b, co + 0);
        // Coordinator-0 mapping method (noclip byte[2] of coordinator entry 0).
        m.coord0_mapping = u8(b, co + 2);
        uint32_t co1 = o + 0x58 + 0x18;
        m.scale1_s = f32(b, co1 + 4);
        m.scale1_t = f32(b, co1 + 8);
        m.trans1_s = f32(b, co1 + 12);
        m.trans1_t = f32(b, co1 + 16);
        // TextureCoordinator::sourceCoordinate is byte 0; byte 1 is referenceCamera.
        // This used to be omitted after a corpus survey accidentally inspected byte 1 and
        // concluded every coordinator sourced texCoord0. valbasiagnd's additive TEX1 stages
        // explicitly use sourceCoordinate=1 and carry an independent texCoord1 attribute.
        m.coord1_source = u8(b, co1 + 0);
        // Coordinator-1 mapping method (noclip byte[2] of the coordinator entry): determines how
        // the second texture's UVs are generated. 3 = CameraSphereEnvMap (normal-derived UV).
        m.coord1_mapping = u8(b, co1 + 2);
        // Coordinator 2 (feeds texture binding 2).
        uint32_t co2 = o + 0x58 + 0x30;
        m.scale2_s = f32(b, co2 + 4);
        m.scale2_t = f32(b, co2 + 8);
        m.trans2_s = f32(b, co2 + 12);
        m.trans2_t = f32(b, co2 + 16);
        m.coord2_source = u8(b, co2 + 0);
        m.coord2_mapping = u8(b, co2 + 2);
        m.alpha_test = u8(b, o + 0x130) != 0;
        m.alpha_ref = u8(b, o + 0x131) / 255.0f;
        m.alpha_func = u16(b, o + 0x132);
        // Blend state (GL-ES enum values, used verbatim — see CmbMaterial). Offsets per
        // noclip readMatsChunk (Ocarina v6 layout); verified against real room materials.
        m.depth_test = u8(b, o + 0x134) != 0;
        m.depth_write = u8(b, o + 0x135) != 0;
        m.depth_func = u16(b, o + 0x136);
        m.blend_enable = u8(b, o + 0x138) != 0;
        m.blend_src_rgb = u16(b, o + 0x13C);
        m.blend_dst_rgb = u16(b, o + 0x13E);
        m.blend_eq_rgb = u16(b, o + 0x140);
        m.blend_src_a = u16(b, o + 0x144);
        m.blend_dst_a = u16(b, o + 0x146);
        m.blend_eq_a = u16(b, o + 0x148);
        for (int k = 0; k < 4; k++)
            m.blend_color[k] = f32(b, o + 0x14C + 4 * k);

        // --- OoT3D fragment pipeline (lighting flags + material colours + TEV combiner). ---
        // See docs/oot3d_world_lighting_re.md. Material colours are RGBA8 BIG-endian.
        m.fragment_lighting = u8(b, o + 0x00) != 0;
        m.vertex_lighting = u8(b, o + 0x01) != 0;
        const uint32_t fragmentDescriptor = o + 0xCC;
        auto& descriptor = m.fragment_lighting_descriptor;
        descriptor.enum_10 = u16(b, fragmentDescriptor + 0x10);
        descriptor.enum_12 = u16(b, fragmentDescriptor + 0x12);
        descriptor.flag_14 = u8(b, fragmentDescriptor + 0x14) != 0;
        descriptor.enum_18 = u16(b, fragmentDescriptor + 0x18);
        descriptor.enum_1c = u16(b, fragmentDescriptor + 0x1C);
        descriptor.flag_1e = u8(b, fragmentDescriptor + 0x1E) != 0;
        descriptor.flag_1f = u8(b, fragmentDescriptor + 0x1F) != 0;
        descriptor.flag_20 = u8(b, fragmentDescriptor + 0x20) != 0;
        descriptor.flag_23 = u8(b, fragmentDescriptor + 0x23) != 0;
        descriptor.enabled = u8(b, fragmentDescriptor + 0x24) != 0;
        descriptor.enum_26 = u16(b, fragmentDescriptor + 0x26);
        descriptor.scale = f32(b, fragmentDescriptor + 0x28);
        // isFogEnabled (noclip readMatsChunk +0x02): this material participates in the PICA
        // distance-fog stage (fog_mode=5 per draw on the 3DS). spot99 room 0: 24/29 set; the
        // additive/effect materials (0,5,11-13) are the exceptions. Consumed by the renderer's
        // 3DS fog path (oot3d-decomp docs/title_env_lighting.md §13).
        m.is_fog = u8(b, o + 0x02) != 0;
        auto rgb_be = [&](uint32_t off, float* dst) {
            dst[0] = b[off + 0] / 255.0f; // big-endian: byte0 = R
            dst[1] = b[off + 1] / 255.0f;
            dst[2] = b[off + 2] / 255.0f;
        };
        rgb_be(o + 0xA4, m.mat_ambient);
        rgb_be(o + 0xA8, m.mat_diffuse);
        m.mat_diffuse[3] = b[o + 0xAB] / 255.0f;
        // PICA200 TEV constant-color palette: 6 slots at +0xB4..+0xCB (u8 RGBA per slot, matching
        // noclip readMatsChunk's constantColors[0]=+0xB4 and the byte-level decode in
        // oot3d-decomp/docs/title_logo_fireglow_cmab.md §3.1). BUGFIX 2026-07-10: this used to
        // read +0xB8, shifting the whole palette down one slot (slot k returned the file's slot
        // k+1) — g_title.cmb's constColor[0]=(255,255,255,255) read as black, fine_star.cmb's
        // (255,255,127) star tint read as black (which is what the shader-side "constBlack skip"
        // heuristic was then built around). File values are u8; the runtime struct + the SoH3D
        // port carry them as float RGBA (matches the shader UBO and matches
        // Model_SetMaterialConstantColor / FUN_003688a8, which writes float[4]). Townsfolk
        // archetype CMBs bake black-opaque defaults and the game overwrites them at runtime —
        // see debug_journal/2026-07-02-en-hy-body-colors.md.
        for (int k = 0; k < 6; k++) {
            uint32_t co = o + 0xB4 + k * 4;
            m.mat_constant[k][0] = b[co + 0] / 255.0f;
            m.mat_constant[k][1] = b[co + 1] / 255.0f;
            m.mat_constant[k][2] = b[co + 2] / 255.0f;
            m.mat_constant[k][3] = b[co + 3] / 255.0f;
        }
        // Stage-N combiner. Each stage's 0x28-byte entry (verified vs AHG hyliaman2 mat 0
        // combined with the runtime overrides EnHy_Draw writes):
        //   +0x00  u16  rgb_combine op (0x2100 MODULATE / 0x0104 ADD / 0x6401 MULT_ADD / ...)
        //   +0x04  u16  rgb_scale (1/2/4)
        //   +0x0C  u16  rgb src A (0x8577 PRIMARY / 0x84C0 TEXTURE0 / 0x8576 CONSTANT / 0x8578 PREVIOUS)
        //   +0x0E  u16  rgb src B
        //   +0x10  u16  rgb src C
        //   +0x24  u32  constant-color selector (0..5 index into mat_constant[])
        // Stage 0's data goes into comb_combine_rgb/scale/src_rgb[] (existing world-lighting
        // port relies on those). comb_const_idx captures the FINAL stage's selector — for
        // townsfolk archetypes stage 1 is MODULATE(PREV, CONST[N]) with N = 3 or 4 = the slot
        // EnHy_Draw overrides.  comb_uses_const = OR of "any stage sources CONSTANT".
        m.comb_stage_count = (int)u32(b, o + 0x120);
        // Stage-1 op/scale/sources, captured alongside stage 0 below — needed to classify
        // multi-stage dual-texture shapes (title_logo_us.cmb's shield/sword glint materials;
        // see comb_dual_tex_mode below). Only meaningful when m.comb_stage_count >= 2.
        uint16_t stage1Op = 0, stage1SrcA = 0, stage1SrcB = 0, stage1Scale = 1;
        for (int s = 0; s < m.comb_stage_count && s < 6; s++) {
            uint32_t cidx = u16(b, o + 0x124 + s * 2); // per-stage combiner-settings index
            uint32_t co = combTableBase + cidx * 0x28;
            uint16_t op = u16(b, co + 0x00);
            uint16_t srcA = u16(b, co + 0x0C);
            uint16_t srcB = u16(b, co + 0x0E);
            uint16_t srcC = u16(b, co + 0x10);
            // Full per-stage capture (render.multi-stage-tev). Field layout validated over the
            // whole ROM corpus by tools/tev_corpus_survey.py (zero enum-domain violations):
            // +0x00 rgbOp, +0x02 alphaOp, +0x04 rgbScale, +0x06 alphaScale, +0x08/+0x0A buffer
            // inputs, +0x0C srcRGB[3], +0x12 modRGB[3], +0x18 srcA[3], +0x1E modA[3], +0x24 const.
            CmbMaterial::CombStage& cs = m.comb_stages[s];
            cs.rgb_op = op;
            cs.a_op = u16(b, co + 0x02);
            cs.rgb_scale = u16(b, co + 0x04);
            cs.a_scale = u16(b, co + 0x06);
            cs.buf_rgb = u16(b, co + 0x08);
            cs.buf_a = u16(b, co + 0x0A);
            for (int k = 0; k < 3; k++) {
                cs.rgb_src[k] = u16(b, co + 0x0C + 2 * k);
                cs.rgb_mod[k] = u16(b, co + 0x12 + 2 * k);
                cs.a_src[k] = u16(b, co + 0x18 + 2 * k);
                cs.a_mod[k] = u16(b, co + 0x1E + 2 * k);
            }
            cs.const_idx = (uint8_t)(u32(b, co + 0x24) & 0x07);
            if (cs.const_idx > 5)
                cs.const_idx = 0;
            if (s == 1) {
                stage1Op = op;
                stage1SrcA = srcA;
                stage1SrcB = srcB;
                stage1Scale = u16(b, co + 0x04);
            }
            // Which of the three source slots the OP actually consumes. MODULATE / ADD /
            // SUBTRACT (0x84E7) / ADD_SIGNED (0x8574) / DOT3_RGB / DOT3_RGBA only look at A and
            // B; INTERPOLATE (0x8575) / MULT_ADD (0x6401) / ADD_MULT (0x6402) look at all three;
            // REPLACE (0x1E01) uses A only. This matters because scene materials commonly have
            // srcC=CONSTANT as a leftover default while running MODULATE(A,B) — treating that C
            // as an active CONSTANT reference would darken every mesh to black.
            // (Corrected 2026-07-22: 0x8574 is ADD_SIGNED and 0x8575 INTERPOLATE — standard GL
            // values, previously swapped here — and SUBTRACT is 0x84E7, not 0x8506.)
            int slotsUsed = 3; // default: assume all three (safe fail-open for unknown ops)
            switch (op) {
                case 0x2100: // MODULATE
                case 0x0104: // ADD
                case 0x84E7: // SUBTRACT
                case 0x8574: // ADD_SIGNED
                case 0x86AE: // DOT3_RGB
                case 0x86AF: // DOT3_RGBA
                    slotsUsed = 2;
                    break;
                case 0x1E01: // REPLACE
                    slotsUsed = 1;
                    break;
                default:
                    break;
            }
            bool sourcesConst = false;
            if (slotsUsed >= 1 && srcA == 0x8576)
                sourcesConst = true;
            if (slotsUsed >= 2 && srcB == 0x8576)
                sourcesConst = true;
            if (slotsUsed >= 3 && srcC == 0x8576)
                sourcesConst = true;
            if (sourcesConst) {
                m.comb_uses_const = true;
                uint32_t idx32 = u32(b, co + 0x24);
                m.comb_const_idx = (uint8_t)(idx32 & 0x07);
                if (m.comb_const_idx > 5)
                    m.comb_const_idx = 0;
                // Hardware RGB scale of the CONSTANT-sourcing stage itself (PICA scales the
                // stage output AFTER the combine). g_title.cmb stage 1 = MODULATE(PREV, CONST0)
                // at x2 — the fire-glow "half brightness" factor (fireglow doc §3.2 fix 1).
                uint16_t cScale = u16(b, co + 0x04);
                m.comb_const_scale_rgb = (cScale == 2 || cScale == 4) ? (float)cScale : 1.0f;
            }
            if (s == 0) {
                m.comb_combine_rgb = op;
                uint16_t scale = u16(b, co + 0x04);
                m.comb_scale_rgb = (scale == 2 || scale == 4) ? (float)scale : 1.0f;
                m.comb_src_rgb[0] = srcA;
                m.comb_src_rgb[1] = srcB;
                m.comb_src_rgb[2] = srcC;
                // Dual-texture detail-mask combine: ADD_MULT(TEXTURE0, TEXTURE1, TEXTURE0) =
                // (t0 + t1) * t0, sampling binding 1 through coordinator 1 (fireglow doc §3.1).
                m.comb0_dual_addmult = (op == 0x6402 /*ADD_MULT*/ && srcA == 0x84C0 /*TEXTURE0*/ &&
                                        srcB == 0x84C1 /*TEXTURE1*/ && srcC == 0x84C0 && m.tex1_idx >= 0);
            }
        }
        // Multi-stage dual-texture shapes: title_logo_us.cmb's shield glint (mat6/9) and
        // sword/shield detail-mask (mat4/5/7) materials sample binding 1 through TWO combiner
        // stages instead of g_title.cmb's single ADD_MULT stage (comb0_dual_addmult above).
        // Verified byte-for-byte against title_logo_us.cmb (oot3d-decomp; scratch dump
        // 2026-07-10) — TEXTURE1 (0x84C1) must be an ACTUALLY-CONSUMED source (within the op's
        // slot arity) in one of the first two stages, not just a leftover tex1_idx binding
        // (mat8's tex1 is declared but never read by either stage — stays kDualTexNone).
        m.dual_tex_mode = CmbMaterial::kDualTexNone;
        if (m.tex1_idx >= 0 && m.comb0_dual_addmult) {
            m.dual_tex_mode = CmbMaterial::kDualTexAddMult; // (t0+t1)*t0, single stage
            // STAGE-COUNT GUARD 2026-07-29. Modes 2 and 3 describe EXACTLY-2-stage chains, but this
            // classified on stages 0-1 only and accepted `>= 2`, so any longer chain whose first two
            // stages happened to match was routed to a 2-stage approximation and its remaining stages
            // were silently discarded. 170 materials were affected -- 159 at 3 stages (including
            // link_v2 mat15/mat28 and childlink_v2 mat18, i.e. LINK'S OWN BODY, losing a TEX2 modulate
            // or a CONST tint stage), 3 at 4 stages, 8 title materials at 3.
            // Requiring an exact match sends everything longer to the generic per-stage TEV evaluator,
            // which runs the real chain.
        } else if (m.tex1_idx >= 0 && m.comb_stage_count == 2) {
            uint16_t op0 = m.comb_combine_rgb;
            uint16_t a0 = m.comb_src_rgb[0], b0 = m.comb_src_rgb[1];
            bool stage0AddTex01 = (op0 == 0x0104 /*ADD*/ && a0 == 0x84C0 && b0 == 0x84C1);
            bool stage0ModPrimTex0 =
                (op0 == 0x2100 /*MODULATE*/ && ((a0 == 0x8577 && b0 == 0x84C0) || (a0 == 0x84C0 && b0 == 0x8577)));
            bool stage1ModPrevPrim = (stage1Op == 0x2100 && ((stage1SrcA == 0x8578 && stage1SrcB == 0x8577) ||
                                                             (stage1SrcA == 0x8577 && stage1SrcB == 0x8578)));
            bool stage1ModPrevTex1 = (stage1Op == 0x2100 && ((stage1SrcA == 0x8578 && stage1SrcB == 0x84C1) ||
                                                             (stage1SrcA == 0x84C1 && stage1SrcB == 0x8578)));
            if (stage0AddTex01 && stage1ModPrevPrim) {
                // stage0: PREVIOUS = t0 + t1; stage1: PREVIOUS = PREVIOUS * PRIMARY.
                m.dual_tex_mode = CmbMaterial::kDualTexAddThenModulatePrimary;
            } else if (stage0ModPrimTex0 && stage1ModPrevTex1) {
                // stage0: PREVIOUS = PRIMARY * t0; stage1: PREVIOUS = scale * (PREVIOUS * t1).
                m.dual_tex_mode = CmbMaterial::kDualTexModulateThenScale;
                m.dual_tex_scale2 = (stage1Scale == 2 || stage1Scale == 4) ? (float)stage1Scale : 1.0f;
            }
        }
        // Generic per-stage TEV routing (render.multi-stage-tev). The renderer's legacy fast
        // path evaluates EXACTLY one shape: a single MODULATE(C(PRIMARY), C(TEXTURE0)) rgb
        // stage with MODULATE(A(PRIMARY), A(TEXTURE0)) alpha at alpha-scale x1 (the rgb scale
        // rides uExtra.w). That is 8232/11172 of the ROM's materials, including all of Kokiri's
        // CLOSED terrain rows — those keep the legacy path bit-identically. The three
        // byte-classified dual-texture title shapes (dual_tex_mode != 0, CLOSED title rows)
        // also keep their verified legacy path. EVERYTHING else — multi-stage chains,
        // non-MODULATE ops, CONST/PREVBUF/fragment-light sources, tex1/tex2 combines (Zora's
        // water) — routes through the generic evaluator, which implements the real PICA
        // semantics per stage.
        if (m.comb_stage_count > 0 && m.dual_tex_mode == CmbMaterial::kDualTexNone) {
            const CmbMaterial::CombStage& s0 = m.comb_stages[0];
            bool trivialSingle = m.comb_stage_count == 1 && s0.rgb_op == 0x2100 && s0.a_op == 0x2100 &&
                                 s0.rgb_src[0] == 0x8577 && s0.rgb_src[1] == 0x84C0 && s0.rgb_mod[0] == 0x0300 &&
                                 s0.rgb_mod[1] == 0x0300 && s0.a_src[0] == 0x8577 && s0.a_src[1] == 0x84C0 &&
                                 s0.a_mod[0] == 0x0302 && s0.a_mod[1] == 0x0302 && s0.a_scale == 1;
            m.tev_generic = !trivialSingle;
        }
        o += stride;
    }
    return true;
}

int Cmb::materialTexture(int matIndex) const {
    if (matIndex >= 0 && matIndex < (int)mMaterials.size()) {
        int t = mMaterials[matIndex].tex0_idx;
        return t >= 0 ? t : 0;
    }
    return 0;
}

bool Cmb::parseVatr() {
    const uint8_t* b = mData.data();
    uint32_t p = mVatrPtr;
    if (memcmp(b + p, "vatr", 4) != 0) {
        mErr = "bad vatr";
        return false;
    }
    int count = 0;
    const AttrDef* defs = Cmb_attrsDef(mVersion, &count);
    (void)defs;
    mVatr.resize(count);
    uint32_t o = p + 0x0C;
    for (int i = 0; i < count; i++) {
        uint32_t size = u32(b, o);
        uint32_t off = u32(b, o + 4); // size first, then offset
        mVatr[i].off = mVatrPtr + off;
        mVatr[i].size = size;
        o += 8;
    }
    return true;
}

bool Cmb::parseTex() {
    if (mTexPtr == 0)
        return true;
    const uint8_t* b = mData.data();
    uint32_t p = mTexPtr;
    if (memcmp(b + p, "tex ", 4) != 0)
        return true;
    uint32_t n = u32(b, p + 8);
    uint32_t o = p + 0x0C;
    mTextures.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        CmbTexture& t = mTextures[i];
        t.data_len = u32(b, o);
        t.levels = (int)(u32(b, o + 4) & 0xFFFF);
        if (t.levels < 1)
            t.levels = 1;
        t.etc1 = u8(b, o + 6) != 0;
        t.width = u16(b, o + 8);
        t.height = u16(b, o + 0x0A);
        t.fmt = u16(b, o + 0x0C);
        t.data_type = u16(b, o + 0x0E);
        t.data_offset = u32(b, o + 0x10);
        size_t e = o + 0x14;
        while (e < o + 0x24 && b[e])
            e++;
        t.name.assign((const char*)b + o + 0x14, e - (o + 0x14));
        o += 0x24;
    }
    return true;
}

std::vector<uint8_t> Cmb::textureRaw(const CmbTexture& t) const {
    size_t base = mTexdataPtr + t.data_offset;
    if (base + t.data_len > mData.size())
        return {};
    return std::vector<uint8_t>(mData.begin() + base, mData.begin() + base + t.data_len);
}

bool Cmb::parseSklm() {
    const uint8_t* b = mData.data();
    uint32_t p = mSklmPtr;
    if (memcmp(b + p, "sklm", 4) != 0) {
        mErr = "bad sklm";
        return false;
    }
    uint32_t mshsOff = p + u32(b, p + 0x08);
    uint32_t shpOff = p + u32(b, p + 0x0C);
    if (memcmp(b + mshsOff, "mshs", 4) != 0) {
        mErr = "bad mshs";
        return false;
    }
    uint32_t meshCount = u32(b, mshsOff + 8);
    uint32_t mo = mshsOff + 0x10;
    // Mesh-entry stride is VERSION-GATED like qtrs / the bone stride (0x28 vs 0x2C) and the material
    // stride (0x15C vs 0x16C): OoT3D (v6) packs mesh entries at 0x04, MM3D (v10) at 0x0C. The leading
    // sepd/material/id fields are identical; v>=7 simply carries extra per-mesh fields after them.
    // Ground truth: MM3D's engine walks the mesh table with a 0xC stride (FUN_005e1f84, recorded in
    // mm3d-decomp/docs/joker_anchors.md), and noclip's OcarinaOfTime3D/cmb.ts switches the same way.
    //
    // Reading MM3D at stride 4 walked off into the middle of the table, so most entries decoded to
    // garbage sepd indices: 27 of clocktower_0's 41 sepds ended up referenced by NO mesh and their
    // geometry was never built (only 49% of the room's prm indices reached a draw group, vs 100% for
    // OoT3D). That is what left the ground missing while upper-level geometry drew fine.
    const uint32_t meshStride = (mVersion >= 7) ? 0x0C : 0x04;
    mMeshes.resize(meshCount);
    for (uint32_t i = 0; i < meshCount; i++) {
        mMeshes[i].sepd_index = u16(b, mo);
        mMeshes[i].material_index = u8(b, mo + 2);
        mMeshes[i].mesh_id = u8(b, mo + 3);
        mo += meshStride;
    }
    if (memcmp(b + shpOff, "shp ", 4) != 0) {
        mErr = "bad shp";
        return false;
    }
    uint32_t sepdCount = u32(b, shpOff + 8);
    mSepds.resize(sepdCount);
    for (uint32_t i = 0; i < sepdCount; i++) {
        uint16_t ptr = u16(b, shpOff + 0x10 + 2 * i);
        mSepds[i] = parseSepd(shpOff + ptr);
    }
    return true;
}

Cmb::Sepd Cmb::parseSepd(uint32_t p) {
    const uint8_t* b = mData.data();
    Sepd s;
    if (memcmp(b + p, "sepd", 4) != 0) {
        mErr = "bad sepd";
        return s;
    }
    s.prim_count = u16(b, p + 8);
    int count = 0;
    const AttrDef* defs = Cmb_attrsDef(mVersion, &count);
    (void)defs;
    s.attrs.resize(count);
    uint32_t o = p + 0x24;
    for (int i = 0; i < count; i++) {
        SepdAttr& a = s.attrs[i];
        a.start = u32(b, o);
        a.scale = f32(b, o + 4);
        a.data_type = u16(b, o + 8);
        a.mode = u16(b, o + 0x0A);
        for (int k = 0; k < 4; k++)
            a.constant[k] = f32(b, o + 0x0C + 4 * k);
        a.present = true;
        o += 0x1C;
    }
    s.bone_dimension = u16(b, o);
    o += 4;
    s.prms.resize(s.prim_count);
    for (uint16_t i = 0; i < s.prim_count; i++) {
        uint16_t ptr = u16(b, o + 2 * i);
        s.prms[i] = parsePrms(p + ptr);
    }
    return s;
}

Cmb::Prms Cmb::parsePrms(uint32_t p) {
    const uint8_t* b = mData.data();
    Prms r;
    if (memcmp(b + p, "prms", 4) != 0) {
        mErr = "bad prms";
        return r;
    }
    r.skinning_mode = u16(b, p + 0x0C);
    uint16_t btCount = u16(b, p + 0x0E);
    uint32_t btOff = p + u32(b, p + 0x10);
    uint32_t prmOff = p + u32(b, p + 0x14);
    r.bone_table.resize(btCount);
    for (uint16_t i = 0; i < btCount; i++)
        r.bone_table[i] = u16(b, btOff + 2 * i);
    r.prm = parsePrm(prmOff);
    return r;
}

Cmb::Prm Cmb::parsePrm(uint32_t p) {
    const uint8_t* b = mData.data();
    Prm r;
    if (memcmp(b + p, "prm ", 4) != 0) {
        mErr = "bad prm";
        return r;
    }
    r.index_type = u16(b, p + 0x10);
    r.count = u16(b, p + 0x14);
    r.first = u16(b, p + 0x16);
    return r;
}

// An optional vertex attribute (color/normal/texCoord0/texCoord1/texCoord2) is only real data if either its mode
// is CONSTANT (attr.constant[] always readable, no VATR backing needed) or its ARRAY mode has a
// nonzero-size backing VATR buffer. parseSepd() unconditionally sets `.present = true` for every
// declared attribute slot regardless of whether the exporter actually wrote data for it (many
// CMB files declare a slot as ARRAY with a 0-byte VATR entry as a "no data here" placeholder) —
// `.present` alone is NOT a safe gate for readAttr(): reading an ARRAY attr with a 0-size VATR
// buffer reads whatever bytes happen to follow it in the vatr blob (the next attribute's data),
// silently corrupting the CPU-side vertex with garbage instead of falling back to the sensible
// default (v.color/v.nrm/v.uv's own struct-init value). Found via the title fire-glow's ~2x dim
// residual (oot3d-decomp/docs/title_logo_fireglow_cmab.md): g_title.cmb declares "color" as
// ARRAY with a 0-byte VATR buffer, so every vertex silently read whatever bytes trailed it
// (texCoord0's buffer) as a bogus per-vertex tint instead of the intended baked white.
bool Cmb::attrHasData(const SepdAttr& attr, int attrSlot) const {
    if (attr.mode == 1)
        return true; // CONSTANT: attr.constant[] is always valid.
    return attrSlot >= 0 && attrSlot < (int)mVatr.size() && mVatr[attrSlot].size > 0;
}

void Cmb::readAttr(const SepdAttr& attr, int attrSlot, uint32_t idx, int comps, float* out) const {
    if (attr.mode == 1) { // constant
        for (int i = 0; i < comps; i++)
            out[i] = attr.constant[i];
        return;
    }
    const uint8_t* b = mData.data();
    uint32_t base = mVatr[attrSlot].off;
    int sz = dtSize(attr.data_type);
    uint32_t off = base + attr.start + idx * comps * sz;
    for (int i = 0; i < comps; i++)
        out[i] = dtRead(b, off + i * sz, attr.data_type) * attr.scale;
}

std::string Cmb::indexRangeReport() const {
    const uint8_t* b = mData.data();
    int count = 0;
    const AttrDef* defs = Cmb_attrsDef(mVersion, &count);
    int slotPos = -1;
    for (int i = 0; i < count; i++)
        if (!strcmp(defs[i].name, "position"))
            slotPos = i;
    std::string out;
    char line[256];
    { // Index-region accounting: does sum(count*declaredWidth) match the byte span between the
      // index buffer (+0x40) and the texture data (+0x44)? That distinguishes a flat mixed-width
      // stream (where `first` would be a BYTE offset) from an element-indexed array.
        long sumBytes = 0, sumElems = 0, maxFirstPlusCount = 0;
        for (const auto& sp : mSepds)
            for (const auto& pr : sp.prms) {
                sumBytes += (long)pr.prm.count * dtSize(pr.prm.index_type);
                sumElems += pr.prm.count;
                long e = (long)pr.prm.first + pr.prm.count;
                if (e > maxFirstPlusCount)
                    maxFirstPlusCount = e;
            }
        snprintf(line, sizeof(line),
                 "  [idx] mIndexCount=%u idxPtr=%u texdataPtr=%u span=%ld | sum(count*w)=%ld sum(count)=%ld "
                 "max(first+count)=%ld\n",
                 mIndexCount, mIdxPtr, mTexdataPtr, (long)mTexdataPtr - (long)mIdxPtr, sumBytes, sumElems,
                 maxFirstPlusCount);
        out += line;
    }
    { // Per-width-class ranges: if the stream is segregated by index width, `first` would be an
      // element index WITHIN its class and each class would occupy a contiguous span.
        std::map<uint16_t, long> maxF, cnt, sumC;
        for (const auto& sp : mSepds)
            for (const auto& pr : sp.prms) {
                uint16_t t = pr.prm.index_type;
                cnt[t]++;
                sumC[t] += pr.prm.count;
                long e = (long)pr.prm.first + pr.prm.count;
                if (e > maxF[t])
                    maxF[t] = e;
            }
        for (const auto& kv : cnt) {
            snprintf(line, sizeof(line),
                     "  [cls] itype=0x%X prms=%ld sumCount=%ld max(first+count)=%ld  bytesIfOwnClass=%ld\n", kv.first,
                     kv.second, sumC[kv.first], maxF[kv.first], sumC[kv.first] * dtSize(kv.first));
            out += line;
        }
    }
    { // Is `first` the running ELEMENT index over prms in file order? If so, with MIXED widths the
      // byte offset must accumulate preceding widths, and first*thisWidth is wrong.
        long elems = 0, bytes = 0;
        int mismatch = 0, byteMismatch = 0, total = 0;
        long firstBad = -1;
        for (const auto& sp : mSepds)
            for (const auto& pr : sp.prms) {
                total++;
                if ((long)pr.prm.first != elems) {
                    if (mismatch == 0)
                        firstBad = (long)pr.prm.first - elems;
                    mismatch++;
                }
                if ((long)pr.prm.first != bytes)
                    byteMismatch++;
                elems += pr.prm.count;
                bytes += (long)pr.prm.count * dtSize(pr.prm.index_type);
            }
        snprintf(line, sizeof(line),
                 "  [first] prms=%d  vs runningELEMENT mismatches=%d (delta=%ld)  vs runningBYTE mismatches=%d  "
                 "totalElems=%ld totalBytes=%ld\n",
                 total, mismatch, firstBad, byteMismatch, elems, bytes);
        out += line;
    }
    { // VATR slot layout: are the per-attribute buffers contiguous, and does the last one end at
      // the end of the CMB? A short final size would explain a tail sepd overrunning.
        snprintf(line, sizeof(line), "  [vatr] cmbSize=%zu vatrPtr=%u slots=%zu\n", mData.size(), mVatrPtr,
                 mVatr.size());
        out += line;
        for (size_t i = 0; i < mVatr.size(); i++) {
            snprintf(line, sizeof(line), "    slot%zu off=%-8u size=%-8u end=%-8u\n", i, mVatr[i].off, mVatr[i].size,
                     mVatr[i].off + mVatr[i].size);
            out += line;
        }
    }
    if (slotPos < 0 || slotPos >= (int)mVatr.size())
        return "(no position slot)";
    // Only sepds a MESH actually references are ever drawn; an unreferenced sepd overrunning is
    // harmless bookkeeping, a referenced one is a real defect. Distinguish them.
    std::map<size_t, int> refCount;
    for (const auto& m : mMeshes)
        refCount[m.sepd_index]++;
    for (size_t si = 0; si < mSepds.size(); si++) {
        const Sepd& sepd = mSepds[si];
        const SepdAttr& a = sepd.attrs[slotPos];
        int sz = dtSize(a.data_type);
        // vertices addressable from this sepd's start within the position buffer
        long cap = ((long)mVatr[slotPos].size - (long)a.start) / (3 * sz);
        long maxIdx = -1;
        for (const auto& prms : sepd.prms) {
            const Prm& prm = prms.prm;
            int isz = dtSize(prm.index_type);
            for (uint16_t k = 0; k < prm.count; k++) {
                // must mirror buildDrawGroups: base scaled by 2 (u16 slots), element at its own width
                long idx = (long)dtRead(b, mIdxPtr + (size_t)prm.first * 2 + (size_t)k * isz, prm.index_type);
                if (idx > maxIdx)
                    maxIdx = idx;
            }
        }
        if (getenv("CMB_RANGE_ALL") != nullptr || maxIdx >= cap) {
            for (const auto& prms : sepd.prms) {
                const Prm& pr = prms.prm;
                snprintf(line, sizeof(line), "      prm first=%-6u count=%-6u itype=0x%X skin=%u boneTbl=%zu\n",
                         pr.first, pr.count, pr.index_type, prms.skinning_mode, prms.bone_table.size());
                out += line;
            }
            snprintf(line, sizeof(line),
                     "  sepd%-3zu %-8s maxIdx=%-5ld cap=%-5ld start=%-7u mode=%u dt=0x%X meshRefs=%d%s\n", si,
                     (maxIdx >= cap ? "OVERRUN" : "ok"), maxIdx, cap, a.start, a.mode, a.data_type, refCount[si],
                     refCount[si] ? (maxIdx >= cap ? "  <-- DRAWN, REAL DEFECT" : "  drawn") : "  (unreferenced)");
            out += line;
        }
    }
    if (out.empty())
        out = "  (no sepd overruns)\n";
    return out;
}

std::map<uint16_t, int> Cmb::indexTypeHistogram() const {
    std::map<uint16_t, int> h;
    for (const auto& sepd : mSepds)
        for (const auto& prms : sepd.prms)
            h[prms.prm.index_type]++;
    return h;
}

std::vector<int> Cmb::meshBones(size_t i) const {
    std::vector<int> out;
    if (i >= mMeshes.size() || mMeshes[i].sepd_index >= mSepds.size())
        return out;
    for (const auto& prms : mSepds[mMeshes[i].sepd_index].prms)
        for (uint16_t bid : prms.bone_table)
            if (std::find(out.begin(), out.end(), (int)bid) == out.end())
                out.push_back((int)bid);
    std::sort(out.begin(), out.end());
    return out;
}

size_t Cmb::unreferencedSepdCount() const {
    std::vector<uint8_t> used(mSepds.size(), 0);
    for (const auto& m : mMeshes)
        if (m.sepd_index < used.size())
            used[m.sepd_index] = 1;
    size_t n = 0;
    for (uint8_t u : used)
        n += (u ? 0 : 1);
    return n;
}

std::vector<CmbDrawGroup> Cmb::buildDrawGroups() const {
    return buildDrawGroupsSkinned(nullptr, 0, {});
}
std::vector<CmbDrawGroup> Cmb::buildDrawGroups(const std::vector<uint8_t>& skipMesh) const {
    return buildDrawGroupsSkinned(nullptr, 0, skipMesh);
}
std::vector<CmbDrawGroup> Cmb::buildDrawGroupsSkinned(const std::array<float, 16>* skinMats, size_t nMats) const {
    return buildDrawGroupsSkinned(skinMats, nMats, {});
}

std::vector<CmbDrawGroup> Cmb::buildDrawGroupsSkinned(const std::array<float, 16>* skinMats, size_t nMats,
                                                      const std::vector<uint8_t>& skipMesh) const {
    const uint8_t* b = mData.data();
    int count = 0;
    const AttrDef* defs = Cmb_attrsDef(mVersion, &count);
    // locate attribute slots by name
    int slotPos = -1, slotNrm = -1, slotUv0 = -1, slotUv1 = -1, slotUv2 = -1;
    int slotBi = -1, slotBw = -1, slotCol = -1;
    for (int i = 0; i < count; i++) {
        if (!strcmp(defs[i].name, "position"))
            slotPos = i;
        else if (!strcmp(defs[i].name, "normal"))
            slotNrm = i;
        else if (!strcmp(defs[i].name, "color"))
            slotCol = i;
        else if (!strcmp(defs[i].name, "texCoord0"))
            slotUv0 = i;
        else if (!strcmp(defs[i].name, "texCoord1"))
            slotUv1 = i;
        else if (!strcmp(defs[i].name, "texCoord2"))
            slotUv2 = i;
        else if (!strcmp(defs[i].name, "boneIndices"))
            slotBi = i;
        else if (!strcmp(defs[i].name, "boneWeights"))
            slotBw = i;
    }

    auto skinOf = [&](int boneId) -> Mat4 {
        if (skinMats && boneId >= 0 && (size_t)boneId < nMats)
            return skinMats[boneId];
        return matId();
    };

    // Accumulate per (material index, mesh_id, HasColor). Splitting by mesh_id (not just material) keeps
    // variant meshes that share a material in distinct groups so the renderer can toggle them
    // by mesh_id per frame (see CmbDrawGroup). HasColor is a draw-uniform CmbVShader branch, so
    // geometry with and without a color attribute cannot share one group even when its material
    // and mesh id match.
    std::vector<CmbDrawGroup> groups;
    auto groupFor = [&](int mat, int mid, bool hasColor) -> CmbDrawGroup& {
        for (auto& g : groups)
            if (g.material_index == mat && g.mesh_id == mid && g.has_color == hasColor)
                return g;
        groups.push_back({ mat, mid, hasColor, {} });
        return groups.back();
    };

    for (size_t mi = 0; mi < mMeshes.size(); mi++) {
        if (mi < skipMesh.size() && skipMesh[mi])
            continue; // drop culled variant meshes
        const Mesh& mesh = mMeshes[mi];
        if (mesh.sepd_index >= mSepds.size())
            continue;
        const Sepd& sepd = mSepds[mesh.sepd_index];
        int coordSources[3] = { 0, 0, 0 };
        if (mesh.material_index >= 0 && mesh.material_index < (int)mMaterials.size()) {
            const CmbMaterial& material = mMaterials[mesh.material_index];
            coordSources[0] = material.coord0_source;
            coordSources[1] = material.coord1_source;
            coordSources[2] = material.coord2_source;
        }
        const int uvSlots[3] = { slotUv0, slotUv1, slotUv2 };
        bool uvHasData[3] = {};
        for (int uvIndex = 0; uvIndex < 3; ++uvIndex) {
            const int slot = uvSlots[uvIndex];
            uvHasData[uvIndex] = slot >= 0 && sepd.attrs[slot].present && attrHasData(sepd.attrs[slot], slot);
        }
        int bd = sepd.bone_dimension;
        bool hasNormal = slotNrm >= 0 && sepd.attrs[slotNrm].present && attrHasData(sepd.attrs[slotNrm], slotNrm);
        bool hasColor = slotCol >= 0 && sepd.attrs[slotCol].present && attrHasData(sepd.attrs[slotCol], slotCol);
        CmbDrawGroup& g = groupFor(mesh.material_index, mesh.mesh_id, hasColor);
        for (const auto& prms : sepd.prms) {
            const Prm& prm = prms.prm;
            int boneId = prms.bone_table.empty() ? 0 : prms.bone_table[0];
            // A mesh with bone_dimension > 1 stores its verts in MODEL space and blends `bd`
            // bones per vertex (smooth skinning); bd == 1 is rigid (bone-local verts -> the
            // bound bone's bind-world matrix). The per-vertex weights/indices may be ARRAY
            // (one set per vertex) OR CONSTANT (a single set shared by every vertex, e.g. a
            // head riding a 2-bone neck/head chain with constant indices + per-vertex weights).
            // The CONSTANT case is STILL smooth/model-space — classifying it rigid double-applies
            // the bind matrix and flings the mesh away (the Kokiri "floating head": kokirimaster/
            // kokiripeople face & hat are bd=2 with constant boneIndices). So smooth == bd > 1.
            bool smooth = bd > 1 && slotBi >= 0 && slotBw >= 0;
            // RIGID_SKINNING (skinning_mode == 1): the mesh is bd == 1 (one bone per vertex,
            // weight 1) BUT each vertex carries its OWN bone index (boneIndices, a local index
            // into bone_table), and is stored in THAT bone's local space. Binding every vertex
            // to bone_table[0] (the plain single-bone rigid path) collapses the whole mesh onto
            // one bone and scatters the rest of the rig — the stalchild rendered as a pile of
            // detached skull/ribcage/limb pieces (#109). Resolve the bone (and its bind matrix)
            // per vertex below. SINGLE_BONE (mode 0) keeps the one-bone path; SMOOTH is bd > 1.
            bool rigidPV = !smooth && prms.skinning_mode == 1 && slotBi >= 0 && sepd.attrs[slotBi].present &&
                           prms.bone_table.size() > 1;
            // model-space bake: rigid verts come in bone-local space -> bound bone's
            // bind world; smooth verts are already model space (identity). rigidPV resolves
            // its matrix per vertex inside the loop, so leave M at the bone_table[0] default.
            Mat4 M = smooth ? matId() : (boneId < (int)mBoneMatrix.size() ? mBoneMatrix[boneId] : matId());
            // prm.first is expressed in 2-BYTE SLOTS and is ALWAYS scaled by 2 to get the byte
            // offset — independent of the element width given by prm.index_type. Ground truth:
            // noclip's OcarinaOfTime3D/cmb.ts does `prm.offset = getUint16(0x16) * 2`, and MM3D's
            // engine allocates the index region as `index_count << 1` (FUN_005e1994), i.e. u16
            // slots. The ELEMENT is still read at its declared width.
            //
            // OoT3D is unaffected: every prm there is USHORT, so first*2 == first*dtSize. MM3D room
            // CMBs mix UBYTE and USHORT prms, and for the UBYTE ones scaling by 1 landed mid-buffer
            // and produced out-of-range indices (clocktower_0 sepd40: first=28643 -> 28643 instead
            // of 57286, indices up to 90 against an 8-vertex window).
            int isz = dtSize(prm.index_type);
            size_t ibase = mIdxPtr + (size_t)prm.first * 2;
            // per-vertex bone bindings (model-space terms): rigid -> single bound
            // bone w=1; smooth -> boneIndices(local into bone_table)+boneWeights.
            int biSz = (slotBi >= 0) ? dtSize(sepd.attrs[slotBi].data_type) : 1;
            uint32_t biBase = (slotBi >= 0) ? mVatr[slotBi].off : 0;
            std::vector<CmbVertex> verts;
            verts.reserve(prm.count);
            for (uint16_t k = 0; k < prm.count; k++) {
                uint32_t idx = (uint32_t)dtRead(b, ibase + (size_t)k * isz, prm.index_type);
                float pos[3], nrm[3] = { 0, 0, 1 }, rawUv[3][2] = {};
                readAttr(sepd.attrs[slotPos], slotPos, idx, 3, pos);
                if (hasNormal)
                    readAttr(sepd.attrs[slotNrm], slotNrm, idx, 3, nrm);
                for (int uvIndex = 0; uvIndex < 3; ++uvIndex) {
                    if (uvHasData[uvIndex]) {
                        readAttr(sepd.attrs[uvSlots[uvIndex]], uvSlots[uvIndex], idx, 2, rawUv[uvIndex]);
                    }
                }
                // RIGID_SKINNING: resolve this vertex's own bone + bind matrix (see rigidPV above).
                int vBone = boneId;
                Mat4 Mv = M;
                if (rigidPV) {
                    const SepdAttr& bia = sepd.attrs[slotBi];
                    int li = (bia.mode == 1)
                                 ? (int)bia.constant[0]
                                 : (int)dtRead(b, biBase + bia.start + idx * (uint32_t)bd * biSz, bia.data_type);
                    vBone = (li >= 0 && li < (int)prms.bone_table.size()) ? prms.bone_table[li] : boneId;
                    Mv = (vBone >= 0 && vBone < (int)mBoneMatrix.size()) ? mBoneMatrix[vBone] : matId();
                }
                // to model space (rigid bake / smooth identity)
                float mp[3], mn[3];
                matApplyPos(Mv, pos, mp);
                matApplyDir(Mv, nrm, mn);
                // gather (boneId, weight)
                int ids[8];
                float wts[8];
                int nb = 0;
                if (!smooth) {
                    ids[0] = vBone;
                    wts[0] = 1.0f;
                    nb = 1;
                } else {
                    const SepdAttr& bia = sepd.attrs[slotBi];
                    float wbuf[8] = {};
                    readAttr(sepd.attrs[slotBw], slotBw, idx, bd, wbuf);
                    // boneIndices: CONSTANT -> one local-index set (bia.constant[e]) shared by
                    // every vertex; ARRAY -> per-vertex local indices in the attribute stream.
                    uint32_t bioff = biBase + bia.start + idx * (uint32_t)bd * biSz;
                    for (int e = 0; e < bd && e < 8; e++) {
                        float w = wbuf[e];
                        if (w <= 0)
                            continue;
                        int li = (bia.mode == 1) ? (int)bia.constant[e]
                                                 : (int)dtRead(b, bioff + (size_t)e * biSz, bia.data_type);
                        int gb = (li >= 0 && li < (int)prms.bone_table.size()) ? prms.bone_table[li] : boneId;
                        ids[nb] = gb;
                        wts[nb] = w;
                        nb++;
                    }
                    if (nb == 0) {
                        ids[0] = boneId;
                        wts[0] = 1.0f;
                        nb = 1;
                    }
                }
                // weighted skin blend of the model-space vert (CPU). With identity
                // skinMats this leaves pos/nrm in model space (= bind pose).
                CmbVertex v{};
                for (int e = 0; e < nb; e++) {
                    Mat4 S = skinOf(ids[e]);
                    float sp[3], sn[3];
                    matApplyPos(S, mp, sp);
                    matApplyDir(S, mn, sn);
                    for (int c = 0; c < 3; c++) {
                        v.pos[c] += wts[e] * sp[c];
                        v.nrm[c] += wts[e] * sn[c];
                    }
                }
                // Resolve each texture coordinator's selected source while all three CMB
                // attribute streams are available. Missing/invalid sources retain texCoord0,
                // matching the previous path rather than sampling an uninitialized coordinate.
                float* resolvedUv[3] = { v.uv, v.uv1, v.uv2 };
                for (int textureUnit = 0; textureUnit < 3; ++textureUnit) {
                    int source = coordSources[textureUnit];
                    if (source < 0 || source >= 3 || !uvHasData[source]) {
                        source = 0;
                    }
                    resolvedUv[textureUnit][0] = rawUv[source][0];
                    resolvedUv[textureUnit][1] = rawUv[source][1];
                }
                // Per-vertex color: OoT3D's baked scene lighting (walls dimmed, AO on the
                // ground) and the additive light-shaft/god-ray alpha falloff. Defaults to
                // white when the attribute is absent, so untinted models are unaffected.
                if (slotCol >= 0 && sepd.attrs[slotCol].present && attrHasData(sepd.attrs[slotCol], slotCol)) {
                    readAttr(sepd.attrs[slotCol], slotCol, idx, 4, v.color);
                }
                // record the (model-space) bone bindings for GPU skinning. The CPU
                // blend above bakes the pose into pos/nrm; GPU skinning instead uploads
                // these bindings + model-space pos (skinMats=identity) and applies the
                // pose via the uBones uniform. Store up to 4 (max bone_dim seen = 3).
                for (int e = 0; e < nb && e < 4; e++) {
                    v.boneIds[e] = (float)ids[e];
                    v.weights[e] = wts[e];
                }
                verts.push_back(v);
            }
            // triangle list (matches cmb.py: groups of 3, drop trailing remainder)
            size_t n = verts.size();
            for (size_t i = 0; i + 2 < n; i += 3) {
                g.verts.push_back(verts[i]);
                g.verts.push_back(verts[i + 1]);
                g.verts.push_back(verts[i + 2]);
            }
        }
    }
    return groups;
}

} // namespace Zelda3D
