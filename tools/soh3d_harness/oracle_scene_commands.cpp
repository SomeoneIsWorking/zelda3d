#include "oracle_scene_commands.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_state.h"
#include "repl_protocol.h"
#include "soh_player_state.h"
#include "soh_warp_state.h"

namespace HarnessOracle {

using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

void HandlePlayState(std::istringstream&) {
    const auto playState = CurrentPlayState();
    if (!playState) {
        PrintErr("playstate: not populated (still in menu/title?)");
        return;
    }
    std::printf("ok 0x%08x mode=%s\n", *playState, GameplayPlayState() ? "play" : "title");
}

void HandleGameplay(std::istringstream&) {
    std::printf("ok %s\n", GameplayPlayState() ? "yes" : "no");
}

void HandleScene(std::istringstream&) {
    const auto playState = CurrentPlayState();
    if (!playState) {
        PrintErr("scene: no playstate");
        return;
    }
    const auto scene =
        Core::System::GetInstance().Memory().Read32OrNullopt(*playState + OracleLayout::kSceneNumberOffset);
    if (!scene) {
        PrintErr("scene: sceneNum unmapped");
        return;
    }
    std::printf("ok 0x%04x\n", static_cast<unsigned>(*scene & 0xFFFF));
}

void HandleWarp(std::istringstream& arguments) {
    std::string entranceText;
    if (!(arguments >> entranceText)) {
        PrintErr("warp: usage: warp <entrance>");
        return;
    }
    const auto entrance = ParseNum(entranceText);
    if (!entrance) {
        PrintErr("warp: bad entrance");
        return;
    }

    // A transition written into the title demo's PlayState reports success but
    // cannot load a scene because no save is active. Only the gameplay pointer
    // is a valid transition owner.
    const auto playState = GameplayPlayState();
    if (!playState) {
        PrintErr("warp: not in gameplay (title/menu) — warp only works from a loaded "
                 "save. loadstate a gameplay state first, then warp.");
        return;
    }
    auto& memory = Core::System::GetInstance().Memory();
    memory.Write16(*playState + OracleLayout::kNextEntranceOffset, static_cast<uint16_t>(*entrance));
    memory.Write8(*playState + OracleLayout::kTransitionTriggerOffset, OracleLayout::kTransitionTriggerStart);
    std::printf("ok warp 0x%04x\n", static_cast<unsigned>(*entrance & 0xFFFF));
}

void HandleSohWarp(std::istringstream& arguments, bool sohBooted) {
    std::string entranceText;
    if (!(arguments >> entranceText)) {
        PrintErr("soh_warp: usage: soh_warp <entrance>");
        return;
    }
    const auto entrance = ParseNum(entranceText);
    if (!entrance) {
        PrintErr("soh_warp: bad entrance");
        return;
    }
    if (!sohBooted) {
        PrintErr("soh_warp: run soh_boot first");
        return;
    }
    if (!SohState_Warp(static_cast<unsigned short>(*entrance & 0xFFFF))) {
        PrintErr("soh_warp: no playstate — soh_step until Play is up first");
        return;
    }
    std::printf("ok soh_warp 0x%04x\n", static_cast<unsigned>(*entrance & 0xFFFF));
}

void HandleSohSetAge(std::istringstream& arguments, bool sohBooted) {
    std::string ageText;
    if (!(arguments >> ageText)) {
        PrintErr("soh_setage: usage: soh_setage <0|1>");
        return;
    }
    const auto age = ParseNum(ageText);
    if (!age || (*age != 0 && *age != 1)) {
        PrintErr("soh_setage: bad age (must be 0 or 1)");
        return;
    }
    if (!sohBooted) {
        PrintErr("soh_setage: run soh_boot first");
        return;
    }
    SohState_SetLinkAge(static_cast<int>(*age));
    const int currentAge = SohState_GetLinkAge();
    std::printf("ok soh_setage %ld (%s)  now=%d\n", static_cast<long>(*age), *age == 0 ? "adult" : "child", currentAge);
}

void HandleSohGetAge(std::istringstream&, bool sohBooted) {
    if (!sohBooted) {
        PrintErr("soh_getage: run soh_boot first");
        return;
    }
    const int age = SohState_GetLinkAge();
    std::printf("ok soh_getage %d (%s)\n", age, age == 0 ? "adult" : "child");
}

} // namespace HarnessOracle
