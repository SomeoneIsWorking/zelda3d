// Zelda3D actor-behavior registry and dispatch. See actor_behavior.h.
#include "actor_behavior.h"
#include "actor_behavior_bridge.h"
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
#include "actor/boss_fd2.h"
#include "actor/boss_fd.h"
#include "actor/en_vb_ball.h"

namespace Zelda3D {

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
    static BossFd2Behavior sBossFd2;
    static BossFdBehavior sBossFd;
    static EnVbBallBehavior sEnVbBall;
    switch (actorId) {
        case ACTOR_BOSS_FD:
            return &sBossFd;
        case ACTOR_BOSS_FD2:
            return &sBossFd2;
        case ACTOR_EN_VB_BALL:
            return &sEnVbBall;
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

extern "C" int Zelda3D_TryActorDeferredDraw(PlayState* play, Actor* actor) {
    if (Zelda3D::ActorBehavior* b = Zelda3D::findActorBehavior(actor->id)) {
        return b->prepareDeferredDraw(play, actor) ? 1 : 0;
    }
    return 0;
}

extern "C" void Zelda3D_ActorBehaviorPreUpdate(PlayState* play, Actor* actor) {
    if (play == nullptr || actor == nullptr) {
        return;
    }
    if (Zelda3D::ActorBehavior* b = Zelda3D::findActorBehavior(actor->id)) {
        b->preUpdate(play, actor);
    }
}

extern "C" void Zelda3D_ActorBehaviorPostUpdate(PlayState* play, Actor* actor) {
    if (play == nullptr || actor == nullptr) {
        return;
    }
    if (Zelda3D::ActorBehavior* b = Zelda3D::findActorBehavior(actor->id)) {
        b->postUpdate(play, actor);
    }
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
