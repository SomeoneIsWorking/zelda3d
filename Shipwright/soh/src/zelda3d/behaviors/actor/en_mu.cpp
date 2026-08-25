// Zelda3D behavior: En_Mu (Market Day haggling townspeople) — per-material CONSTANT overrides.
//
// Ground truth (SoH z_en_mu.c EnMu_Draw): the N64 draw picks one of two 5-colour palettes by
// (params & 1) and pushes each colour into a segment (0x08..0x0C) as an EnvColor. The N64
// SkelAnime resolves those segments in the clothing DL's material sub-lists.
//
//     colors[0] = { (100,130,235), (160,250,60), (90, 60, 20), (30,240,200), (140, 70, 20) }
//     colors[1] = reverse of colors[0]
//
// OoT3D's `marketpeople.cmb` (in `/actor/zelda_mu.zar`) has the same 5 clothing colours but
// packages each as a per-material CONSTANT slot. Without a runtime override every clothing
// material renders BLACK (the CMB file defaults matConstant = (0,0,0,1) and the material's
// TEV stage 1 = MODULATE(PREV, CONST) zeroes the fragment). The visible symptom at Market
// Day (ent 0xB1, dayTime 0x8001) was two townspeople rendered as solid black voids next to
// their partially-textured skin.
//
// The material→constIdx pairs actually referenced by marketpeople.cmb (dumped via REPL
// `sgdump 2012` at the live Market Day, groups g1/g2/g3/g6/g7/g8 with combUsesConst=1):
//     (mat=0, constIdx=4)
//     (mat=1, constIdx=3)
//     (mat=2, constIdx=2)
//     (mat=3, constIdx=1)
// Material 4 is the skin/head — it does not consume CONSTANT and renders correctly without
// an override.
//
// The palette→(mat, constIdx) binding is inferred: five palette entries, four CMB
// material-CONSTANT pairs. Binding is index-parallel: palette[0..3] → (0,4)/(1,3)/(2,2)/(3,1)
// in order. palette[4] is left unbound for now — it either drives the skin (which doesn't
// need a CONSTANT) or belongs to a fifth material CONSTANT we haven't seen in the sgdump.
// TODO(enmu-followup): (1) verify the palette→material mapping by decompiling OoT3D
// EnMu_Draw (private oot3d-decomp; the RE would confirm which OoT3D material corresponds
// to each of the N64 0x08..0x0C segments). (2) once mapped, bind palette[4] too.
//
// Verified by tools/enmu_close_test.py which asserts the [MATCONST] log line lands for the
// marketpeople model at Market Day.
#include "z64.h"
#include "src/overlays/actors/ovl_En_Mu/z_en_mu.h"
#include "en_mu.h"
#include "fast/zelda3d_material_overrides.h"

namespace Zelda3D {

// N64 EnMu_Draw colour palettes (SoH z_en_mu.c:204-207). Packed as 0..255 bytes; the shader
// wants 0..1 floats, so divide at bind time.
static constexpr unsigned char kEnMuPalette[2][5][3] = {
    { { 100, 130, 235 }, { 160, 250, 60 }, { 90, 60, 20 }, { 30, 240, 200 }, { 140, 70, 20 } },
    { { 140, 70, 20 }, { 30, 240, 200 }, { 90, 60, 20 }, { 160, 250, 60 }, { 100, 130, 235 } },
};

// Palette-slot -> (materialIndex, constIdx) binding for marketpeople.cmb; see file header.
struct MatConstSlot {
    int materialIndex;
    int constIdx;
};
static constexpr MatConstSlot kMarketPeopleSlots[4] = {
    { 0, 4 },
    { 1, 3 },
    { 2, 2 },
    { 3, 1 },
};

s16 EnMuBehavior::actorId() const {
    return ACTOR_EN_MU;
}

void EnMuBehavior::applyDrawOverrides(int modelId, Actor* actor, bool /*track*/, bool /*facial*/) {
    if (modelId < 0 || actor == nullptr) {
        return;
    }
    int palette = actor->params & 1; // N64 uses this->actor.params[0/1] as the palette index
    for (const auto& slot : kMarketPeopleSlots) {
        const auto& c = kEnMuPalette[palette][slot.materialIndex]; // parallel index by slot order
        Zelda3D_GL_SetMatConstOverride(modelId, slot.materialIndex, slot.constIdx, c[0] / 255.0f, c[1] / 255.0f,
                                       c[2] / 255.0f, 1.0f);
    }
}

} // namespace Zelda3D
