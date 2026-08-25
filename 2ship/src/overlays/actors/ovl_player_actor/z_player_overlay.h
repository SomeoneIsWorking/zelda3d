#pragma once

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Player-overlay entry points consumed by host-side controls and enhancements.
void func_80836B3C(PlayState* play, Player* player, f32 initialFrame);
void Player_UseItem(PlayState* play, Player* player, ItemId item);
void Player_Action_86(Player* player, PlayState* play);
void Player_Action_96(Player* player, PlayState* play);

#ifdef __cplusplus
}
#endif
