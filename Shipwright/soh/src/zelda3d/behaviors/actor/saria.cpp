// Zelda3D behavior: En_Sa Saria — head/torso track + eye/mouth material-anim.
//
// OoT3D ground truth (oot3d-decomp/docs/enko_override_and_ensa_facial.md):
//   EnSa_OverrideLimbDraw @ 0x23bca4: limb 10 (HEAD) <- headRot, limb 9 (TORSO) <- torsoRot, via the
//   shared rotate helper (RotateX(rot.y)·RotateZ(rot.x), no Y negation, no pivot).
//   EnSa_Draw eye/mouth: material-anim slot 0 = eyeIndex (DIRECT), slot 1 = mouthRemap[mouthIndex].
//   CMB eye material 2, mouth material 3. Mouth remap = {0,3,4,1,2} (N64 order -> cmab frame order).
// (Ocarina-in-hand mesh swap in Sacred Forest Meadow is not yet ported — separate from track/facial.)
//
// All actor state is read through the EnSa C struct — never a raw N64 byte offset (64-bit build; see
// kokiri_kid.cpp for the full rationale behind the #116 fix).
#include "z64.h"
#include "src/overlays/actors/ovl_En_Sa/z_en_sa.h"
#include "npc_draw.h"
#include "saria.h"

namespace Zelda3D {

static constexpr int kHeadBone = 10;
static constexpr int kTorsoBone = 9;
static constexpr int kEyeMaterial = 2;
static constexpr int kMouthMaterial = 3;

// N64 mouthIndex (0..4) -> cmab material-anim frame order.
static constexpr int kMouthRemap[] = { 0, 3, 4, 1, 2 };

s16 SariaBehavior::actorId() const {
    return ACTOR_EN_SA;
}

void SariaBehavior::applyDrawOverrides(int modelId, Actor* actor, bool track, bool facial) {
    EnSa* sa = reinterpret_cast<EnSa*>(actor);
    if (track) {
        applyHeadTorsoTrack(modelId, kHeadBone, kTorsoBone, sa->interactInfo);
    }
    if (facial) {
        applyFacialFrame(modelId, kEyeMaterial, sa->rightEyeIndex);
        int m = sa->mouthIndex;
        int mouthFrame = (m >= 0 && m < (int)(sizeof(kMouthRemap) / sizeof(kMouthRemap[0]))) ? kMouthRemap[m] : 0;
        applyFacialFrame(modelId, kMouthMaterial, mouthFrame);
    }
}

} // namespace Zelda3D
