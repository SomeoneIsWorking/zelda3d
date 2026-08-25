// Deterministic entry into native Link action states for control and parity checks.
#ifndef ZELDA3D_CONTROL_PLAYER_STATE_H
#define ZELDA3D_CONTROL_PLAYER_STATE_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

s32 Zelda3D_PlayerForceClimb(Player* player, PlayState* play);
f32 Zelda3D_PlayerForceTeleport(Player* player, PlayState* play, f32 x, f32 z, s16 yaw, s32 setYaw);
s32 Zelda3D_PlayerForceRoll(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceTalk(Player* player, PlayState* play, f32 range);
s32 Zelda3D_PlayerForceIdle(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceJump(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceSwim(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceDamage(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceShield(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceAttack(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceHang(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceAttackCombo2(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceSwimDive(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceGetItem(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceDeath(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceCarry(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceThrow(Player* player, PlayState* play);
s32 Zelda3D_PlayerForcePutDown(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceItemUse(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceBackwalk(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceClimbMove(Player* player, PlayState* play, s32 direction);
s32 Zelda3D_PlayerIsZTargetIdleStance(Player* player);
s32 Zelda3D_PlayerZTargetStanceVariant(Player* player);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CONTROL_PLAYER_STATE_H
