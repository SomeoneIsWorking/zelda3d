#include "faceb.h"

#include <cstring>

namespace Zelda3D {

Faceb::Faceb(std::vector<uint8_t> data) {
    if (data.size() < 8 || std::memcmp(data.data(), "fkb", 3) != 0) {
        mErr = "not a faceb";
        return;
    }
    const uint8_t ver = data[3];
    if (ver != 1) {
        mErr = "unsupported faceb version " + std::to_string((int)ver);
        return;
    }
    const uint32_t n = (uint32_t)data[4] | ((uint32_t)data[5] << 8);
    if (8u + 4u * n > data.size()) {
        mErr = "faceb truncated";
        return;
    }
    mKeys.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* p = data.data() + 8 + 4 * i;
        Key k;
        k.frame = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        k.eye = p[2];
        k.mouth = p[3];
        mKeys.push_back(k);
    }
    mOk = true;
}

void Faceb::sample(float frame, int* outEye, int* outMouth) const {
    int eye = -1, mouth = -1;
    // Step (hold-last) sampling: the track carries integer texture indices, never interpolated.
    for (const Key& k : mKeys) {
        if ((float)k.frame > frame) break;
        if (k.eye != kHold) eye = k.eye;
        if (k.mouth != kHold) mouth = k.mouth;
    }
    if (outEye) *outEye = eye;
    if (outMouth) *outMouth = mouth;
}

} // namespace Zelda3D
