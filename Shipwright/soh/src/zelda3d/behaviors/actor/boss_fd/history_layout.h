// OoT3D Boss_Fd procedural-history ring layout.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_HISTORY_LAYOUT_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_HISTORY_LAYOUT_H

#include <array>

namespace Zelda3D::BossFdHistoryLayout {

inline constexpr int kBodyCount = 150;
inline constexpr int kManeCount = 45;
inline constexpr int kBodySegmentCount = 18;
inline constexpr int kManeSegmentCount = 10;

// DAT_004D73AC: FUN_003B4308 indexes entries 1..18 for body bones; entry 2 anchors
// both arms and entry 0 anchors the head.
inline constexpr std::array<int, 20> kBodyOffset = {
    0, 141, 135, 126, 120, 111, 105, 96, 90, 81, 75, 66, 60, 51, 45, 36, 30, 21, 15, 6,
};

} // namespace Zelda3D::BossFdHistoryLayout

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD_HISTORY_LAYOUT_H
