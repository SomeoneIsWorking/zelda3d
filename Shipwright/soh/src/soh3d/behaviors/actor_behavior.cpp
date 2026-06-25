// SoH3D actor-behavior registry + shared port utilities. See actor_behavior.h.
#include "z64.h"
#include "actor_behavior.h"
#include "actor/kokiri_kid.h"
#include "actor/saria.h"
#include "actor/mido.h"
#include "actor/malon.h"
#include "actor/townsfolk.h"
#include "actor/door.h"
#include "../asset/mat4.h"

extern "C" {
// CSAB skinner channel: post-multiply a 3x3 rotation onto a bone's local transform (MTXMODE_APPLY).
void SoH3D_SetBonePostRot(int modelId, int boneId, const float* mat9);
// Facial material-anim channel: bind material `mat`'s frame texture (tex<0 clears -> base sprite).
void SoH3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
int SoH3D_FacialFrameTex(int modelId, int materialIndex, int frame); // -1 = none/out-of-range
}

// REPL `faceframe <n>` verification override (>= 0 forces the eye/mouth frame). Owned by
// soh3d_anim_override.cpp; honored by every facial path so the channel can be driven deterministically.
extern int gSoH3dFaceForce;

namespace SoH3D {

static constexpr float kBinangToRad = 3.14159265358979f / 32768.0f;

void applyTrackRot(int modelId, int bone, const Vec3s& rot) {
    // RotateX(rot.y) then RotateZ(rot.x) — the OoT3D shared helper's exact sequence (no Y negation,
    // no pivot). matMul(A, B) = A·B, applied to the bone as a post-multiply.
    Mat4 m = matMul(matRx(rot.y * kBinangToRad), matRz(rot.x * kBinangToRad));
    float m9[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] };
    SoH3D_SetBonePostRot(modelId, bone, m9);
}

void applyHeadTorsoTrack(int modelId, int headBone, int torsoBone, const NpcInteractInfo& ii) {
    applyTrackRot(modelId, headBone, ii.headRot);
    applyTrackRot(modelId, torsoBone, ii.torsoRot);
}

void applyFacialFrame(int modelId, int material, int liveIdx) {
    if (material < 0) {
        return;
    }
    int idx = (gSoH3dFaceForce >= 0) ? gSoH3dFaceForce : liveIdx;
    int tex = SoH3D_FacialFrameTex(modelId, material, idx);
    SoH3D_GL_SetMatTexOverride(modelId, material, tex); // tex<0 (out-of-range) clears -> base sprite
}

// Explicit registry: one static singleton per ported behavior, dispatched by actor id. Add a case
// here as each actor is migrated out of the legacy soh3d_anim_override.cpp tables.
ActorBehavior* findActorBehavior(s16 actorId) {
    static KokiriKidBehavior sKokiriKid;
    static SariaBehavior sSaria;
    static MidoBehavior sMido;
    static ChildMalonBehavior sChildMalon;
    static AdultMalonBehavior sAdultMalon;
    static TownsfolkBehavior sTownsfolk;
    static EnDoorBehavior sEnDoor;
    switch (actorId) {
        case ACTOR_EN_DOOR:
            return &sEnDoor;
        case ACTOR_EN_KO:
            return &sKokiriKid;
        case ACTOR_EN_SA:
            return &sSaria;
        case ACTOR_EN_MD:
            return &sMido;
        case ACTOR_EN_MA1:
            return &sChildMalon;
        case ACTOR_EN_MA2:
        case ACTOR_EN_MA3:
            return &sAdultMalon;
        case ACTOR_EN_HY:
            return &sTownsfolk;
        default:
            return nullptr;
    }
}

} // namespace SoH3D

// C bridge for soh3d.c (compiled as C): dispatch an actor to its model-REPLACEMENT behavior, if any.
// Returns 1 if the behavior fully drew the OoT3D replacement (N64 draw should be suppressed), else 0.
// Called once per actor from SoH3D_TryDrawActor, before the auto/table forced-CMB path.
extern "C" int SoH3D_TryActorModelDraw(PlayState* play, Actor* actor) {
    if (SoH3D::ActorBehavior* b = SoH3D::findActorBehavior(actor->id)) {
        if (b->tryDrawModel(play, actor)) {
            return 1;
        }
    }
    return 0;
}
