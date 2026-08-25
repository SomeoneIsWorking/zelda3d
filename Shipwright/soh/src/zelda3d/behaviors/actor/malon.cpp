// Zelda3D behaviors: Malon (En_Ma1 child, En_Ma2/En_Ma3 adult) — head/torso track + eye/mouth.
//
// OoT3D ground truth (oot3d-decomp/docs/enko_override_and_ensa_facial.md):
//   En_Ma1 OverrideLimbDraw @ 0x233614: limb 7 (HEAD) <- headRot, limb 6 (TORSO) <- torsoRot.
//     EnMa1_Draw @ 0x1d9d50: eye slot 0 = eyeIndex, mouth slot 1 = mouthIndex (both DIRECT, no remap).
//     CMB eye material 3, mouth material 4.
//   En_Ma2/En_Ma3 OverrideLimbDraw @ 0x233648 / 0x23367c: limb 8 (HEAD), limb 7 (TORSO).
//     EnMa2_Draw @ 0x1da000: eye slot 0 = eyeIndex, mouth slot 1 = mouthIndex (DIRECT).
//     CMB eye material 4, mouth material 5.
//   Shared rotate helper: RotateX(rot.y)·RotateZ(rot.x), no Y negation, no pivot.
// State read through the C struct (64-bit build — never raw N64 offsets).
#include "z64.h"
#include "src/overlays/actors/ovl_En_Ma1/z_en_ma1.h"
#include "src/overlays/actors/ovl_En_Ma2/z_en_ma2.h"
// NOTE: z_en_ma3.h is intentionally NOT included — it redefines the same AdultMalonLimb enum as
// z_en_ma2.h (conflict in one TU). En_Ma3's struct is byte-identical to EnMa2, so En_Ma3 actors are
// read through EnMa2* below (the fields used — interactInfo/eyeIndex/mouthIndex — match exactly).
#include "malon.h"
#include "npc_draw.h"

namespace Zelda3D {

// --- Child Malon (En_Ma1) ------------------------------------------------------------------------
s16 ChildMalonBehavior::actorId() const {
    return ACTOR_EN_MA1;
}

void ChildMalonBehavior::applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {
    EnMa1* ma = reinterpret_cast<EnMa1*>(actor);
    if (track) {
        applyHeadTorsoTrack(modelId, 7, 6, ma->interactInfo);
    }
    if (facial) {
        applyFacialFrame(modelId, 3, ma->eyeIndex);
        applyFacialFrame(modelId, 4, ma->mouthIndex);
    }
}

// --- Adult Malon (En_Ma2 Lon Lon / En_Ma3 post-credits) ------------------------------------------
// EnMa2 and EnMa3 share an identical struct layout (interactInfo / eyeIndex / mouthIndex at the same
// fields); branch on the live actor id to read the correct type without relying on layout punning.
s16 AdultMalonBehavior::actorId() const {
    return ACTOR_EN_MA2;
}

void AdultMalonBehavior::applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {
    // Both ACTOR_EN_MA2 and ACTOR_EN_MA3 share the EnMa2 layout (see include note).
    EnMa2* ma = reinterpret_cast<EnMa2*>(actor);
    if (track) {
        applyHeadTorsoTrack(modelId, 8, 7, ma->interactInfo);
    }
    if (facial) {
        applyFacialFrame(modelId, 4, ma->eyeIndex);
        applyFacialFrame(modelId, 5, ma->mouthIndex);
    }
}

} // namespace Zelda3D
