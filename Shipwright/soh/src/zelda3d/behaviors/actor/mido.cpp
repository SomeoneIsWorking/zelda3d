// Zelda3D behavior: En_Md Mido — head/torso track + eye material-anim.
//
// OoT3D ground truth (oot3d-decomp/docs/enko_override_and_ensa_facial.md):
//   EnMd_OverrideLimbDraw @ 0x23bc70: limb 9 (HEAD) <- headRot, limb 8 (TORSO) <- torsoRot, shared
//   rotate helper. EnMd_Draw @ 0x1b72b4: eye material-anim slot 0 = eyeIdx (DIRECT). CMB eye material 1.
//   No mouth. (Optional blink-alpha overlay not ported — eyeIdx alone gives open/half/closed.)
// State read through the EnMd C struct (64-bit build — never raw N64 offsets).
#include "z64.h"
#include "src/overlays/actors/ovl_En_Md/z_en_md.h"
#include "mido.h"
#include "npc_draw.h"

namespace Zelda3D {

static constexpr int kHeadBone = 9;
static constexpr int kTorsoBone = 8;
static constexpr int kEyeMaterial = 1;

s16 MidoBehavior::actorId() const {
    return ACTOR_EN_MD;
}

void MidoBehavior::applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {
    EnMd* md = reinterpret_cast<EnMd*>(actor);
    if (track) {
        applyHeadTorsoTrack(modelId, kHeadBone, kTorsoBone, md->interactInfo);
    }
    if (facial) {
        applyFacialFrame(modelId, kEyeMaterial, md->eyeIdx);
    }
}

} // namespace Zelda3D
