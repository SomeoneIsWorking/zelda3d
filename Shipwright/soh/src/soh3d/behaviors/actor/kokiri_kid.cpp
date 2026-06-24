// SoH3D behavior: En_Ko Kokiri kids — head/torso tracking + eye material-anim, ported from OoT3D.
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

#include <cstring>

extern "C" {
const char* SoH3D_AutoModelZar(int modelId);
void SoH3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
int SoH3D_FacialFrameTex(int modelId, int materialIndex, int frame); // -1 = none/out-of-range
}

// Verification override (REPL `faceframe <n>`): when >= 0, force the eye frame, bypassing the live
// index, so the facial channel can be driven deterministically. Defined in soh3d_anim_override.cpp.
extern int gSoH3dFaceForce;

namespace SoH3D {

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
        // interactInfo is live: SoH3D forces replaced actors to keep updating regardless of culling
        // (z_actor.c Actor_UpdateAll), so headRot/torsoRot are the current, clamped track values.
        applyTrackRot(modelId, kHeadBone, ko->interactInfo.headRot);
        applyTrackRot(modelId, kTorsoBone, ko->interactInfo.torsoRot);
    }

    // --- Eye material-anim -----------------------------------------------------------------------
    if (facial) {
        // km1 (boy) eye = material 1. kw1 shares one body CMB across the girl types + Fado, with the
        // eye baked as material 1 for Fado and material 2 for the girls (ENKO_TYPE in params low byte).
        const char* zar = SoH3D_AutoModelZar(modelId);
        int eyeMat = 1;
        if (zar != nullptr && std::strstr(zar, "zelda_kw1") != nullptr) {
            eyeMat = ((ko->actor.params & 0xFF) == ENKO_TYPE_CHILD_FADO) ? 1 : 2;
        }
        int idx = (gSoH3dFaceForce >= 0) ? gSoH3dFaceForce : ko->eyeTextureIndex;
        int tex = SoH3D_FacialFrameTex(modelId, eyeMat, idx);
        SoH3D_GL_SetMatTexOverride(modelId, eyeMat, tex); // tex<0 clears -> base sprite
    }
}

} // namespace SoH3D
