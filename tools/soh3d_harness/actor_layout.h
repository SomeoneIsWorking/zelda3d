#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_ACTOR_LAYOUT_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_ACTOR_LAYOUT_H

#include <cstdint>

namespace ActorLayout {

inline constexpr uint32_t kContextOffset = 0x208C;
inline constexpr uint32_t kListsOffset = 0x000C;
inline constexpr uint32_t kCategoryCount = 12;
inline constexpr uint32_t kListStride = 8;
inline constexpr uint32_t kListCountOffset = 0;
inline constexpr uint32_t kListHeadOffset = 4;
inline constexpr uint32_t kMaxActorsPerCategory = 256;
inline constexpr uint32_t kListGuardSlack = 4;
inline constexpr uint32_t kIdOffset = 0x0000;

// OoT3D Actor.home remains at +0x08/+0x14. Generic live observables must
// instead read Actor.world at +0x28/+0x34, matching SoH's Actor.world fields.
inline constexpr uint32_t kWorldPosOffset = 0x0028;
inline constexpr uint32_t kWorldRotOffset = 0x0034;
inline constexpr uint32_t kShapeRotOffset = 0x00BC;
inline constexpr uint32_t kNextOffset = 0x0130;

constexpr uint32_t ListAddress(uint32_t playStateAddress, uint32_t category) {
    return playStateAddress + kContextOffset + kListsOffset + category * kListStride;
}

} // namespace ActorLayout

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_ACTOR_LAYOUT_H
