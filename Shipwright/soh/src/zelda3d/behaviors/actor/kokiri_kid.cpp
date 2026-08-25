// Zelda3D behavior: En_Ko Kokiri kids — head/torso tracking + eye material-anim, ported from OoT3D.
//
// OoT3D ground truth (oot3d-decomp/docs/enko_override_and_ensa_facial.md):
//   EnKo_OverrideLimbDraw @ 0x2335b4:
//     limb 10 (HEAD)  -> shared rotate helper(mtx, interactInfo.headRot)
//     limb  9 (TORSO) -> shared rotate helper(mtx, interactInfo.torsoRot)
//   shared helper FUN_0034e01c = RotateX(rot.y)·RotateZ(rot.x), NO Y negation, NO pivot.
//   EnKo_Draw eye: material-anim slot fed eyeTextureIndex (km1 boy = eye material 1; kw1 body bakes
//   Fado = material 1, the girls = material 2).
//
// Why a port and not the old raw-offset read: SoH is a 64-bit build, so the N64 struct-offset
// comments in z_en_ko.h do NOT match the runtime layout (8-byte pointers shift every field past the
// first pointer). The legacy track read interactInfo at the raw N64 byte offset 0x1F0 and got
// MISREAD memory (non-physical "headRot" up to 129°) — the real cause of #116's twisted heads, which
// the 903c629 clamp only masked. Here we read interactInfo through the C struct: the compiler resolves
// the correct offset, the value is the genuine (Npc_TrackPoint-clamped) head/torso rotation, and the
// faithful RotateX·RotateZ port applies cleanly with no plausibility guard.
#include "z64.h"
#include "src/overlays/actors/ovl_En_Ko/z_en_ko.h"
#include "kokiri_kid.h"
#include "npc_draw.h"

#include <cstring>

extern "C" {
const char* Zelda3D_AutoModelZar(int modelId);
}

namespace Zelda3D {

// OoT3D En_Ko skeleton: head = bone 10, torso = bone 9 (decomp EnKo_OverrideLimbDraw).
static constexpr int kHeadBone = 10;
static constexpr int kTorsoBone = 9;

s16 KokiriKidBehavior::actorId() const {
    return ACTOR_EN_KO;
}

void KokiriKidBehavior::applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {
    EnKo* ko = reinterpret_cast<EnKo*>(actor);

    // --- Head/torso tracking ---------------------------------------------------------------------
    if (track) {
        // interactInfo is live: Zelda3D forces replaced actors to keep updating regardless of culling
        // (z_actor.c Actor_UpdateAll), so headRot/torsoRot are the current, clamped track values.
        applyHeadTorsoTrack(modelId, kHeadBone, kTorsoBone, ko->interactInfo);
    }

    // --- Eye material-anim -----------------------------------------------------------------------
    if (facial) {
        // km1 (boy) eye = material 1. kw1 shares one body CMB across the girl types + Fado, with the
        // eye baked as material 1 for Fado and material 2 for the girls (ENKO_TYPE in params low byte).
        const char* zar = Zelda3D_AutoModelZar(modelId);
        int eyeMat = 1;
        if (zar != nullptr && std::strstr(zar, "zelda_kw1") != nullptr) {
            eyeMat = ((ko->actor.params & 0xFF) == ENKO_TYPE_CHILD_FADO) ? 1 : 2;
        }
        applyFacialFrame(modelId, eyeMat, ko->eyeTextureIndex);
    }
}

} // namespace Zelda3D

// The running 3DS game selects several En_Ko idle poses by actor type rather than by the N64
// animation pointer. Keeping this resolver with the rest of En_Ko's draw policy prevents the
// shared retarget path from becoming a second authority for Kokiri pose selection.
extern "C" const char* Zelda3D_EnKoCsabOverride(int modelId, Actor* actor) {
    (void)modelId;
    if (actor == nullptr || actor->id != ACTOR_EN_KO) {
        return nullptr;
    }

    switch (actor->params & 0xFF) {
        case ENKO_TYPE_CHILD_0:
            return "km1_ishi_wait"; // oracle 19f; lifting-rock wait
        case ENKO_TYPE_CHILD_2:
            return "km1_ijiiji"; // oracle 20f; impatient boy
        case ENKO_TYPE_CHILD_3:
            return "km1_out_in_pose3"; // oracle 14f; unique-duration match
        case ENKO_TYPE_CHILD_4:
            return "km1_kusakari"; // oracle 19f; grass cutting
        case ENKO_TYPE_CHILD_1:
            return "kw1_out_in_pose1"; // oracle 25f; first girl standby pose
        case ENKO_TYPE_CHILD_5:
            return "kw1_out_in_pose2"; // oracle 21f; unique-duration match
        case ENKO_TYPE_CHILD_6:
            return "kw1_out_in_pose3"; // oracle 25f; distinct girl standby pose
        case ENKO_TYPE_CHILD_FADO:
            return "fad_n_wait"; // oracle 40f; Fado idle
        default:
            return nullptr;
    }
}
