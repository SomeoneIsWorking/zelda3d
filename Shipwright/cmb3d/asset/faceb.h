// Parser for OoT3D `.faceb` — Grezzo's per-animation FACIAL track for Link.
//
// GROUND TRUTH (RE'd 2026-07-23 from the retail romfs, docs/re-frontier.md `player.facial-anim`):
// every Link CSAB in /actor/zelda_link_child_new.zar and /actor/zelda_link_boy_new.zar has a
// SIBLING file with the same basename and the extension `.faceb` (582 csab / 582 faceb per zar,
// both under `boy/anim/`). It is a tiny step-keyframe track of the eye and mouth indices for that
// clip — the 3DS equivalent of N64 OoT encoding the face in the animation's fake limb 22
// (`z_player_lib.c` Player_DrawImpl: eyeIndex = (jointTable[22].x & 0xF) - 1,
//  mouthIndex = (jointTable[22].x >> 4) - 1).
//
// Layout (little-endian):
//   0x00  char[3] "fkb" + u8 version (1)
//   0x04  u16 keyCount, u16 pad
//   0x08  keyCount x { u16 frame; u8 eyeIndex; u8 mouthIndex }   (frames ascending)
// 0xFF in either index means HOLD — this clip does not drive that channel at that key, so the
// previously bound face stays. Clips with no facial content are a single key {0, 0xFF, 0xFF}.
//
// The indices select a frame of the eye/mouth CMAB (a TexturePalette material anim):
//   child  childlink_eye.cmab  -> material 14, 8 frames | childlink_mouth.cmab -> material 15, 4
//   adult  link_eye.cmab       -> material 16, 8 frames | link_mouth.cmab      -> material 17, 4
// 8 eye / 4 mouth is exactly N64's sEyeTextures[8] / sMouthTextures[4].
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Zelda3D {

class Faceb {
  public:
    static constexpr uint8_t kHold = 0xFF; // "this key does not drive this channel"

    struct Key {
        uint16_t frame = 0;
        uint8_t eye = kHold;
        uint8_t mouth = kHold;
    };

    explicit Faceb(std::vector<uint8_t> data);

    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }
    const std::vector<Key>& keys() const { return mKeys; }

    // Step-sample the track at `frame` (CSAB frame units). Writes the last index set at or before
    // `frame` for each channel, or -1 when the clip has not set that channel yet (caller holds).
    void sample(float frame, int* outEye, int* outMouth) const;

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<Key> mKeys;
};

} // namespace Zelda3D
