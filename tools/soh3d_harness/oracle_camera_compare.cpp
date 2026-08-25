#include "oracle_camera_compare.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "soh_camera_state.h"
#include "soh_play_state.h"

namespace HarnessOracle {

void CompareCameraImpl() {
    auto& memory = Core::System::GetInstance().Memory();
    float eye[3] = {};
    float direction[3] = {};
    float up[3] = {};
    bool mapped = true;
    for (int axis = 0; axis < 3; ++axis) {
        const auto eyeBits =
            memory.Read32OrNullopt(OracleLayout::kTitleCameraBasisAddress + static_cast<uint32_t>(axis * 4));
        const auto directionBits =
            memory.Read32OrNullopt(OracleLayout::kTitleCameraBasisAddress + 12 + static_cast<uint32_t>(axis * 4));
        const auto upBits =
            memory.Read32OrNullopt(OracleLayout::kTitleCameraBasisAddress + 24 + static_cast<uint32_t>(axis * 4));
        if (!eyeBits || !directionBits || !upBits) {
            mapped = false;
            break;
        }
        std::memcpy(&eye[axis], &*eyeBits, sizeof(float));
        std::memcpy(&direction[axis], &*directionBits, sizeof(float));
        std::memcpy(&up[axis], &*upBits, sizeof(float));
    }
    if (mapped) {
        std::printf("  3ds (title, 0x%08x): eye=(%.2f,%.2f,%.2f) dir=(%.3f,%.3f,%.3f) "
                    "up=(%.3f,%.3f,%.3f)\n",
                    OracleLayout::kTitleCameraBasisAddress, eye[0], eye[1], eye[2], direction[0], direction[1],
                    direction[2], up[0], up[1], up[2]);
    } else {
        std::printf("  3ds: n/a (0x%08x unmapped)\n", OracleLayout::kTitleCameraBasisAddress);
    }

    if (!SohState_HasPlayState()) {
        std::printf("  soh: n/a (no playstate)\n");
        return;
    }
    float eyeX = 0.0F;
    float eyeY = 0.0F;
    float eyeZ = 0.0F;
    float atX = 0.0F;
    float atY = 0.0F;
    float atZ = 0.0F;
    float upX = 0.0F;
    float upY = 0.0F;
    float upZ = 0.0F;
    float fieldOfView = 0.0F;
    short roll = 0;
    int activeCameraId = 0;
    if (!SohState_Camera(&eyeX, &eyeY, &eyeZ, &atX, &atY, &atZ, &upX, &upY, &upZ, &fieldOfView, &roll,
                         &activeCameraId)) {
        std::printf("  soh: n/a (no active camera)\n");
        return;
    }
    std::printf("  soh: camId=%d eye=(%.2f,%.2f,%.2f) at=(%.2f,%.2f,%.2f) up=(%.2f,%.2f,%.2f)\n"
                "       fov=%.2f roll=%d\n",
                activeCameraId, eyeX, eyeY, eyeZ, atX, atY, atZ, upX, upY, upZ, fieldOfView, roll);
}

} // namespace HarnessOracle
