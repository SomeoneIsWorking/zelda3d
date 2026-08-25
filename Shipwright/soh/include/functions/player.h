#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

u8 PlayerGrounded(Player* player);
void Player_SetBootData(PlayState* play, Player* player);
void Player_StartAnimMovement(PlayState* play, Player* player, s32 flags);
s32 Player_InBlockingCsMode(PlayState* play, Player* player);
s32 Player_TryCsAction(PlayState* play, Actor* actor, s32 csAction);
s32 Player_InCsMode(PlayState* play);
s32 Player_CheckHostileLockOn(Player* player);
s32 Player_IsChildWithHylianShield(Player* player);
s32 Player_ActionToModelGroup(Player* player, s32 actionParam);
void Player_SetModelsForHoldingShield(Player* player);
void Player_SetModels(Player* player, s32 modelGroup);
void Player_SetModelGroup(Player* player, s32 modelGroup);
void func_8008EC70(Player* player);
void Player_SetEquipmentData(PlayState* play, Player* player);
void Player_UpdateBottleHeld(PlayState* play, Player* player, s32 item, s32 actionParam);
void func_80837C0C(PlayState* play, Player* thisx, s32 arg2, f32 arg3, f32 arg4, s16 arg5, s32 arg6);
void Player_ReleaseLockOn(Player* player);
void Player_ClearZTargeting(Player* player);
void Player_SetAutoLockOnActor(PlayState* play, Actor* actor);
s32 func_8008EF44(PlayState* play, s32 ammo);
s32 Player_IsBurningStickInRange(PlayState* play, Vec3f* pos, f32 radius, f32 arg3);
s32 Player_GetStrength(void);
u8 Player_GetMask(PlayState* play);
Player* Player_UnsetMask(PlayState* play);
s32 Player_HasMirrorShieldEquipped(PlayState* play);
s32 Player_HasMirrorShieldSetToDraw(PlayState* play);
s32 Player_ActionToMagicSpell(Player* player, s32 actionParam);
void Player_DrawHookshotReticle(PlayState* play, Player* player, f32 hookshotRange);
s32 Player_HoldsHookshot(Player* player);
s32 Player_HoldsBow(Player* player);
s32 Player_HoldsSlingshot(Player* player);
s32 func_8008F128(Player* player);
s32 Player_ActionToMeleeWeapon(s32 actionParam);
s32 Player_GetMeleeWeaponHeld(Player* player);
s32 Player_HoldsTwoHandedWeapon(Player* player);
s32 Player_HoldsBrokenKnife(Player* player);
s32 Player_ActionToBottle(Player* player, s32 actionParam);
s32 Player_GetBottleHeld(Player* player);
s32 Player_ActionToExplosive(Player* player, s32 actionParam);
s32 Player_GetExplosiveHeld(Player* player);
s32 func_8008F2BC(Player* player, s32 actionParam);
s32 Player_GetEnvironmentalHazard(PlayState* play);
void Player_DrawImpl(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount, s32 lod, s32 tunic, s32 boots,
                     s32 face, OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* thisx);
s32 Player_OverrideLimbDrawGameplayCommon(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                          void* data);
s32 Player_OverrideLimbDrawGameplayDefault(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                           void* data);
s32 Player_OverrideLimbDrawGameplayFirstPerson(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                               void* data);
s32 Player_OverrideLimbDrawGameplayCrawling(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                            void* data);
u8 func_80090480(PlayState* play, ColliderQuad* collider, WeaponInfo* weaponDim, Vec3f* newTip, Vec3f* newBase);
void Player_DrawGetItem(PlayState* play, Player* player);
void Player_PostLimbDrawGameplay(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* data);
u32 func_80091738(PlayState* play, u8* segment, SkelAnime* skelAnime);
void Player_DrawPause(PlayState* play, u8* segment, SkelAnime* skelAnime, Vec3f* pos, Vec3s* rot, f32 scale, s32 sword,
                      s32 tunic, s32 shield, s32 boots);

#ifdef __cplusplus
}
#endif
