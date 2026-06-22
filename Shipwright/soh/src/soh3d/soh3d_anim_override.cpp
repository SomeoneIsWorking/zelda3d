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
// Facial material-anim channel (keystone #3): per-material eye/mouth frame texture swap.
void SoH3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
void SoH3D_GL_ClearMatTexOverrides(int modelId);
int SoH3D_FacialFrameTex(int modelId, int materialIndex, int frame); // -1 = none/out-of-range
}

int gSoH3dTrack = -1;  // -1 = uninit (env SOH3D_TRACK, default on)
int gSoH3dFacial = -1; // -1 = uninit (env SOH3D_FACIAL, default on)
// Debug/verification override (REPL `faceframe <n>`): when >= 0, force EVERY facial actor's eye AND
// mouth to this frame index, bypassing the live N64 index. Lets the channel be verified deterministi-
// cally (drive the frame, screenshot the face changing) despite the headless actor-update throttle
// stalling natural blink. -1 = off (use the live index).
int gSoH3dFaceForce = -1;

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
// All these actors apply head/torso through the SAME OoT3D shared helper (FUN_0034e01c) trackRot()
// uses — only {N64 interactInfo offset, head bone, torso bone} differ. interactOff is the N64 struct
// offset (SoH3D runs N64 logic); bones are OoT3D CMB bone ids. Decomp-derived:
// oot3d-decomp/docs/enko_override_and_ensa_facial.md (En_Ko/En_Sa live-verified; the rest pending).
// En_Hy townsfolk swap body skeletons by EnHyType (params&0x7f) — but each archetype is a distinct
// OoT3D body zar, so the ZAR-keyed table covers them with one row each (constant interactOff 0x1E8,
// per-archetype head/torso bones).
constexpr TrackActor kTrackActors[] = {
    { "/actor/zelda_km1.zar", 0x1E8, 10, 9 }, // En_Ko Kokiri boy
    { "/actor/zelda_kw1.zar", 0x1E8, 10, 9 }, // En_Ko Kokiri girl
    { "/actor/zelda_sa.zar",  0x1E0, 10, 9 }, // En_Sa Saria
    { "/actor/zelda_md.zar",  0x1E0,  9, 8 }, // En_Md Mido
    { "/actor/zelda_ma1.zar", 0x1E8,  7, 6 }, // En_Ma1 child Malon
    { "/actor/zelda_ma2.zar", 0x1E0,  8, 7 }, // En_Ma2/Ma3 adult Malon
    // En_Hy townsfolk, per body archetype (interactOff 0x1E8 for all):
    { "/actor/zelda_boj.zar", 0x1E8,  9, 8 },
    { "/actor/zelda_ahg.zar", 0x1E8, 15, 8 },
    { "/actor/zelda_bji.zar", 0x1E8, 10, 9 },
    { "/actor/zelda_cne.zar", 0x1E8, 14, 7 },
    { "/actor/zelda_aob.zar", 0x1E8, 10, 9 },
    { "/actor/zelda_cob.zar", 0x1E8, 12, 5 },
    { "/actor/zelda_bba.zar", 0x1E8,  7, 6 },
    { "/actor/zelda_bob.zar", 0x1E8, 14, 7 },
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

// --- Facial material-anim channel (keystone #3) --------------------------------------------------
// OoT3D animates an NPC's eye/mouth by swapping which texture a single eye/mouth MATERIAL samples
// (the alternate frame sprites are decoded from the sibling .cmab at model load; soh3d_model.cpp).
// The N64 actor logic that SoH3D runs already computes the live eye/mouth FRAME INDEX each frame; we
// read it from the N64 actor struct (the same source as the track interactInfo) and redirect the
// eye/mouth material to that frame's texture via SoH3D_GL_SetMatTexOverride. Faithful by
// construction: only the application (CMB material texture) is the OoT3D-native mechanism.
//
// All offsets are N64 actor-struct offsets (SoH3D reads the N64 struct). Material slots + frame
// order are CONSTANTS resolved per ZAR (docs/material_facial_channel_spec.md "Resolved unknowns").
struct FacialActor {
    const char* zar;
    int eyeIdxOff;    // N64 offset of the eye-index s16 (-1 = none)
    int mouthIdxOff;  // N64 offset of the mouth-index s16 (-1 = none)
    int eyeMat;       // CMB material slot of the eye (override key); -2 = per-ENKO_TYPE (kw1)
    int mouthMat;     // CMB material slot of the mouth (-1 = none)
    const signed char* mouthRemap; // N64 mouthIndex -> cmab frame; null = direct
    int mouthRemapLen;
};

// En_Sa mouth: N64 mouthTextures[] order differs from the cmab frame order — remap (decomp
// DAT_001b943c = {0,3,4,1,2}). Eye index is used directly (no remap).
constexpr signed char kEnSaMouthRemap[] = { 0, 3, 4, 1, 2 };

// Eye/mouth index offsets are N64 struct offsets: En_Sa rightEyeIndex@0x212, mouthIndex@0x216
// (z_en_sa.h); En_Ko eyeTextureIndex@0x216 (z_en_ko.h). km1/kw1 have no mouth. kw1 bakes two eye
// materials (Fado mat1 / girl mat2) into one shared body — eyeMat = -2 selects by ENKO_TYPE.
constexpr FacialActor kFacialActors[] = {
    { "/actor/zelda_sa.zar",  0x212, 0x216, 2, 3, kEnSaMouthRemap, (int)(sizeof(kEnSaMouthRemap)) },
    { "/actor/zelda_km1.zar", 0x216, -1, 1, -1, nullptr, 0 },
    { "/actor/zelda_kw1.zar", 0x216, -1, -2, -1, nullptr, 0 },
};

// ENKO_TYPE_CHILD_FADO = 12 (z_en_ko.h KokiriChildren enum). Actor.params is an s16 @ N64 offset
// 0x01C (z64actor.h). kw1's Fado variant uses eye material 1, every other (girl) type material 2.
constexpr int kEnKoTypeFado = 12;
constexpr int kActorParamsOff = 0x01C;

const FacialActor* findFacialRow(const char* zar) {
    if (!zar) return nullptr;
    for (const auto& r : kFacialActors)
        if (std::strcmp(r.zar, zar) == 0) return &r;
    return nullptr;
}

// Resolve the eye material slot for an actor (handles kw1's per-ENKO_TYPE split).
int eyeMaterial(const FacialActor* row, const unsigned char* a) {
    if (row->eyeMat != -2) return row->eyeMat;
    short params = *reinterpret_cast<const short*>(a + kActorParamsOff);
    return ((params & 0xFF) == kEnKoTypeFado) ? 1 : 2; // Fado eye mat1, girl eye mat2
}

// Read a live s16 facial index from the N64 actor and bind material `mat`'s frame texture.
void applyFacialSlot(int modelId, const unsigned char* a, int idxOff, int mat,
                     const signed char* remap, int remapLen) {
    if (idxOff < 0 || mat < 0) return;
    int idx = *reinterpret_cast<const short*>(a + idxOff);
    if (remap) idx = (idx >= 0 && idx < remapLen) ? remap[idx] : 0;
    if (gSoH3dFaceForce >= 0) idx = gSoH3dFaceForce; // verification override (REPL `faceframe`)
    int tex = SoH3D_FacialFrameTex(modelId, mat, idx);
    SoH3D_GL_SetMatTexOverride(modelId, mat, tex); // tex<0 (out-of-range) clears -> base sprite
}

} // namespace

extern "C" void SoH3D_ApplyActorOverrides(int modelId, void* actorv) {
    if (gSoH3dTrack < 0) {
        const char* v = std::getenv("SOH3D_TRACK");
        gSoH3dTrack = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    if (gSoH3dFacial < 0) {
        const char* v = std::getenv("SOH3D_FACIAL");
        gSoH3dFacial = (v != nullptr && v[0] == '0') ? 0 : 1;
    }
    // Start from a clean slate each draw (no stale track pose / facial frame).
    SoH3D_ClearBonePostRots(modelId);
    SoH3D_GL_ClearMatTexOverrides(modelId);
    if (actorv == nullptr) {
        return;
    }
    const char* zar = SoH3D_AutoModelZar(modelId);
    const unsigned char* a = static_cast<const unsigned char*>(actorv);

    // Head/torso tracking (#93).
    const TrackActor* row = gSoH3dTrack ? findRow(zar) : nullptr;
    if (row != nullptr) {
        const short* headRot = reinterpret_cast<const short*>(a + row->interactOff + 0x08);  // Vec3s
        const short* torsoRot = reinterpret_cast<const short*>(a + row->interactOff + 0x0E); // Vec3s
        // Head and torso both use the same OoT3D no-negation helper, post-multiplied onto their bones.
        setBonePost(modelId, row->headBone, trackRot(headRot));
        setBonePost(modelId, row->torsoBone, trackRot(torsoRot));
    }

    // Facial eye/mouth material-anim (keystone #3).
    const FacialActor* fr = gSoH3dFacial ? findFacialRow(zar) : nullptr;
    if (fr != nullptr) {
        applyFacialSlot(modelId, a, fr->eyeIdxOff, eyeMaterial(fr, a), nullptr, 0);
        applyFacialSlot(modelId, a, fr->mouthIdxOff, fr->mouthMat, fr->mouthRemap, fr->mouthRemapLen);
    }
}
