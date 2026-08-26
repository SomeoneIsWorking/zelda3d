#pragma once

#include <cstdint>

namespace OracleLayout {

inline constexpr uint32_t kPlayStateAddress = 0x0050AF34;
inline constexpr uint32_t kSceneNumberOffset = 0x0104;
inline constexpr uint32_t kSaveContextAddress = 0x00587958;
inline constexpr uint32_t kDayTimeOffset = 0x0C;

inline constexpr uint32_t kTitleContextAddress = 0x0050AF34;
inline constexpr uint32_t kTitleSceneOffset = 0x006C;
inline constexpr uint32_t kTitleActiveOffset = 0x0078;
inline constexpr uint32_t kTitlePlayStatePointerAddress = 0x00539F98;
inline constexpr uint32_t kTitlePoseTableAddress = 0x005642D0;
inline constexpr uint32_t kTitlePoseCount = 25;
inline constexpr uint32_t kTitlePoseStride = 36;
inline constexpr uint32_t kTitlePoseTableBAddress = 0x005A54D8;
inline constexpr uint32_t kTitlePoseBCount = 25;
inline constexpr uint32_t kTitleCameraBasisAddress = 0x005BE6D4;
inline constexpr uint32_t kTitleLinkWorldPositionAddress = 0x005AFFB0;

inline constexpr uint32_t kActorSpeedXzOffset = 0x0068;
inline constexpr uint32_t kActorBgCheckFlagsOffset = 0x0090;
inline constexpr uint16_t kBgCheckWall = 0x0008;
inline constexpr uint16_t kBgCheckGround = 0x0001;

inline constexpr uint32_t kPlayerYawOffset = 0x0036;
inline constexpr uint32_t kPlayerSkelAnimeOffset = 0x0254;
inline constexpr uint32_t kSkelAnimeAnimationIdOffset = 0x0030;
inline constexpr uint32_t kSkelAnimeJointTableOffset = 0x0078;
inline constexpr uint32_t kLinkJointBoneStride = 13 * 4;
inline constexpr int kLinkJointBoneCount = 25;

inline constexpr uint32_t kTransitionTriggerOffset = 0x5C2D;
inline constexpr uint32_t kNextEntranceOffset = 0x5C32;
inline constexpr uint8_t kTransitionTriggerStart = 20;

inline constexpr uint32_t kPlayCameraEyeOffset = 0x01B8;
inline constexpr uint32_t kPlayCameraAtOffset = 0x01C4;
inline constexpr uint32_t kPlayCameraUpOffset = 0x01D0;
inline constexpr uint32_t kPlayCameraFovOffset = 0x0198;
inline constexpr uint32_t kPlayCameraDirtyOffset = 0x0360;
inline constexpr uint32_t kPlayCameraPointersOffset = 0x0A54;
inline constexpr uint32_t kPlayActiveCameraOffset = 0x0A64;
inline constexpr uint32_t kCameraAtOffset = 0x0080;
inline constexpr uint32_t kCameraEyeOffset = 0x008C;
inline constexpr uint32_t kCameraUpOffset = 0x0098;
inline constexpr uint32_t kCameraFovOffset = 0x0144;
inline constexpr uint32_t kCameraStatusOffset = 0x0188;
inline constexpr uint32_t kCameraModeOffset = 0x018C;

} // namespace OracleLayout
