// Zelda3D behavior: En_Hy Hylian townsfolk — head/torso track + eye material-anim.
//
// OoT3D ground truth (oot3d-decomp/docs/enko_override_and_ensa_facial.md, sweep 2):
//   EnHy_OverrideLimbDraw @ 0x16d9d4 picks head/torso limbs PER EnHyType (params & 0x7f) from a
//   per-type table — consistent per body archetype, so one row per OoT3D body zar. Shared rotate
//   helper: RotateX(rot.y)·RotateZ(rot.x), no Y negation, no pivot. interactInfo headRot/torsoRot.
//   EnHy_Draw @ 0x1b4944: eye material-anim slot 0 = curEyeIndex (DIRECT). No mouth. CMB eye material
//   3 for the men (boj/ahg/bji), 1 for the women (aob/bob). cne/cob/bba have no eye anim (track only).
// State read through the EnHy C struct (64-bit build — never raw N64 offsets).
#include "z64.h"
#include "src/overlays/actors/ovl_En_Hy/z_en_hy.h"
#include "npc_draw.h"
#include "townsfolk.h"
#include "townsfolk_body_colors.h"
#include "fast/zelda3d_material_overrides.h"

#include <cstring>

extern "C" {
const char* Zelda3D_AutoModelZar(int modelId);
}

namespace Zelda3D {

// Per body archetype (matched by a substring of the OoT3D body zar): head bone, torso bone, eye CMB
// material (-1 = no eye anim). From the EnHy per-type bone table + the eye-material decomp.
struct HyArchetype {
    const char* zarKey;
    int headBone;
    int torsoBone;
    int eyeMaterial;
};

static constexpr HyArchetype kArchetypes[] = {
    { "zelda_boj", 9, 8, 3 },                                                         // Hylian man 1
    { "zelda_ahg", 15, 8, 3 },                                                        // Hylian man 2
    { "zelda_bji", 10, 9, 3 },                                                        // Hylian old man
    { "zelda_cne", 14, 7, -1 }, { "zelda_aob", 10, 9, 1 },                            // Hylian woman 1
    { "zelda_cob", 12, 5, -1 }, { "zelda_bba", 7, 6, -1 }, { "zelda_bob", 14, 7, 1 }, // Hylian woman 3
};

s16 TownsfolkBehavior::actorId() const {
    return ACTOR_EN_HY;
}

void TownsfolkBehavior::applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {
    const char* zar = Zelda3D_AutoModelZar(modelId);
    if (zar == nullptr) {
        return;
    }
    const HyArchetype* arch = nullptr;
    for (const auto& a : kArchetypes) {
        if (std::strstr(zar, a.zarKey) != nullptr) {
            arch = &a;
            break;
        }
    }
    if (arch == nullptr) {
        return; // unknown body archetype — leave the CSAB pose untouched
    }

    EnHy* hy = reinterpret_cast<EnHy*>(actor);
    if (track) {
        applyHeadTorsoTrack(modelId, arch->headBone, arch->torsoBone, hy->interactInfo);
    }
    if (facial && arch->eyeMaterial >= 0) {
        applyFacialFrame(modelId, arch->eyeMaterial, hy->curEyeIndex);
    }
    // Per-type body-color overrides. Ported from EnHy_Draw
    // (oot3d-decomp/build/decomp/001b4944.c): the game writes two per-material CONSTANT-color
    // overrides for a townsfolk actor based on (params & 0x7f). Without this every townsfolk
    // clothing material renders BLACK — the CMB-file default matConstant values are
    // (0, 0, 0, 1) and stage 1 = MODULATE(PREV, CONST) zeros out the fragment.
    TownsfolkMatConstOverride bodyOv[2];
    int type = actor->params & 0x7F;
    int nOv = TownsfolkBodyColorOverrides(type, bodyOv);
    for (int i = 0; i < nOv; i++) {
        Zelda3D_GL_SetMatConstOverride(modelId, bodyOv[i].matIdx, bodyOv[i].constIdx, bodyOv[i].rgba[0],
                                       bodyOv[i].rgba[1], bodyOv[i].rgba[2], bodyOv[i].rgba[3]);
    }
}

} // namespace Zelda3D

// OoT3D chooses these En_Hy idle clips from the per-type animation pool. The automatic model
// default is only correct for types omitted from this switch.
extern "C" const char* Zelda3D_EnHyCsabOverride(int modelId, Actor* actor) {
    (void)modelId;
    if (actor == nullptr || actor->id != ACTOR_EN_HY) {
        return nullptr;
    }

    switch (actor->params & 0x7F) {
        case 4:
        case 17:
            return "Ahg2_8";
        case 13:
        case 20:
            return "Ahg2_18";
        case 6:
            return "Bba_n_wait";
        case 15:
        case 19:
            return "Bji2_20";
        case 3:
            return "Boj2_5";
        case 5:
            return "Boj2_9";
        case 9:
            return "Boj_13";
        case 10:
            return "Boj_14";
        case 12:
            return "Boj2_17";
        case 14:
            return "Boj2_19";
        case 16:
            return nullptr; // Boj_matsu is already the correct automatic default.
        case 8:
            return "Cne_n_wait";
        case 11:
            return "Cne2_15";
        default:
            return nullptr;
    }
}
