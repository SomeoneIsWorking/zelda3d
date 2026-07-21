// zelda3d_sword_trail — OoT3D's sword-trail geometry, ported.
//
// OoT3D (0x002b9a88) shortens the sword trail before handing it to the blure: the TIP passed to
// EffectBlure_AddVertex is pulled back toward the base by a per-weapon factor, so the visible streak
// covers only part of the blade. The COLLIDER keeps the untrimmed tip — this is a purely visual
// change, and 3DS applies it in exactly that asymmetric way.
//
// Lives in its own module rather than in z_player_lib.c, per the project rule that ported 3DS
// behavior goes into a dedicated, well-named module instead of being crammed into engine files.
#ifndef ZELDA3D_SWORD_TRAIL_H
#define ZELDA3D_SWORD_TRAIL_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Writes into `out` the trail tip OoT3D would use for `player`'s currently-held melee weapon.
// (Parameter is named `player`, not `this`, because this module is C++ where `this` is reserved.)
// Weapons with no 3DS trim factor (everything except the Kokiri and Master swords) copy `tip`
// through unchanged, which is what the 3DS table lookup does for them.
void Zelda3D_SwordTrail_TrimTip(Player* player, Vec3f* tip, Vec3f* base, Vec3f* out);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SWORD_TRAIL_H
