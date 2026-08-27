#include "paired_camera_control.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_state.h"
#include "repl_protocol.h"
#include "soh_camera_state.h"
#include "soh_play_state.h"
#include "soh_runtime.h"

namespace HarnessPairedCameraControl {
namespace {

constexpr uint16_t kCameraActive = 7;
constexpr std::array<float, 3> kUp = { 0.0f, 1.0f, 0.0f };
constexpr uint32_t kWatchedVectorSize = OracleLayout::kCameraUpOffset + sizeof(kUp) - OracleLayout::kCameraAtOffset;

struct OracleOverride {
    uint32_t camera = 0;
    uint16_t previousStatus = 0;
    std::array<float, 3> eye{};
    std::array<float, 3> at{};
    float fov = 60.0f;
};

std::optional<OracleOverride> g_oracleOverride;
thread_local bool g_writingOverride = false;

std::optional<float> ParseFiniteFloat(const std::string& text) {
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

uint32_t FloatWord(float value) {
    uint32_t word = 0;
    static_assert(sizeof(word) == sizeof(value));
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

void WriteVector(Memory::MemorySystem& memory, uint32_t address, const std::array<float, 3>& value) {
    for (std::size_t axis = 0; axis < value.size(); ++axis) {
        memory.Write32(address + static_cast<uint32_t>(axis * sizeof(float)), FloatWord(value[axis]));
    }
}

void WriteForcedCamera(Memory::MemorySystem& memory, const OracleOverride& overrideState) {
    WriteVector(memory, overrideState.camera + OracleLayout::kCameraAtOffset, overrideState.at);
    WriteVector(memory, overrideState.camera + OracleLayout::kCameraEyeOffset, overrideState.eye);
    WriteVector(memory, overrideState.camera + OracleLayout::kCameraUpOffset, kUp);
    memory.Write32(overrideState.camera + OracleLayout::kCameraFovOffset, FloatWord(overrideState.fov));
}

void WriteForcedCameraGuarded(Memory::MemorySystem& memory, const OracleOverride& overrideState) {
    g_writingOverride = true;
    WriteForcedCamera(memory, overrideState);
    g_writingOverride = false;
}

std::optional<uint32_t> ActiveOracleCamera(Memory::MemorySystem& memory, uint32_t playState) {
    const uint16_t activeIndex = memory.Read16(playState + OracleLayout::kPlayActiveCameraOffset);
    if (activeIndex >= 4) {
        return std::nullopt;
    }
    const auto camera = memory.Read32OrNullopt(playState + OracleLayout::kPlayCameraPointersOffset + activeIndex * 4);
    if (!camera || *camera == 0) {
        return std::nullopt;
    }
    return *camera;
}

bool ApplyOracle(const std::array<float, 3>& eye, const std::array<float, 3>& at, float fov) {
    const auto playState = HarnessOracle::GameplayPlayState();
    if (!playState) {
        return false;
    }
    auto& memory = Core::System::GetInstance().Memory();
    const auto camera = ActiveOracleCamera(memory, *playState);
    if (!camera) {
        return false;
    }
    if (g_oracleOverride && g_oracleOverride->camera != *camera) {
        if (auto process = Core::System::GetInstance().Kernel().GetCurrentProcess()) {
            memory.UnregisterWatchpoint(*process, g_oracleOverride->camera + OracleLayout::kCameraAtOffset,
                                        kWatchedVectorSize);
        }
        memory.Write16(g_oracleOverride->camera + OracleLayout::kCameraStatusOffset, g_oracleOverride->previousStatus);
        g_oracleOverride.reset();
    }
    if (!g_oracleOverride) {
        const auto process = Core::System::GetInstance().Kernel().GetCurrentProcess();
        if (!process) {
            return false;
        }
        memory.RegisterWatchpoint(*process, *camera + OracleLayout::kCameraAtOffset, kWatchedVectorSize);
        g_oracleOverride =
            OracleOverride{ *camera, memory.Read16(*camera + OracleLayout::kCameraStatusOffset), eye, at, fov };
    } else {
        g_oracleOverride->eye = eye;
        g_oracleOverride->at = at;
        g_oracleOverride->fov = fov;
    }

    // Keep the normal active-camera pipeline alive so FUN_002d77dc consumes
    // these vectors and rebuilds the view matrix. The memory-write bridge
    // restores them after each mode-specific producer write.
    memory.Write16(*camera + OracleLayout::kCameraStatusOffset, kCameraActive);
    WriteForcedCameraGuarded(memory, *g_oracleOverride);
    WriteVector(memory, *playState + OracleLayout::kPlayCameraEyeOffset, eye);
    WriteVector(memory, *playState + OracleLayout::kPlayCameraAtOffset, at);
    WriteVector(memory, *playState + OracleLayout::kPlayCameraUpOffset, kUp);
    memory.Write32(*playState + OracleLayout::kPlayCameraFovOffset, FloatWord(fov));
    const uint8_t viewFlags = memory.Read8(*playState + OracleLayout::kPlayCameraDirtyOffset);
    memory.Write8(*playState + OracleLayout::kPlayCameraDirtyOffset, static_cast<uint8_t>(viewFlags | 1));
    return true;
}

bool DisableOracle() {
    if (!g_oracleOverride) {
        return true;
    }
    auto& memory = Core::System::GetInstance().Memory();
    if (auto process = Core::System::GetInstance().Kernel().GetCurrentProcess()) {
        memory.UnregisterWatchpoint(*process, g_oracleOverride->camera + OracleLayout::kCameraAtOffset,
                                    kWatchedVectorSize);
    }
    memory.Write16(g_oracleOverride->camera + OracleLayout::kCameraStatusOffset, g_oracleOverride->previousStatus);
    g_oracleOverride.reset();
    return true;
}

void SetCamera(std::istringstream& arguments) {
    std::array<float, 3> eye{};
    std::array<float, 3> at{};
    std::array<float*, 6> outputs = { &eye[0], &eye[1], &eye[2], &at[0], &at[1], &at[2] };
    for (float* output : outputs) {
        std::string text;
        if (!(arguments >> text)) {
            HarnessRepl::PrintErr("force camera: usage: force camera <eyeX> <eyeY> <eyeZ> <atX> <atY> <atZ> <fov>");
            return;
        }
        const auto value = ParseFiniteFloat(text);
        if (!value) {
            HarnessRepl::PrintErr("force camera: coordinates must be finite numbers");
            return;
        }
        *output = *value;
    }
    std::string fovText;
    if (!(arguments >> fovText)) {
        HarnessRepl::PrintErr("force camera: usage: force camera <eyeX> <eyeY> <eyeZ> <atX> <atY> <atZ> <fov>");
        return;
    }
    const auto fov = ParseFiniteFloat(fovText);
    if (!fov || *fov <= 0.0f || *fov >= 180.0f) {
        HarnessRepl::PrintErr("force camera: fov must be a finite number between 0 and 180 degrees");
        return;
    }
    std::string trailing;
    if (arguments >> trailing) {
        HarnessRepl::PrintErr("force camera: usage: force camera <eyeX> <eyeY> <eyeZ> <atX> <atY> <atZ> <fov>");
        return;
    }
    if (!Apply(eye, at, eye, at, *fov)) {
        HarnessRepl::PrintErr("force camera: could not apply the paired gameplay camera");
        return;
    }
    std::printf("ok force camera eye=(%.3f,%.3f,%.3f) at=(%.3f,%.3f,%.3f) fov=%.3f\n", eye[0], eye[1], eye[2], at[0],
                at[1], at[2], *fov);
}

void SetCameraPair(std::istringstream& arguments) {
    std::array<float, 3> oracleEye{};
    std::array<float, 3> oracleAt{};
    std::array<float, 3> sohEye{};
    std::array<float, 3> sohAt{};
    std::array<float*, 12> outputs = { &oracleEye[0], &oracleEye[1], &oracleEye[2], &oracleAt[0],
                                       &oracleAt[1],  &oracleAt[2],  &sohEye[0],    &sohEye[1],
                                       &sohEye[2],    &sohAt[0],     &sohAt[1],     &sohAt[2] };
    for (float* output : outputs) {
        std::string text;
        if (!(arguments >> text)) {
            HarnessRepl::PrintErr("force camera_pair: usage: force camera_pair <oracleEye xyz> <oracleAt xyz> <sohEye "
                                  "xyz> <sohAt xyz> <fov>");
            return;
        }
        const auto value = ParseFiniteFloat(text);
        if (!value) {
            HarnessRepl::PrintErr("force camera_pair: coordinates must be finite numbers");
            return;
        }
        *output = *value;
    }
    std::string fovText;
    const auto fov = (arguments >> fovText) ? ParseFiniteFloat(fovText) : std::optional<float>{};
    std::string trailing;
    if (!fov || *fov <= 0.0f || *fov >= 180.0f || (arguments >> trailing)) {
        HarnessRepl::PrintErr("force camera_pair: usage: force camera_pair <oracleEye xyz> <oracleAt xyz> <sohEye xyz> "
                              "<sohAt xyz> <fov>");
        return;
    }
    if (!Apply(oracleEye, oracleAt, sohEye, sohAt, *fov)) {
        HarnessRepl::PrintErr("force camera_pair: could not apply the paired gameplay cameras");
        return;
    }
    std::printf("ok force camera_pair oracleEye=(%.3f,%.3f,%.3f) oracleAt=(%.3f,%.3f,%.3f) "
                "sohEye=(%.3f,%.3f,%.3f) sohAt=(%.3f,%.3f,%.3f) fov=%.3f\n",
                oracleEye[0], oracleEye[1], oracleEye[2], oracleAt[0], oracleAt[1], oracleAt[2], sohEye[0], sohEye[1],
                sohEye[2], sohAt[0], sohAt[1], sohAt[2], *fov);
}

void DisableCamera() {
    const bool oracleOk = DisableOracle();
    const bool sohOk = !HarnessSohRuntime::IsBooted() || !SohState_HasPlayState() ||
                       SohState_SetCameraOverride(0, nullptr, nullptr, 0.0f);
    if (!oracleOk || !sohOk) {
        HarnessRepl::PrintErr("force camera_off: could not release the paired gameplay camera");
        return;
    }
    std::printf("ok force camera_off\n");
}

} // namespace

bool Apply(const std::array<float, 3>& oracleEye, const std::array<float, 3>& oracleAt,
           const std::array<float, 3>& sohEye, const std::array<float, 3>& sohAt, float fov) {
    if (!std::isfinite(fov) || fov <= 0.0f || fov >= 180.0f || !HarnessSohRuntime::IsBooted() ||
        !SohState_HasPlayState() || !HarnessOracle::GameplayPlayState() || !ApplyOracle(oracleEye, oracleAt, fov)) {
        return false;
    }
    if (!SohState_SetCameraOverride(1, sohEye.data(), sohAt.data(), fov)) {
        DisableOracle();
        return false;
    }
    return true;
}

void OverrideOracleWrite(uint32_t address, uint32_t size) {
    if (g_writingOverride || !g_oracleOverride) {
        return;
    }
    const bool writesVector = address < g_oracleOverride->camera + OracleLayout::kCameraAtOffset + kWatchedVectorSize &&
                              address + size > g_oracleOverride->camera + OracleLayout::kCameraAtOffset;
    const bool writesFov = address < g_oracleOverride->camera + OracleLayout::kCameraFovOffset + sizeof(float) &&
                           address + size > g_oracleOverride->camera + OracleLayout::kCameraFovOffset;
    if (!writesVector && !writesFov) {
        return;
    }
    WriteForcedCameraGuarded(Core::System::GetInstance().Memory(), *g_oracleOverride);
}

bool HandleForce(std::string_view subcommand, std::istringstream& arguments) {
    if (subcommand == "camera") {
        SetCamera(arguments);
        return true;
    }
    if (subcommand == "camera_pair") {
        SetCameraPair(arguments);
        return true;
    }
    if (subcommand == "camera_off") {
        DisableCamera();
        return true;
    }
    return false;
}

} // namespace HarnessPairedCameraControl
