#include "oracle_player_animation_commands.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_player_locator.h"
#include "oracle_state.h"
#include "repl_protocol.h"

namespace HarnessOraclePlayerAnimation {
namespace {

bool HandleAnimation() {
    if (!HarnessOracle::CurrentPlayState()) {
        HarnessRepl::PrintErr("az_linkanim: no playstate");
        return true;
    }
    const auto player = HarnessOraclePlayer::FindActor();
    if (!player) {
        HarnessRepl::PrintErr("az_linkanim: no Player actor");
        return true;
    }

    auto& memory = Core::System::GetInstance().Memory();
    const auto animationId = memory.Read32OrNullopt(*player + OracleLayout::kPlayerSkelAnimeOffset +
                                                    OracleLayout::kSkelAnimeAnimationIdOffset);
    const auto speedBits = memory.Read32OrNullopt(*player + OracleLayout::kActorSpeedXzOffset);
    if (!animationId || !speedBits) {
        HarnessRepl::PrintErr("az_linkanim: mem read fail");
        return true;
    }

    float speedXz = 0.0f;
    std::memcpy(&speedXz, &*speedBits, sizeof(speedXz));
    std::printf("ok az_linkanim animId=%u speedXZ=%.4f addr=0x%08x\n", *animationId, speedXz, *player);
    return true;
}

bool HandleJoints() {
    if (!HarnessOracle::CurrentPlayState()) {
        HarnessRepl::PrintErr("az_linkjoints: no playstate");
        return true;
    }
    const auto player = HarnessOraclePlayer::FindActor();
    if (!player) {
        HarnessRepl::PrintErr("az_linkjoints: no Player actor");
        return true;
    }

    auto& memory = Core::System::GetInstance().Memory();
    const auto jointTable = memory.Read32OrNullopt(*player + OracleLayout::kPlayerSkelAnimeOffset +
                                                   OracleLayout::kSkelAnimeJointTableOffset);
    if (!jointTable || *jointTable == 0) {
        HarnessRepl::PrintErr("az_linkjoints: jointTable ptr null/unreadable");
        return true;
    }

    float rotations[OracleLayout::kLinkJointBoneCount][9]{};
    for (int bone = 0; bone < OracleLayout::kLinkJointBoneCount; ++bone) {
        float matrix[12]{};
        for (int word = 0; word < 12; ++word) {
            const auto bits =
                memory.Read32OrNullopt(*jointTable + bone * OracleLayout::kLinkJointBoneStride + word * 4);
            if (!bits) {
                HarnessRepl::PrintErr("az_linkjoints: mem read fail mid-table");
                return true;
            }
            std::memcpy(&matrix[word], &*bits, sizeof(matrix[word]));
        }
        const float rotation[9] = { matrix[0], matrix[1], matrix[2], matrix[4], matrix[5],
                                    matrix[6], matrix[8], matrix[9], matrix[10] };
        std::memcpy(rotations[bone], rotation, sizeof(rotation));
    }

    std::printf("ok linkjoints %d addr=0x%08x\n", OracleLayout::kLinkJointBoneCount, *jointTable);
    for (int bone = 0; bone < OracleLayout::kLinkJointBoneCount; ++bone) {
        std::printf("  %d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", bone, rotations[bone][0], rotations[bone][1],
                    rotations[bone][2], rotations[bone][3], rotations[bone][4], rotations[bone][5], rotations[bone][6],
                    rotations[bone][7], rotations[bone][8]);
    }
    std::printf("ok end\n");
    return true;
}

} // namespace

bool HandleCommand(const std::string& command, std::istringstream&) {
    if (command == "az_linkanim") {
        return HandleAnimation();
    }
    if (command == "az_linkjoints") {
        return HandleJoints();
    }
    return false;
}

} // namespace HarnessOraclePlayerAnimation
