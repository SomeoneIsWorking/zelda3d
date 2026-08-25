#include "oracle_title_state.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_state.h"
#include "repl_protocol.h"

namespace HarnessOracle {

bool ReadTitleCameraBasis(TitleCameraBasis* out) {
    auto& memory = Core::System::GetInstance().Memory();
    for (int axis = 0; axis < 3; ++axis) {
        const auto eye =
            memory.Read32OrNullopt(OracleLayout::kTitleCameraBasisAddress + static_cast<uint32_t>(axis * 4));
        const auto up =
            memory.Read32OrNullopt(OracleLayout::kTitleCameraBasisAddress + 0x18 + static_cast<uint32_t>(axis * 4));
        const auto direction =
            memory.Read32OrNullopt(OracleLayout::kTitleCameraBasisAddress + 0x24 + static_cast<uint32_t>(axis * 4));
        if (!eye || !up || !direction) {
            return false;
        }
        std::memcpy(&out->eye[axis], &*eye, sizeof(float));
        std::memcpy(&out->up[axis], &*up, sizeof(float));
        std::memcpy(&out->dir[axis], &*direction, sizeof(float));
    }
    return true;
}

bool ReadTitleLinkWorldPosition(TitleLinkWorldPosition* out) {
    auto& memory = Core::System::GetInstance().Memory();
    for (int axis = 0; axis < 3; ++axis) {
        const auto value =
            memory.Read32OrNullopt(OracleLayout::kTitleLinkWorldPositionAddress + static_cast<uint32_t>(axis * 4));
        if (!value) {
            return false;
        }
        std::memcpy(&out->pos[axis], &*value, sizeof(float));
    }
    return true;
}

void HandleTitleActors(std::istringstream& arguments) {
    std::string tableName;
    arguments >> tableName;
    if (tableName.empty()) {
        tableName = "a";
    }

    uint32_t baseAddress = 0;
    uint32_t poseCount = 0;
    const char* actorName = nullptr;
    if (tableName == "a") {
        baseAddress = OracleLayout::kTitlePoseTableAddress;
        poseCount = OracleLayout::kTitlePoseCount;
        actorName = "epona";
    } else if (tableName == "b") {
        baseAddress = OracleLayout::kTitlePoseTableBAddress;
        poseCount = OracleLayout::kTitlePoseBCount;
        actorName = "sibling";
    } else {
        HarnessRepl::PrintErr("titleactors: usage: titleactors [a|b]");
        return;
    }
    if (!TitleActive()) {
        HarnessRepl::PrintErr("titleactors: not at title (scene!=0x51 or active flag clear)");
        return;
    }

    auto& memory = Core::System::GetInstance().Memory();
    std::printf("ok titleactors %s %u\n", actorName, poseCount);
    for (uint32_t index = 0; index < poseCount; ++index) {
        const uint32_t address = baseAddress + index * OracleLayout::kTitlePoseStride;
        float position[3] = {};
        float rotation[3] = {};
        float scale[3] = {};
        bool mapped = true;
        for (int axis = 0; axis < 3; ++axis) {
            const auto positionBits = memory.Read32OrNullopt(address + static_cast<uint32_t>(axis * 4));
            const auto rotationBits = memory.Read32OrNullopt(address + 12 + static_cast<uint32_t>(axis * 4));
            const auto scaleBits = memory.Read32OrNullopt(address + 24 + static_cast<uint32_t>(axis * 4));
            if (!positionBits || !rotationBits || !scaleBits) {
                mapped = false;
                break;
            }
            std::memcpy(&position[axis], &*positionBits, sizeof(float));
            std::memcpy(&rotation[axis], &*rotationBits, sizeof(float));
            std::memcpy(&scale[axis], &*scaleBits, sizeof(float));
        }
        if (!mapped) {
            std::printf("  %2u @ 0x%08x  <unmapped>\n", index, address);
            continue;
        }
        std::printf("  %2u @ 0x%08x  pos=(%9.2f,%9.2f,%9.2f)  rot=(%6.3f,%6.3f,%6.3f)  "
                    "scale=(%.2f,%.2f,%.2f)\n",
                    index, address, position[0], position[1], position[2], rotation[0], rotation[1], rotation[2],
                    scale[0], scale[1], scale[2]);
    }
    std::printf("ok end\n");
}

} // namespace HarnessOracle
