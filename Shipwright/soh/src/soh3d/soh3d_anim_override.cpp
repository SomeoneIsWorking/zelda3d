// SoH3D skeletal-actor draw-override framework — see soh3d_anim_override.h.
//
// Head/torso tracking port. The OoT3D actor OverrideLimbDraw turns the head/torso toward the player
// by applying Matrix_Rotate* to the head/torso limb matrix (MTXMODE_APPLY) from interactInfo. We
// replicate that on the OoT3D rig: build the SAME rotation matrix and post-multiply it onto the OoT3D
// head/torso bone's local rotation (SoH3D_SetBonePostRot). Because the OoT3D head bone's origin is at
// the neck, a local rotation pivots there naturally — no explicit pivot translate needed. The
// interactInfo values come from the running (faithful) actor logic; only the application is ported.
#include "soh3d_anim_override.h"
#include "asset/mat4.h"

#include <cstring>
#include <cmath>
#include <cstdlib>

extern "C" {
const char* SoH3D_AutoModelZar(int modelId);
void SoH3D_SetBonePostRot(int modelId, int boneId, const float* mat9);
void SoH3D_ClearBonePostRots(int modelId);
}

int gSoH3dTrack = -1; // -1 = uninit (env SOH3D_TRACK, default on)

namespace {

using SoH3D::Mat4;

// One row per trackable actor, keyed by its OoT3D model ZAR.
//   interactOff = byte offset of NpcInteractInfo in the (faithful) actor struct (headRot @ +0x08,
//                 torsoRot @ +0x0E, both Vec3s).
//   headBone/torsoBone = OoT3D CMB bone ids for the head and upper torso (from the model skeleton).
//   torsoStyle = which Matrix_Rotate sequence the actor's OverrideLimbDraw uses for the torso
//                (head is the same for all: RotateX(headRot.y) then RotateZ(headRot.x)).
struct TrackActor {
    const char* zar;
    int interactOff; // NpcInteractInfo offset in the N64 actor struct (SoH3D runs N64 logic)
    int headBone;
    int torsoBone;
};

// En_Ko Kokiri kids: km1 (boy) / kw1 (girl), N64 interactInfo @ 0x1E8 (z_en_ko.h). En_Sa Saria:
// zelda_sa, N64 interactInfo @ 0x1E0 (z_en_sa.h). OoT3D head = bone 10, torso = bone 9 for both
// (decomp-confirmed: EnKo_OverrideLimbDraw 0x2335b4 + EnSa, shared helper FUN_0034e01c). NOTE: these
// are the **N64** struct offsets — SoH3D reads the N64 actor (it runs N64 logic); the OoT3D-native
// interactInfo offsets differ (En_Ko +0x28c, En_Sa +0x450) and must NOT be used here (validated live:
// head-track with 0x1E8 matched the reference). oot3d-decomp/docs/enko_override_and_ensa_facial.md.
constexpr TrackActor kTrackActors[] = {
    { "/actor/zelda_km1.zar", 0x1E8, 10, 9 },
    { "/actor/zelda_kw1.zar", 0x1E8, 10, 9 },
    { "/actor/zelda_sa.zar",  0x1E0, 10, 9 },
};

constexpr float kBinangToRad = 3.14159265358979f / 32768.0f;

// OoT3D head/torso track rotation — the shared no-negation helper (FUN_0034e01c) OoT3D applies to
// BOTH the head and torso limb matrix (MTXMODE_APPLY): RotateX(rot.y) then RotateZ(rot.x). (N64
// EnKo negates torso.y; OoT3D does not — a genuine Grezzo divergence. We match OoT3D.)
Mat4 trackRot(const short* rot) {
    return SoH3D::matMul(SoH3D::matRx(rot[1] * kBinangToRad), SoH3D::matRz(rot[0] * kBinangToRad));
}

// Push a 4x4's rotation 3x3 block (row-major) as a bone post-rotation.
void setBonePost(int modelId, int bone, const Mat4& m) {
    float m9[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] };
    SoH3D_SetBonePostRot(modelId, bone, m9);
}

const TrackActor* findRow(const char* zar) {
    if (!zar) return nullptr;
    for (const auto& r : kTrackActors)
        if (std::strcmp(r.zar, zar) == 0) return &r;
    return nullptr;
}

} // namespace

extern "C" void SoH3D_ApplyActorOverrides(int modelId, void* actorv) {
    if (gSoH3dTrack < 0) {
        const char* v = std::getenv("SOH3D_TRACK");
        gSoH3dTrack = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    SoH3D_ClearBonePostRots(modelId); // start from a clean slate each draw (no stale track pose)
    if (!gSoH3dTrack || actorv == nullptr) {
        return;
    }
    const TrackActor* row = findRow(SoH3D_AutoModelZar(modelId));
    if (row == nullptr) {
        return;
    }
    const unsigned char* a = static_cast<const unsigned char*>(actorv);
    const short* headRot = reinterpret_cast<const short*>(a + row->interactOff + 0x08);  // Vec3s
    const short* torsoRot = reinterpret_cast<const short*>(a + row->interactOff + 0x0E); // Vec3s

    // Head and torso both use the same OoT3D no-negation helper, post-multiplied onto their bones.
    setBonePost(modelId, row->headBone, trackRot(headRot));
    setBonePost(modelId, row->torsoBone, trackRot(torsoRot));
}
