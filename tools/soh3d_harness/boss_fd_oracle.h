#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_ORACLE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_ORACLE_H

#include <array>
#include <cstdint>

#include "../../Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/history_layout.h"

namespace Memory {
class MemorySystem;
}

namespace HarnessBossFdOracle {

inline constexpr uint16_t kActorId = 0x0096;
inline constexpr uint16_t kHoleActorId = 0x00A2;
inline constexpr uint32_t kCategory = 9;
inline constexpr uint32_t kParentPointerOffset = 0x0124;
inline constexpr uint32_t kRenderedHeadOffset = 0x0328;
inline constexpr uint32_t kHoleAnimationControllerOffset = 0x01A4;
inline constexpr uint32_t kHoleAnimationFrameOffset = kHoleAnimationControllerOffset + 0x003C;
inline constexpr uint32_t kHoleTimerOffset = 0x02BC;
inline constexpr uint32_t kHoleHeadRotOffset = 0x0334;
inline constexpr uint32_t kHoleJawOpeningOffset = 0x0814;
inline constexpr uint32_t kParentHandoffSignalOffset = 0x0940;
inline constexpr uint8_t kGroundHandoffSignal = 0x64;
inline constexpr uint32_t kActorSpeedOffset = 0x006C;
inline constexpr uint32_t kActorVelocityOffset = 0x0060;
inline constexpr uint32_t kActorDisplacementOffset = 0x00A4;
inline constexpr uint32_t kActionFunctionOffset = 0x0880;
inline constexpr uint32_t kFlightActionFunction = 0x003C724C;
inline constexpr uint32_t kActionOffset = 0x088A;
inline constexpr uint32_t kMoveTimerOffset = 0x088C;
inline constexpr uint32_t kHistoryLeadOffset = 0x0890;
inline constexpr uint32_t kStartAttackOffset = 0x089A;
inline constexpr uint32_t kStopFlagOffset = 0x08AC;
inline constexpr uint32_t kActionTimerOffset = 0x08B0;
inline constexpr uint32_t kControlOffset = 0x090C;
inline constexpr uint32_t kTargetOffset = 0x0924;
inline constexpr uint32_t kHistoryRotOffset = 0x0944;
inline constexpr uint32_t kHistoryPosOffset = 0x104C;
inline constexpr uint32_t kIntroStateOffset = 0x229E;

inline constexpr int kHistoryCount = Zelda3D::BossFdHistoryLayout::kBodyCount;

struct State {
    uint32_t address = 0;
    uint32_t actionFunction = 0;
    int scene = -1;
    int action = 0;
    int moveTimer = 0;
    int bodyLead = 0;
    int startAttack = 0;
    int stopFlag = 0;
    int actionTimer = 0;
    int introState = 0;
    std::array<float, 3> worldPos{};
    std::array<short, 3> worldRot{};
    std::array<float, 3> velocity{};
    float speed = 0.0f;
    std::array<float, 3> displacement{};
    std::array<float, 3> target{};
    std::array<float, 5> controls{};
    std::array<float, kHistoryCount * 3> historyPos{};
    std::array<float, kHistoryCount * 3> historyRot{};
};

struct ManeState {
    std::array<std::array<float, 3>, 3> head{};
    std::array<std::array<std::array<float, 3>, 10>, 3> pos{};
};

struct ManeRootDriverState {
    std::array<float, 3> worldPos{};
    std::array<short, 3> worldRot{};
    std::array<short, 3> shapeRot{};
    std::array<short, 3> headRot{};
    short timer = 0;
    float jawOpening = 0.0F;
    float animationFrame = 0.0F;
};

enum class LookupStatus {
    Found,
    Missing,
    Invalid,
};

struct Lookup {
    LookupStatus status;
    uint32_t address;
};

Lookup Find(Memory::MemorySystem& memory, uint32_t playState);
Lookup FindById(Memory::MemorySystem& memory, uint32_t playState, uint16_t actorId);
bool Read(Memory::MemorySystem& memory, uint32_t playState, uint32_t actor, State* out);
bool ReadHoleRenderedAnchor(Memory::MemorySystem& memory, uint32_t playState, std::array<float, 3>* outHead,
                            int16_t* outShapeYaw);
bool ReadHoleMane(Memory::MemorySystem& memory, uint32_t playState, ManeState* out);
bool ReadHoleManeRootDriver(Memory::MemorySystem& memory, uint32_t playState, ManeRootDriverState* outDriver);
bool ResetHoleMane(Memory::MemorySystem& memory, uint32_t playState, ManeRootDriverState* outDriver);
bool WriteHoleManeRootDriver(Memory::MemorySystem& memory, uint32_t playState, const ManeRootDriverState& driver);

} // namespace HarnessBossFdOracle

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD_ORACLE_H
