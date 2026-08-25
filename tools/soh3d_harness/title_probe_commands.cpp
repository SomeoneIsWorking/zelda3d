#include "title_probe_commands.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "repl_protocol.h"
#include "soh_runtime.h"
#include "soh_title_bridge.h"

namespace HarnessTitleProbe {
namespace {

bool HandleTitleCutscene(std::istringstream& arguments) {
    std::string frameText;
    if (arguments >> frameText) {
        const auto frame = HarnessRepl::ParseNum(frameText);
        if (!frame) {
            HarnessRepl::PrintErr("soh_titlecs: bad frame");
            return true;
        }
        Zelda3D_TitleCsSetFrame(static_cast<int>(*frame));
    }
    std::printf("ok soh_titlecs frame=%d end=%d\n", Zelda3D_TitleCsFrame(), Zelda3D_TitleCsEndFrame());
    return true;
}

bool HandleOracleCamera() {
    auto& memory = Core::System::GetInstance().Memory();
    constexpr uint32_t kCameraBasisAddress = OracleLayout::kTitleCameraBasisAddress;
    float eye[3] = {};
    float right[3] = {};
    float up[3] = {};
    float forward[3] = {};
    for (int axis = 0; axis < 3; ++axis) {
        const auto eyeBits = memory.Read32OrNullopt(kCameraBasisAddress + static_cast<uint32_t>(axis * 4));
        const auto rightBits = memory.Read32OrNullopt(kCameraBasisAddress + 0x0C + static_cast<uint32_t>(axis * 4));
        const auto upBits = memory.Read32OrNullopt(kCameraBasisAddress + 0x18 + static_cast<uint32_t>(axis * 4));
        const auto forwardBits = memory.Read32OrNullopt(kCameraBasisAddress + 0x24 + static_cast<uint32_t>(axis * 4));
        if (!eyeBits || !rightBits || !upBits || !forwardBits) {
            std::printf("ok az_camera unmapped\n");
            return true;
        }
        std::memcpy(&eye[axis], &*eyeBits, sizeof(float));
        std::memcpy(&right[axis], &*rightBits, sizeof(float));
        std::memcpy(&up[axis], &*upBits, sizeof(float));
        std::memcpy(&forward[axis], &*forwardBits, sizeof(float));
    }
    std::printf("ok az_camera eye=(%.1f,%.1f,%.1f) fwd=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f)"
                " up=(%.3f,%.3f,%.3f)\n",
                eye[0], eye[1], eye[2], forward[0], forward[1], forward[2], right[0], right[1], right[2], up[0], up[1],
                up[2]);
    return true;
}

bool HandleSohCamera() {
    if (!HarnessSohRuntime::IsBooted()) {
        HarnessRepl::PrintErr("soh_camera: run soh_boot first");
        return true;
    }
    float eye[3] = {};
    float at[3] = {};
    float up[3] = {};
    float fieldOfView = 0.0F;
    const int live = Zelda3D_Title_CameraState(eye, at, up, &fieldOfView);
    if (live == 0) {
        std::printf("ok soh_camera inactive (title not active)\n");
        return true;
    }
    const float dx = at[0] - eye[0];
    const float dy = at[1] - eye[1];
    const float dz = at[2] - eye[2];
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float divisor = length < 1e-6F ? 1.0F : length;
    std::printf("ok soh_camera live=%d eye=(%.1f,%.1f,%.1f) at=(%.1f,%.1f,%.1f)"
                " dir=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f) fov=%.2f\n",
                live == 1 ? 1 : 0, eye[0], eye[1], eye[2], at[0], at[1], at[2], dx / divisor, dy / divisor,
                dz / divisor, up[0], up[1], up[2], fieldOfView);
    return true;
}

bool HandleSohRider() {
    if (!HarnessSohRuntime::IsBooted()) {
        HarnessRepl::PrintErr("soh_rider: run soh_boot first");
        return true;
    }
    float position[3] = {};
    int computedYaw = 0;
    int worldYaw = 0x7FFFFFFF;
    int shapeYaw = 0x7FFFFFFF;
    const int mounted = Zelda3D_Title_RiderState(position, &computedYaw, &worldYaw, &shapeYaw);
    if (mounted == 0) {
        std::printf("ok soh_rider inactive (title not active)\n");
        return true;
    }
    const auto degrees = [](int angle) { return static_cast<double>(static_cast<int16_t>(angle)) * 360.0 / 65536.0; };
    char worldText[48] = {};
    char shapeText[48] = {};
    if (worldYaw == 0x7FFFFFFF) {
        std::snprintf(worldText, sizeof(worldText), "n/a");
    } else {
        std::snprintf(worldText, sizeof(worldText), "%d(%.1fdeg)", static_cast<int16_t>(worldYaw), degrees(worldYaw));
    }
    if (shapeYaw == 0x7FFFFFFF) {
        std::snprintf(shapeText, sizeof(shapeText), "n/a");
    } else {
        std::snprintf(shapeText, sizeof(shapeText), "%d(%.1fdeg)", static_cast<int16_t>(shapeYaw), degrees(shapeYaw));
    }
    std::printf("ok soh_rider mounted=%d pos=(%.1f,%.1f,%.1f) computedYaw=%d(%.1fdeg)"
                " horseWorldYaw=%s horseShapeYaw=%s\n",
                mounted == 1 ? 1 : 0, position[0], position[1], position[2], static_cast<int16_t>(computedYaw),
                degrees(computedYaw), worldText, shapeText);
    return true;
}

} // namespace

bool HandleCommand(const std::string& command, std::istringstream& arguments) {
    if (command == "soh_titlecs") {
        return HandleTitleCutscene(arguments);
    }
    if (command == "az_camera") {
        return HandleOracleCamera();
    }
    if (command == "soh_camera") {
        return HandleSohCamera();
    }
    if (command == "soh_rider") {
        return HandleSohRider();
    }
    return false;
}

} // namespace HarnessTitleProbe
