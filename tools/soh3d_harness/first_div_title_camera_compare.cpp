#include "first_div_title_camera_compare.h"

#include "first_div_reporter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_title_state.h"
#include "soh_camera_state.h"

namespace HarnessOracle {

void CompareTitleCameraFirstDiv(bool oracleAtTitle, bool sohAtTitle, FirstDivReporter& reporter) {
    if (!oracleAtTitle) {
        std::printf("  title-cam: az=not-at-title\n");
        return;
    }

    TitleCameraBasis basis{};
    const bool oracleReadable = ReadTitleCameraBasis(&basis);
    if (!oracleReadable) {
        std::printf("  title-cam: az=(unmapped) soh=?\n");
        return;
    }
    if (!sohAtTitle) {
        std::printf("  title-cam: az_eye=(%.1f,%.1f,%.1f) soh=(no playstate)\n", basis.eye[0], basis.eye[1],
                    basis.eye[2]);
        return;
    }

    float eye[3] = {};
    float at[3] = {};
    float up[3] = {};
    float fieldOfView = 0.0F;
    short roll = 0;
    int cameraId = 0;
    if (!SohState_Camera(&eye[0], &eye[1], &eye[2], &at[0], &at[1], &at[2], &up[0], &up[1], &up[2], &fieldOfView, &roll,
                         &cameraId)) {
        std::printf("  title-cam: az_eye=(%.1f,%.1f,%.1f) soh=(no active camera)\n", basis.eye[0], basis.eye[1],
                    basis.eye[2]);
        return;
    }

    float sohDirection[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
    const float directionMagnitude = std::sqrt(sohDirection[0] * sohDirection[0] + sohDirection[1] * sohDirection[1] +
                                               sohDirection[2] * sohDirection[2]);
    if (directionMagnitude > 1.0e-4F) {
        sohDirection[0] /= directionMagnitude;
        sohDirection[1] /= directionMagnitude;
        sohDirection[2] /= directionMagnitude;
    }

    const float eyeDelta = std::sqrt((basis.eye[0] - eye[0]) * (basis.eye[0] - eye[0]) +
                                     (basis.eye[1] - eye[1]) * (basis.eye[1] - eye[1]) +
                                     (basis.eye[2] - eye[2]) * (basis.eye[2] - eye[2]));
    const float directionDelta = std::sqrt((basis.dir[0] - sohDirection[0]) * (basis.dir[0] - sohDirection[0]) +
                                           (basis.dir[1] - sohDirection[1]) * (basis.dir[1] - sohDirection[1]) +
                                           (basis.dir[2] - sohDirection[2]) * (basis.dir[2] - sohDirection[2]));
    const float upDelta =
        std::sqrt((basis.up[0] - up[0]) * (basis.up[0] - up[0]) + (basis.up[1] - up[1]) * (basis.up[1] - up[1]) +
                  (basis.up[2] - up[2]) * (basis.up[2] - up[2]));
    std::printf("  title-cam: az_eye=(%.1f,%.1f,%.1f) "
                "soh_eye=(%.1f,%.1f,%.1f) |Δeye|=%.2f "
                "|Δdir|=%.4f |Δup|=%.4f  (expect <5u post-port)\n",
                basis.eye[0], basis.eye[1], basis.eye[2], eye[0], eye[1], eye[2], eyeDelta, directionDelta, upDelta);
    std::printf("  title-cam:fov soh=%.2f°  activeCamId=%d\n", fieldOfView, cameraId);

    auto& memory = Core::System::GetInstance().Memory();
    std::printf("  title-cam:probe @ 0x%08x+0x24..0x84 (post-basis):", OracleLayout::kTitleCameraBasisAddress);
    for (int offset = 0x24; offset <= 0x84; offset += 4) {
        const auto word = memory.Read32OrNullopt(OracleLayout::kTitleCameraBasisAddress + offset);
        if (!word) {
            std::printf(" ??");
            continue;
        }
        float value = 0.0F;
        const std::uint32_t raw = *word;
        std::memcpy(&value, &raw, sizeof(float));
        std::printf(" +%02x=%.3f", offset, value);
    }
    std::printf("\n");

    const float rightX = basis.up[1] * basis.dir[2] - basis.up[2] * basis.dir[1];
    const float rightY = basis.up[2] * basis.dir[0] - basis.up[0] * basis.dir[2];
    const float rightZ = basis.up[0] * basis.dir[1] - basis.up[1] * basis.dir[0];
    std::printf("  title-cam:LH-right derived (up × dir) = (%.3f,%.3f,%.3f)\n", rightX, rightY, rightZ);
    const auto readFloat = [&](std::uint32_t address) -> std::optional<float> {
        const auto word = memory.Read32OrNullopt(address);
        if (!word) {
            return std::nullopt;
        }
        float value = 0.0F;
        const std::uint32_t raw = *word;
        std::memcpy(&value, &raw, sizeof(float));
        return value;
    };
    const auto storedRightX = readFloat(OracleLayout::kTitleCameraBasisAddress + 0x24);
    const auto storedRightY = readFloat(OracleLayout::kTitleCameraBasisAddress + 0x28);
    const auto storedRightZ = readFloat(OracleLayout::kTitleCameraBasisAddress + 0x2C);
    if (storedRightX && storedRightY && storedRightZ) {
        std::printf("  title-cam:stored right @ +0x140 = (%.3f,%.3f,%.3f)  "
                    "(match LH → OoT3D uses LH view basis)\n",
                    *storedRightX, *storedRightY, *storedRightZ);
    }

    if (!reporter.Reported() && eyeDelta > 200.0F) {
        char details[192];
        std::snprintf(details, sizeof(details), "|Δeye|=%.1f (az=%.0f,%.0f,%.0f soh=%.0f,%.0f,%.0f)", eyeDelta,
                      basis.eye[0], basis.eye[1], basis.eye[2], eye[0], eye[1], eye[2]);
        reporter.Report("title-cam", details);
    }
}

} // namespace HarnessOracle
