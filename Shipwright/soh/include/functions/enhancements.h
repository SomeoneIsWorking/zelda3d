#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// #region SOH [General]
s32 Ship_CalcShouldDrawAndUpdate(PlayState* play, Actor* actor, Vec3f* projectedPos, f32 projectedW, bool* shouldDraw,
                                 bool* shouldUpdate);

// #region SOH [Rocs Feather]
void func_80838940(Player* thisx, LinkAnimationHeader* anim, f32 arg2, PlayState* play, u16 sfxId);

#ifdef __cplusplus
}
#endif
