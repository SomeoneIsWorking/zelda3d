#include "oracle_player_state_commands.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "actor_layout.h"
#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_player_locator.h"
#include "oracle_state.h"
#include "repl_protocol.h"

namespace HarnessOraclePlayerState {
namespace {

bool HandlePlayerInfo() {
    if (!HarnessOracle::CurrentPlayState()) {
        HarnessRepl::PrintErr("az_playerinfo: no playstate");
        return true;
    }
    const auto player = HarnessOraclePlayer::FindActor();
    if (!player) {
        HarnessRepl::PrintErr("az_playerinfo: no Player actor");
        return true;
    }

    auto& memory = Core::System::GetInstance().Memory();
    const auto speedBits = memory.Read32OrNullopt(*player + OracleLayout::kActorSpeedXzOffset);
    const auto rotation = memory.Read32OrNullopt(*player + ActorLayout::kWorldRotOffset);
    const auto yawBits = memory.Read32OrNullopt(*player + (OracleLayout::kPlayerYawOffset & ~3u));
    if (!speedBits || !rotation || !yawBits) {
        HarnessRepl::PrintErr("az_playerinfo: mem read fail");
        return true;
    }

    float speedXz = 0.0f;
    std::memcpy(&speedXz, &*speedBits, sizeof(speedXz));
    const auto rotationX = static_cast<int16_t>(*rotation & 0xFFFF);
    const auto rotationY = static_cast<int16_t>((*rotation >> 16) & 0xFFFF);
    const auto playerYaw = static_cast<int16_t>((*yawBits >> ((OracleLayout::kPlayerYawOffset & 2) * 8)) & 0xFFFF);
    std::printf("ok az_playerinfo speedXZ=%.4f rot=(%d,%d) playerYaw=%d addr=0x%08x\n", speedXz,
                static_cast<int>(rotationX), static_cast<int>(rotationY), static_cast<int>(playerYaw), *player);
    return true;
}

bool HandlePlayerPosition() {
    if (!HarnessOracle::CurrentPlayState()) {
        HarnessRepl::PrintErr("az_playerpos: no playstate");
        return true;
    }
    const auto player = HarnessOraclePlayer::FindActor();
    if (!player) {
        HarnessRepl::PrintErr("az_playerpos: no Player actor");
        return true;
    }

    auto& memory = Core::System::GetInstance().Memory();
    const auto xBits = memory.Read32OrNullopt(*player + ActorLayout::kWorldPosOffset);
    const auto yBits = memory.Read32OrNullopt(*player + ActorLayout::kWorldPosOffset + 4);
    const auto zBits = memory.Read32OrNullopt(*player + ActorLayout::kWorldPosOffset + 8);
    const auto rotation = memory.Read32OrNullopt(*player + ActorLayout::kWorldRotOffset);
    const auto yawBits = memory.Read32OrNullopt(*player + (OracleLayout::kPlayerYawOffset & ~3u));
    if (!xBits || !yBits || !zBits || !rotation || !yawBits) {
        HarnessRepl::PrintErr("az_playerpos: mem read fail");
        return true;
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::memcpy(&x, &*xBits, sizeof(x));
    std::memcpy(&y, &*yBits, sizeof(y));
    std::memcpy(&z, &*zBits, sizeof(z));
    const auto worldRotationY = static_cast<int16_t>((*rotation >> 16) & 0xFFFF);
    const auto playerYaw = static_cast<int16_t>((*yawBits >> ((OracleLayout::kPlayerYawOffset & 2) * 8)) & 0xFFFF);
    std::printf("ok az_playerpos pos=(%.4f,%.4f,%.4f) worldRy=%d playerYaw=%d\n", x, y, z,
                static_cast<int>(worldRotationY), static_cast<int>(playerYaw));
    return true;
}

} // namespace

bool HandleCommand(const std::string& command, std::istringstream&) {
    if (command == "az_playerinfo") {
        return HandlePlayerInfo();
    }
    if (command == "az_playerpos") {
        return HandlePlayerPosition();
    }
    return false;
}

} // namespace HarnessOraclePlayerState
