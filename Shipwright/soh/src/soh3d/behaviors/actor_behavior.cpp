// SoH3D actor-behavior registry + shared port utilities. See actor_behavior.h.
#include "z64.h"
#include "actor_behavior.h"
#include "actor/kokiri_kid.h"
#include "../asset/mat4.h"

extern "C" {
// CSAB skinner channel: post-multiply a 3x3 rotation onto a bone's local transform (MTXMODE_APPLY).
void SoH3D_SetBonePostRot(int modelId, int boneId, const float* mat9);
}

namespace SoH3D {

static constexpr float kBinangToRad = 3.14159265358979f / 32768.0f;

void applyTrackRot(int modelId, int bone, const Vec3s& rot) {
    // RotateX(rot.y) then RotateZ(rot.x) — the OoT3D shared helper's exact sequence (no Y negation,
    // no pivot). matMul(A, B) = A·B, applied to the bone as a post-multiply.
    Mat4 m = matMul(matRx(rot.y * kBinangToRad), matRz(rot.x * kBinangToRad));
    float m9[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] };
    SoH3D_SetBonePostRot(modelId, bone, m9);
}

// Explicit registry: one static singleton per ported behavior, dispatched by actor id. Add a case
// here as each actor is migrated out of the legacy soh3d_anim_override.cpp tables.
ActorBehavior* findActorBehavior(s16 actorId) {
    static KokiriKidBehavior sKokiriKid;
    switch (actorId) {
        case ACTOR_EN_KO:
            return &sKokiriKid;
        default:
            return nullptr;
    }
}

} // namespace SoH3D
