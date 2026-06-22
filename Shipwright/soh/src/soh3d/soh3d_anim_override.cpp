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
    int interactOff;
    int headBone;
    int torsoBone;
    int torsoStyle; // 0 = En_Ko (RotateX(-torsoRot.y), RotateZ(torsoRot.x))
                    // 1 = En_Sa (RotateY(torsoRot.y),  RotateX(torsoRot.x))
};

// En_Ko Kokiri kids: km1 (boy) / kw1 (girl). interactInfo @ 0x1E8 (z_en_ko.h). OoT3D km1 skeleton
// (bonestats): bone 10 = head (highest, 1124 face verts), bone 9 = upper torso (parent of head +
// both arms). En_Sa Saria: zelda_sa, interactInfo @ 0x1E0 (z_en_sa.h); OoT3D limbs head 10 / torso 9
// (saria_en_sa_compare.md), torso uses RotateY/RotateX. (sa bone ids assume the same layout as km1 —
// re-confirm against the sa skeleton when first exercised in the Meadow.)
constexpr TrackActor kTrackActors[] = {
    { "/actor/zelda_km1.zar", 0x1E8, 10, 9, 0 },
    { "/actor/zelda_kw1.zar", 0x1E8, 10, 9, 0 },
    { "/actor/zelda_sa.zar",  0x1E0, 10, 9, 1 },
};

constexpr float kBinangToRad = 3.14159265358979f / 32768.0f;

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

    // Head: RotateX(headRot.y) then RotateZ(headRot.x) — MTXMODE_APPLY => post = Rx(hy)·Rz(hx).
    Mat4 head = SoH3D::matMul(SoH3D::matRx(headRot[1] * kBinangToRad),
                              SoH3D::matRz(headRot[0] * kBinangToRad));
    setBonePost(modelId, row->headBone, head);

    // Torso: actor-specific sequence (same MTXMODE_APPLY post-multiply order).
    Mat4 torso;
    if (row->torsoStyle == 0) { // En_Ko
        torso = SoH3D::matMul(SoH3D::matRx(-torsoRot[1] * kBinangToRad),
                              SoH3D::matRz(torsoRot[0] * kBinangToRad));
    } else { // En_Sa
        torso = SoH3D::matMul(SoH3D::matRy(torsoRot[1] * kBinangToRad),
                              SoH3D::matRx(torsoRot[0] * kBinangToRad));
    }
    setBonePost(modelId, row->torsoBone, torso);
}
