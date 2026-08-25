#include "oracle_player_compare.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "actor_layout.h"
#include "core/core.h"
#include "core/memory.h"
#include "oracle_state.h"
#include "soh_play_state.h"
#include "soh_player_state.h"

namespace HarnessOracle {

void ComparePlayerImpl() {
    const auto playState = CurrentPlayState();
    if (!playState) {
        std::printf("  3ds: n/a (no playstate)\n");
    } else {
        auto& memory = Core::System::GetInstance().Memory();
        bool found = false;
        for (uint32_t category = 0; category < ActorLayout::kCategoryCount && !found; ++category) {
            const auto head =
                memory.Read32OrNullopt(ActorLayout::ListAddress(*playState, category) + ActorLayout::kListHeadOffset);
            if (!head || *head == 0) {
                continue;
            }
            const auto id = memory.Read32OrNullopt(*head + ActorLayout::kIdOffset);
            if (!id || (*id & 0xFFFF) != 0) {
                continue;
            }
            const auto x = memory.Read32OrNullopt(*head + ActorLayout::kWorldPosOffset);
            const auto y = memory.Read32OrNullopt(*head + ActorLayout::kWorldPosOffset + 4);
            const auto z = memory.Read32OrNullopt(*head + ActorLayout::kWorldPosOffset + 8);
            const auto rotationXY = memory.Read32OrNullopt(*head + ActorLayout::kWorldRotOffset);
            const auto rotationZ = memory.Read32OrNullopt(*head + ActorLayout::kWorldRotOffset + 4);
            if (!x || !y || !z || !rotationXY || !rotationZ) {
                break;
            }
            float position[3] = {};
            std::memcpy(&position[0], &*x, sizeof(float));
            std::memcpy(&position[1], &*y, sizeof(float));
            std::memcpy(&position[2], &*z, sizeof(float));
            std::printf("  3ds: pos=(%.2f,%.2f,%.2f) rot=(%d,%d,%d)\n", position[0], position[1], position[2],
                        static_cast<int>(static_cast<int16_t>(*rotationXY & 0xFFFF)),
                        static_cast<int>(static_cast<int16_t>((*rotationXY >> 16) & 0xFFFF)),
                        static_cast<int>(static_cast<int16_t>(*rotationZ & 0xFFFF)));
            found = true;
        }
        if (!found) {
            std::printf("  3ds: n/a (no player actor live)\n");
        }
    }

    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    float position[3] = {};
    short rotation[3] = {};
    if (SohState_PlayerPos(&position[0], &position[1], &position[2], &rotation[0], &rotation[1], &rotation[2])) {
        std::printf("  soh: pos=(%.2f,%.2f,%.2f) rot=(%d,%d,%d)\n", position[0], position[1], position[2], rotation[0],
                    rotation[1], rotation[2]);
    } else {
        std::printf("  soh: n/a (no player actor live)\n");
    }
}

} // namespace HarnessOracle
