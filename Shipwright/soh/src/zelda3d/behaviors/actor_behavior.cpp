// Zelda3D actor-behavior registry + shared port utilities. See actor_behavior.h.
#include "z64.h"
#include "actor_behavior.h"
#include "actor/kokiri_kid.h"
#include "actor/saria.h"
#include "actor/mido.h"
#include "actor/malon.h"
#include "actor/townsfolk.h"
#include "actor/door.h"
#include "actor/door_ana.h"
#include "actor/door_shutter.h"
#include "actor/obj_switch.h"
#include "actor/en_trap.h"
#include "actor/en_jsjutan.h"
#include "actor/en_butte.h"
#include "actor/en_elf.h"
#include "actor/en_fish.h"
#include "actor/en_mu.h"
#include "actor/en_dog.h"
#include "actor/pot.h"
#include "actor/comb.h"
#include "actor/hamishi.h"
#include "actor/bombwall.h"
#include "actor/ruppy.h"
#include "actor/en_item00.h"
#include "actor/kibako.h"
#include "actor/kibako2.h"
#include "actor/push_block.h"
#include "actor/en_tana.h"
#include "actor/boss_goma.h"
#include "asset/mat4.h"

extern "C" {
// CSAB skinner channel: post-multiply a 3x3 rotation onto a bone's local transform (MTXMODE_APPLY).
void Zelda3D_SetBonePostRot(int modelId, int boneId, const float* mat9);
// Facial material-anim channel: bind material `mat`'s frame texture (tex<0 clears -> base sprite).
void Zelda3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
int Zelda3D_FacialFrameTex(int modelId, int materialIndex, int frame); // -1 = none/out-of-range
// Model-draw bridges (shared rupee draw): resolve a CMB, mask to a mesh_id subset, draw at an actor.
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask);
}

// REPL `faceframe <n>` verification override (>= 0 forces the eye/mouth frame). Owned by
// zelda3d_anim_override.cpp; honored by every facial path so the channel can be driven deterministically.
extern int gZelda3dFaceForce;

namespace Zelda3D {

static constexpr float kBinangToRad = 3.14159265358979f / 32768.0f;

void applyTrackRot(int modelId, int bone, const Vec3s& rot) {
    // RotateX(rot.y) then RotateZ(rot.x) — the OoT3D shared helper's exact sequence (no Y negation,
    // no pivot). matMul(A, B) = A·B, applied to the bone as a post-multiply.
    Mat4 m = matMul(matRx(rot.y * kBinangToRad), matRz(rot.x * kBinangToRad));
    float m9[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] };
    Zelda3D_SetBonePostRot(modelId, bone, m9);
}

void applyHeadTorsoTrack(int modelId, int headBone, int torsoBone, const NpcInteractInfo& ii) {
    applyTrackRot(modelId, headBone, ii.headRot);
    applyTrackRot(modelId, torsoBone, ii.torsoRot);
}

void applyFacialFrame(int modelId, int material, int liveIdx) {
    if (material < 0) {
        return;
    }
    int idx = (gZelda3dFaceForce >= 0) ? gZelda3dFaceForce : liveIdx;
    int tex = Zelda3D_FacialFrameTex(modelId, material, idx);
    Zelda3D_GL_SetMatTexOverride(modelId, material, tex); // tex<0 (out-of-range) clears -> base sprite
}

// Shared colored-rupee draw: the OoT3D get-item rupee CMB (zelda_gi_rupy.cmb) bakes all five rupee
// colors as distinct mesh_ids 0..4 (green/blue/red/gold/purple, OoT3D GID order) stacked at one
// origin. We mask the per-frame mesh_id visibility to the single colorIdx so only that color draws.
// See oot3d-decomp/docs/en_ex_ruppy.md. Used by En_Ex_Ruppy and En_Item00 (both draw the N64 gRupeeDL
// with a per-color texture swap).
bool drawRupeeColorMesh(PlayState* play, Actor* actor, int colorIdx, float worldScale) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId("/actor/zelda_gi_rupy.zar|Model/zelda_gi_rupy.cmb");
    }
    if (sModelId < 0) {
        return false; // no OoT3D rupee CMB -> caller falls through to the N64 rupee
    }
    if (colorIdx < 0 || colorIdx > 4) {
        colorIdx = 0;
    }
    Zelda3D_GL_SetMidMask(sModelId, 1ull << colorIdx);
    Zelda3D_DrawActorModel(play, sModelId, actor, worldScale);
    return true;
}

// Explicit registry: one static singleton per ported behavior, dispatched by actor id. Add a case
// here as each actor is migrated out of the legacy zelda3d_anim_override.cpp tables.
ActorBehavior* findActorBehavior(s16 actorId) {
    static KokiriKidBehavior sKokiriKid;
    static SariaBehavior sSaria;
    static MidoBehavior sMido;
    static ChildMalonBehavior sChildMalon;
    static AdultMalonBehavior sAdultMalon;
    static TownsfolkBehavior sTownsfolk;
    static EnDoorBehavior sEnDoor;
    static DoorAnaBehavior sDoorAna;
    static DoorShutterBehavior sDoorShutter;
    static ObjSwitchBehavior sObjSwitch;
    static EnTrapBehavior sEnTrap;
    static EnJsjutanBehavior sEnJsjutan;
    static EnButteBehavior sEnButte;
    static EnElfBehavior sEnElf;
    static EnFishBehavior sEnFish;
    static EnMuBehavior sEnMu;
    static EnDogBehavior sEnDog;
    static EnTuboTrapBehavior sEnTuboTrap;
    static ObjCombBehavior sObjComb;
    static ObjHamishiBehavior sObjHamishi;
    static BgBombwallBehavior sBgBombwall;
    static EnExRuppyBehavior sEnExRuppy;
    static EnItem00Behavior sEnItem00;
    static ObjKibakoBehavior sObjKibako;
    static ObjKibako2Behavior sObjKibako2;
    static ObjOshihikiBehavior sObjOshihiki;
    static EnTanaBehavior sEnTana;
    static BossGomaBehavior sBossGoma;
    switch (actorId) {
        case ACTOR_BOSS_GOMA:
            return &sBossGoma;
        case ACTOR_EN_DOOR:
            return &sEnDoor;
        case ACTOR_DOOR_ANA:
            return &sDoorAna;
        case ACTOR_DOOR_SHUTTER:
            return &sDoorShutter;
        case ACTOR_OBJ_SWITCH:
            return &sObjSwitch;
        case ACTOR_EN_TRAP:
            return &sEnTrap;
        case ACTOR_EN_JSJUTAN:
            return &sEnJsjutan;
        case ACTOR_EN_BUTTE:
            return &sEnButte;
        case ACTOR_EN_ELF:
            return &sEnElf;
        case ACTOR_EN_FISH:
            return &sEnFish;
        case ACTOR_EN_TUBO_TRAP:
            return &sEnTuboTrap;
        case ACTOR_OBJ_COMB:
            return &sObjComb;
        case ACTOR_OBJ_HAMISHI:
            return &sObjHamishi;
        case ACTOR_BG_BOMBWALL:
            return &sBgBombwall;
        case ACTOR_EN_EX_RUPPY:
            return &sEnExRuppy;
        case ACTOR_EN_ITEM00:
            return &sEnItem00;
        case ACTOR_OBJ_KIBAKO:
            return &sObjKibako;
        case ACTOR_OBJ_KIBAKO2:
            return &sObjKibako2;
        case ACTOR_OBJ_OSHIHIKI:
            return &sObjOshihiki;
        case ACTOR_EN_TANA:
            return &sEnTana;
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
        case ACTOR_EN_MU:
            return &sEnMu;
        case ACTOR_EN_DOG:
            return &sEnDog;
        default:
            return nullptr;
    }
}

} // namespace Zelda3D

// C bridge for zelda3d.c (compiled as C): dispatch an actor to its model-REPLACEMENT behavior, if any.
// Returns 1 if the behavior fully drew the OoT3D replacement (N64 draw should be suppressed), else 0.
// Called once per actor from Zelda3D_TryDrawActor, before the auto/table forced-CMB path.
extern "C" int Zelda3D_TryActorModelDraw(PlayState* play, Actor* actor) {
    if (Zelda3D::ActorBehavior* b = Zelda3D::findActorBehavior(actor->id)) {
        if (b->tryDrawModel(play, actor)) {
            return 1;
        }
    }
    return 0;
}

// Coverage-audit bridge (REPL `actorsnear`): does this actor id have a ported behavior module?
// Used only at the audit's --N64-- fallthrough — i.e. after the object->ZAR table/auto checks have
// already failed — so a registered behavior there is necessarily a model-REPLACEMENT (door, fish,
// ...). Override-only behaviors (NPCs like En_Ko/En_Sa) enhance an auto/table model and so are
// classified AUTO/TABLE before this point, never reaching it.
extern "C" int Zelda3D_ActorHasBehaviorModule(s16 actorId) {
    return Zelda3D::findActorBehavior(actorId) != nullptr ? 1 : 0;
}

// C bridge: query an actor's faithful draw-space transform offset (see ActorBehavior::drawSpaceTransform).
// Returns 1 and fills *outLiftY (world-Y lift) + outLocalOff[3] (rotated, world-unit local translate) if
// the actor's behavior supplies one — the caller (Zelda3D_EmitModelDraw) then applies them and SKIPS the
// generic groundOffset. Returns 0 = no override (keep the generic anchor). Called once per replaced draw.
extern "C" int Zelda3D_ActorDrawSpaceTransform(void* actorv, float* outLiftY, float* outLocalOff) {
    Actor* actor = (Actor*)actorv;
    if (actor == nullptr) {
        return 0;
    }
    if (Zelda3D::ActorBehavior* b = Zelda3D::findActorBehavior(actor->id)) {
        float lift = 0.0f;
        Vec3f local = { 0.0f, 0.0f, 0.0f };
        if (b->drawSpaceTransform(actor, &lift, &local)) {
            *outLiftY = lift;
            outLocalOff[0] = local.x;
            outLocalOff[1] = local.y;
            outLocalOff[2] = local.z;
            return 1;
        }
    }
    return 0;
}
