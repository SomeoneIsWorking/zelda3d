#pragma once

#include "z64.h"
#include <soh/Enhancements/item-tables/ItemTableTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

EnItem00* Item_DropCollectible(PlayState* play, Vec3f* spawnPos, s16 params);
EnItem00* Item_DropCollectible2(PlayState* play, Vec3f* spawnPos, s16 params);
void EnItem00_CustomItemsParticles(Actor* Parent, PlayState* play, GetItemEntry giEntry);
void EnItem00_SetupAction(EnItem00* thisx, EnItem00ActionFunc actionFunc);
void func_8001E5C8(EnItem00* thisx, PlayState* play);
void Item_DropCollectibleRandom(PlayState* play, Actor* fromActor, Vec3f* spawnPos, s16 params);

void ActorShape_Init(ActorShape* shape, f32 yOffset, ActorShadowFunc shadowDraw, f32 shadowScale);
void ActorShadow_DrawCircle(Actor* actor, Lights* lights, PlayState* play);
void ActorShadow_DrawWhiteCircle(Actor* actor, Lights* lights, PlayState* play);
void ActorShadow_DrawHorse(Actor* actor, Lights* lights, PlayState* play);
void ActorShadow_DrawFeet(Actor* actor, Lights* lights, PlayState* play);
void Actor_SetFeetPos(Actor* actor, s32 limbIndex, s32 leftFootIndex, Vec3f* leftFootPos, s32 rightFootIndex,
                      Vec3f* rightFootPos);
void func_8002BE04(PlayState* play, Vec3f* arg1, Vec3f* arg2, f32* arg3);
void func_8002C124(TargetContext* targetCtx, PlayState* play);

void Actor_Kill(Actor* actor);
void Actor_SetFocus(Actor* actor, f32 offset);
void Actor_SetScale(Actor* actor, f32 scale);
void Actor_SetObjectDependency(PlayState* play, Actor* actor);
void Actor_UpdatePos(Actor* actor);
void Actor_UpdateVelocityXZGravity(Actor* actor);
void Actor_MoveXZGravity(Actor* actor);
void Actor_UpdateVelocityXYZ(Actor* actor);
void Actor_MoveXYZ(Actor* actor);
void Actor_SetProjectileSpeed(Actor* actor, f32 arg1);
s16 Actor_WorldYawTowardActor(Actor* actorA, Actor* actorB);
s16 Actor_WorldYawTowardPoint(Actor* actor, Vec3f* refPoint);
f32 Actor_WorldDistXYZToActor(Actor* actorA, Actor* actorB);
f32 Actor_WorldDistXYZToPoint(Actor* actor, Vec3f* refPoint);
s16 Actor_WorldPitchTowardActor(Actor* actorA, Actor* actorB);
s16 Actor_WorldPitchTowardPoint(Actor* actor, Vec3f* refPoint);
f32 Actor_WorldDistXZToActor(Actor* actorA, Actor* actorB);
f32 Actor_WorldDistXZToPoint(Actor* actor, Vec3f* refPoint);
void Actor_WorldToActorCoords(Actor* actor, Vec3f* result, Vec3f* arg2);
f32 Actor_HeightDiff(Actor* actorA, Actor* actorB);
f32 Player_GetHeight(Player* player);
s32 Player_ActionHandler_2(Player* player, PlayState* play);
f32 func_8002DCE4(Player* player);
s32 func_8002DD6C(Player* player);
s32 func_8002DD78(Player* player);
s32 func_8002DDE4(PlayState* play);
s32 func_8002DDF4(PlayState* play);
void func_8002DE04(PlayState* play, Actor* actorA, Actor* actorB);
void func_8002DE74(PlayState* play, Player* player);
void Actor_MountHorse(PlayState* play, Player* player, Actor* horse);
s32 func_8002DEEC(Player* player);
void func_8002DF18(PlayState* play, Player* player);
s32 func_8002DF38(PlayState* play, Actor* actor, u8 csMode);
s32 Player_SetCsActionWithHaltedActors(PlayState* play, Actor* actor, u8 arg2);
void func_8002DF90(DynaPolyActor* dynaActor);
void func_8002DFA4(DynaPolyActor* dynaActor, f32 arg1, s16 arg2);
s32 Player_IsFacingActor(Actor* actor, s16 angle, PlayState* play);
s32 Actor_ActorBIsFacingActorA(Actor* actorA, Actor* actorB, s16 angle);
s32 Actor_IsFacingPlayer(Actor* actor, s16 angle);
s32 Actor_ActorAIsFacingActorB(Actor* actorA, Actor* actorB, s16 angle);
s32 Actor_IsFacingAndNearPlayer(Actor* actor, f32 range, s16 angle);
s32 Actor_ActorAIsFacingAndNearActorB(Actor* actorA, Actor* actorB, f32 range, s16 angle);
void Actor_UpdateBgCheckInfo(PlayState* play, Actor* actor, f32 wallCheckHeight, f32 wallCheckRadius,
                             f32 ceilingCheckHeight, s32 flags);
Hilite* func_8002EABC(Vec3f* object, Vec3f* eye, Vec3f* lightDir, GraphicsContext* gfxCtx);
Hilite* func_8002EB44(Vec3f* object, Vec3f* eye, Vec3f* lightDir, GraphicsContext* gfxCtx);
void func_8002EBCC(Actor* actor, PlayState* play, s32 flag);
void func_8002ED80(Actor* actor, PlayState* play, s32 flag);
PosRot* Actor_GetFocus(PosRot* arg0, Actor* actor);
PosRot* Actor_GetWorld(PosRot* arg0, Actor* actor);
PosRot* Actor_GetWorldPosShapeRot(PosRot* arg0, Actor* actor);
s32 func_8002F0C8(Actor* actor, Player* player, s32 arg2);
u32 Actor_ProcessTalkRequest(Actor* actor, PlayState* play);
s32 func_8002F1C4(Actor* actor, PlayState* play, f32 arg2, f32 arg3, u32 arg4);
s32 func_8002F298(Actor* actor, PlayState* play, f32 arg2, u32 arg3);
s32 func_8002F2CC(Actor* actor, PlayState* play, f32 arg2);
s32 func_8002F2F4(Actor* actor, PlayState* play);
u32 Actor_TextboxIsClosing(Actor* actor, PlayState* play);
s8 func_8002F368(PlayState* play);
void Actor_GetScreenPos(PlayState* play, Actor* actor, s16* x, s16* y);
u32 Actor_HasParent(Actor* actor, PlayState* play);
// TODO: Rename the follwing 3 functions using whatever scheme we use when we rename Actor_OfferGetItem and
// Actor_OfferGetItemNearby.
s32 GiveItemEntryWithoutActor(PlayState* play, GetItemEntry getItemEntry);
s32 GiveItemEntryFromActor(Actor* actor, PlayState* play, GetItemEntry getItemEntry, f32 xzRange, f32 yRange);
s32 GiveItemEntryFromActorWithFixedRange(Actor* actor, PlayState* play, GetItemEntry getItemEntry);
s32 Actor_OfferGetItem(Actor* actor, PlayState* play, s32 getItemId, f32 xzRange, f32 yRange);
void Actor_OfferGetItemNearby(Actor* actor, PlayState* play, s32 getItemId);
void Actor_OfferCarry(Actor* actor, PlayState* play);
u32 Actor_HasNoParent(Actor* actor, PlayState* play);
void func_8002F5C4(Actor* actorA, Actor* actorB, PlayState* play);
void func_8002F5F0(Actor* actor, PlayState* play);
s32 Actor_IsMounted(PlayState* play, Actor* horse);
u32 Actor_SetRideActor(PlayState* play, Actor* horse, s32 arg2);
s32 Actor_NotMounted(PlayState* play, Actor* horse);
void func_8002F698(PlayState* play, Actor* actor, f32 arg2, s16 arg3, f32 arg4, u32 arg5, u32 arg6);
void func_8002F6D4(PlayState* play, Actor* actor, f32 arg2, s16 arg3, f32 arg4, u32 arg5);
void func_8002F71C(PlayState* play, Actor* actor, f32 arg2, s16 arg3, f32 arg4);
void func_8002F758(PlayState* play, Actor* actor, f32 arg2, s16 arg3, f32 arg4, u32 arg5);
void func_8002F7A0(PlayState* play, Actor* actor, f32 arg2, s16 arg3, f32 arg4);
void Player_PlaySfx(Actor* actor, u16 sfxId);
void Audio_PlayActorSound2(Actor* actor, u16 sfxId);
void func_8002F850(PlayState* play, Actor* actor);
void func_8002F8F0(Actor* actor, u16 sfxId);
void func_8002F91C(Actor* actor, u16 sfxId);
void func_8002F948(Actor* actor, u16 sfxId);
void func_8002F974(Actor* actor, u16 sfxId);
void func_8002F994(Actor* actor, s32 arg1);
s32 func_8002F9EC(PlayState* play, Actor* actor, CollisionPoly* poly, s32 bgId, Vec3f* pos);
void Actor_DisableLens(PlayState* play);
void func_800304DC(PlayState* play, ActorContext* actorCtx, ActorEntry* actorEntry);
void Actor_UpdateAll(PlayState* play, ActorContext* actorCtx);
s32 func_800314D4(PlayState* play, Actor* actorB, Vec3f* arg2, f32 arg3);
void func_800315AC(PlayState* play, ActorContext* actorCtx);
void func_80031A28(PlayState* play, ActorContext* actorCtx);
void func_80031B14(PlayState* play, ActorContext* actorCtx);
void func_80031C3C(ActorContext* actorCtx, PlayState* play);
Actor* Actor_Spawn(ActorContext* actorCtx, PlayState* play, s16 actorId, f32 posX, f32 posY, f32 posZ, s16 rotX,
                   s16 rotY, s16 rotZ, s16 params);
Actor* Actor_SpawnAsChild(ActorContext* actorCtx, Actor* parent, PlayState* play, s16 actorId, f32 posX, f32 posY,
                          f32 posZ, s16 rotX, s16 rotY, s16 rotZ, s16 params);
void Actor_SpawnTransitionActors(PlayState* play, ActorContext* actorCtx);
Actor* Actor_SpawnEntry(ActorContext* actorCtx, ActorEntry* actorEntry, PlayState* play);
Actor* Actor_Delete(ActorContext* actorCtx, Actor* actor, PlayState* play);
Actor* func_80032AF0(PlayState* play, ActorContext* actorCtx, Actor** actorPtr, Player* player);
Actor* Actor_Find(ActorContext* actorCtx, s32 actorId, s32 actorCategory);
void Enemy_StartFinishingBlow(PlayState* play, Actor* actor);
s16 func_80032CB4(s16* arg0, s16 arg1, s16 arg2, s16 arg3);
void BodyBreak_Alloc(BodyBreak* bodyBreak, s32 count, PlayState* play);
void BodyBreak_SetInfo(BodyBreak* bodyBreak, s32 limbIndex, s32 minLimbIndex, s32 maxLimbIndex, u32 count, Gfx** dList,
                       s16 objectId);
s32 BodyBreak_SpawnParts(Actor* actor, BodyBreak* bodyBreak, PlayState* play, s16 type);
void Actor_SpawnFloorDustRing(PlayState* play, Actor* actor, Vec3f* posXZ, f32 radius, s32 amountMinusOne,
                              f32 randAccelWeight, s16 scale, s16 scaleStep, u8 useLighting);
void func_80033480(PlayState* play, Vec3f* posBase, f32 randRangeDiameter, s32 amountMinusOne, s16 scaleBase,
                   s16 scaleStep, u8 arg6);
Actor* Actor_GetCollidedExplosive(PlayState* play, Collider* collider);
Actor* func_80033684(PlayState* play, Actor* explosiveActor);
Actor* Actor_GetProjectileActor(PlayState* play, Actor* refActor, f32 radius);
void Actor_ChangeCategory(PlayState* play, ActorContext* actorCtx, Actor* actor, u8 actorCategory);
void Actor_SetTextWithPrefix(PlayState* play, Actor* actor, s16 textIdLower);
s16 Actor_TestFloorInDirection(Actor* actor, PlayState* play, f32 distance, s16 angle);
s32 Actor_IsTargeted(PlayState* play, Actor* actor);
s32 Actor_OtherIsTargeted(PlayState* play, Actor* actor);
f32 func_80033AEC(Vec3f* arg0, Vec3f* arg1, f32 arg2, f32 arg3, f32 arg4, f32 arg5);
void func_80033C30(Vec3f* arg0, Vec3f* arg1, u8 alpha, PlayState* play);
void func_80033DB8(PlayState* play, s16 arg1, s16 arg2);
void func_80033E1C(PlayState* play, s16 arg1, s16 arg2, s16 arg3);
void func_80033E88(Actor* actor, PlayState* play, s16 arg2, s16 arg3);
f32 Rand_ZeroFloat(f32 f);
f32 Rand_CenteredFloat(f32 f);
void Actor_DrawDoorLock(PlayState* play, s32 arg1, s32 arg2);
void func_8003424C(PlayState* play, Vec3f* arg1);
void Actor_SetColorFilter(Actor* actor, s16 colorFlag, s16 colorIntensityMax, s16 xluFlag, s16 duration);
Hilite* func_800342EC(Vec3f* object, PlayState* play);
Hilite* func_8003435C(Vec3f* object, PlayState* play);
s32 Npc_UpdateTalking(PlayState* play, Actor* actor, s16* talkState, f32 interactRange, NpcGetTextIdFunc getTextId,
                      NpcUpdateTalkStateFunc updateTalkState);
s16 Npc_GetTrackingPresetMaxPlayerYaw(s16 presetIndex);
void Npc_TrackPoint(Actor* actor, NpcInteractInfo* interactInfo, s16 presetIndex, s16 trackingMode);
void func_80034BA0(PlayState* play, SkelAnime* skelAnime, OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw,
                   Actor* actor, s16 alpha);
void func_80034CC4(PlayState* play, SkelAnime* skelAnime, OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw,
                   Actor* actor, s16 alpha);
s16 Actor_UpdateAlphaByDistance(Actor* actor, PlayState* play, s16 arg2, f32 arg3);
void Animation_ChangeByInfo(SkelAnime* skelAnime, AnimationInfo* animationInfo, s32 index);
void func_80034F54(PlayState* play, s16* arg1, s16* arg2, s32 arg3);
void Actor_Noop(Actor* actor, PlayState* play);
void Gfx_DrawDListOpa(PlayState* play, Gfx* dlist);
void Gfx_DrawDListXlu(PlayState* play, Gfx* dlist);
Actor* Actor_FindNearby(PlayState* play, Actor* refActor, s16 actorId, u8 actorCategory, f32 range);
s32 func_800354B4(PlayState* play, Actor* actor, f32 range, s16 arg3, s16 arg4, s16 arg5);
void func_8003555C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void func_800355B8(PlayState* play, Vec3f* pos);
u8 func_800355E4(PlayState* play, Collider* collider);
u8 Actor_ApplyDamage(Actor* actor);
void Actor_SetDropFlag(Actor* actor, ColliderInfo* colBody, s32 freezeFlag);
void Actor_SetDropFlagJntSph(Actor* actor, ColliderJntSph* colBody, s32 freezeFlag);
void func_80035844(Vec3f* arg0, Vec3f* arg1, Vec3s* arg2, s32 arg3);
Actor* func_800358DC(Actor* actor, Vec3f* spawnPos, Vec3s* spawnRot, f32* arg3, s32 timer, s16* unused, PlayState* play,
                     s16 params, s32 arg8);
void func_800359B8(Actor* actor, s16 arg1, Vec3s* arg2);

u16 func_80037C30(PlayState* play, s16 arg1);
s32 func_80037D98(PlayState* play, Actor* actor, s16 arg2, s32* arg3);
s32 func_80038290(PlayState* play, Actor* actor, Vec3s* arg2, Vec3s* arg3, Vec3f arg4);

s32 Horse_CanSpawn(s32 scene);
void Horse_ResetHorseData(PlayState* play);
void Horse_FixLakeHyliaPosition(PlayState* play);
void Horse_SetupInGameplay(PlayState* play, Player* player);
void Horse_SetupInCutscene(PlayState* play, Player* player);
void Horse_InitPlayerHorse(PlayState* play, Player* player);
void Horse_RotateToPoint(Actor* actor, Vec3f* arg1, s16 arg2);

void Actor_ProcessInitChain(Actor* actor, InitChainEntry* initChain);

Path* Path_GetByIndex(PlayState* play, s16 index, s16 max);
f32 Path_OrientAndGetDistSq(Actor* actor, Path* path, s16 waypoint, s16* yaw);
void Path_CopyLastPoint(Path* path, Vec3f* dest);

void PlayerCall_InitFuncPtrs(void);

#ifdef __cplusplus
}
#endif
