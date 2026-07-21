#include "zsi.h"
#include <cstring>

namespace Zelda3D {

static uint32_t u32le(const uint8_t* b, size_t o) {
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

static const uint8_t CMD_END = 0x14;
static const uint8_t CMD_MESH = 0x0A;
static const uint8_t CMD_ENV = 0x0F; // EnvironmentSettings (scene-header lighting variants)

Zsi::Zsi(std::vector<uint8_t> data) : mData(std::move(data)) {
    const uint8_t* b = mData.data();
    size_t n = mData.size();
    if (n < 16 || (memcmp(b, "ZSI\x01", 4) != 0 && memcmp(b, "ZSI\x09", 4) != 0)) {
        mErr = "bad ZSI magic";
        return;
    }
    // Name: up to 12 bytes, NUL-terminated.
    for (size_t i = 4; i < 16 && b[i]; i++) mName.push_back((char)b[i]);

    // Command list: 8-byte commands from 0x10 until a 0x14 (End) command. cmd1 is big-endian
    // (byte0 = type, byte1 = count); cmd2 (LE u32) is usually a file offset to the command's data.
    for (size_t off = 16; off + 8 <= n;) {
        uint8_t ctype = b[off];
        if (ctype == CMD_MESH) mHasMesh = true;
        else if (ctype == CMD_ENV) {
            // Environment-lighting variants (OoT3D's own scene lighting; cmd0x0F). count = byte1,
            // data array at the LE offset in cmd2. Per-setting stride 0x1C (non-Majora); layout
            // per noclip zsi.ts readEnvironmentSettings (see docs/oot3d_world_lighting_re.md).
            uint8_t count = b[off + 1];
            uint32_t eo = u32le(b, off + 4);
            for (uint8_t i = 0; i < count; i++) {
                size_t o = (size_t)eo + (size_t)i * 0x1C;
                if (o + 0x1C > n) break;
                ZsiEnvSetting e;
                for (int k = 0; k < 3; k++) {
                    e.ambient[k]  = b[o + 0x0A + k];
                    e.light0Dir[k] = (float)((int8_t)b[o + 0x0D + k]) / 127.0f;
                    e.light0Col[k] = b[o + 0x10 + k];
                    e.light1Dir[k] = (float)((int8_t)b[o + 0x13 + k]) / 127.0f;
                    e.light1Col[k] = b[o + 0x16 + k];
                    e.fogColor[k]  = b[o + 0x19 + k];
                }
                mEnvSettings.push_back(e);
            }
        }
        {
            // Keep every command's raw header. Cheap, and it is the only way to answer "what else is
            // in this file" (actor lists, spawns, collision) without reopening the parser each time.
            ZsiCommand c;
            c.type = ctype;
            c.count = b[off + 1];
            c.offset = u32le(b, off + 4);
            mCommands.push_back(c);
        }
        off += 8;
        if (ctype == CMD_END) break;
    }

    // Locate the single embedded room CMB by its `cmb ` magic (see header / zsi.py).
    for (size_t i = 16; i + 8 <= n; i++) {
        if (memcmp(b + i, "cmb ", 4) == 0) {
            mCmbOff = (int)i;
            mCmbSize = u32le(b, i + 4);
            break;
        }
    }
    mOk = true;
}

std::vector<uint8_t> Zsi::cmbBytes() const {
    if (!hasGeometry()) return {};
    size_t end = (size_t)mCmbOff + mCmbSize;
    if (end > mData.size()) end = mData.size();
    return std::vector<uint8_t>(mData.begin() + mCmbOff, mData.begin() + end);
}

} // namespace Zelda3D
