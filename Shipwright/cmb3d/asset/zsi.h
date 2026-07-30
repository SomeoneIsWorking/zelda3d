// Parser for OoT3D ZSI scene/room info files (the per-room "Zelda Scene Info").
// Port of tools/zsi.py. Pure C++ (no SoH/LUS deps).
//
// A ROOM file (`<scene>_<R>_info.zsi`) wraps the room GEOMETRY as a single embedded
// CMB. A SCENE header file (`<scene>_info.zsi`) holds the room/actor/light lists and
// has NO embedded CMB. This class extracts the embedded room CMB so the existing Cmb
// loader can parse it unchanged.
//
// Format: magic "ZSI\x01" (4) + name[12], then 8-byte commands from 0x10 until a 0x14
// (End) command. cmd1 = u32 big-endian (byte0=type, byte1=count); cmd2 = u32 LE.
// Command 0x0A (Mesh) marks that the room has renderable geometry. The room CMB is the
// single `cmb ` blob (verified: every one of the game's 390 room files has exactly one;
// OoT3D rooms are one multi-material CMB, opaque/transparent split by material flags).
// See tools/zsi.py for the full rationale on why we locate the CMB by magic rather than
// by resolving the mesh-header pointer chain.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Zelda3D {

// One OoT3D scene environment-lighting setting (header command 0x0F). A scene holds a list of
// these (time-of-day variants); OoT3D selects/blends among them by dayTime. Used to drive Zelda3D's
// world lighting from OoT3D's OWN data for graphical parity (docs/oot3d_world_lighting_re.md).
// Non-Majora (OoT3D) per-setting stride 0x1C; colours are u8 [0,255], dirs are s8/0x7F in [-1,1].
// Raw ZSI header-command record (type, count, data offset). Retained for every command so callers can
// inspect lists this parser does not itself decode (actor/spawn/collision) — needed to relate MM3D
// scene-space coordinates to the N64 scene.
struct ZsiCommand {
    uint8_t type = 0;
    uint8_t count = 0;
    uint32_t offset = 0;
};


class Zsi {
  public:
    // Takes ownership of the file bytes.
    explicit Zsi(std::vector<uint8_t> data);
    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }

    const std::string& name() const { return mName; }
    bool hasMesh() const { return mHasMesh; }

    // True for a room file with a renderable embedded CMB (a 0x0A Mesh command plus a
    // sized `cmb ` blob). False for a header-only scene file.
    bool hasGeometry() const { return mHasMesh && mCmbOff >= 0 && mCmbSize > 0; }

    // The embedded room CMB bytes (a copy), or empty if !hasGeometry().
    std::vector<uint8_t> cmbBytes() const;

    // Raw slice bounds (for diagnostics / oracle cross-check).
    int cmbOffset() const { return mCmbOff; }
    uint32_t cmbSize() const { return mCmbSize; }

    // Every header command in file order, as raw (type, count, offset) records.
    const std::vector<ZsiCommand>& commands() const { return mCommands; }
    // Raw file bytes, so callers can decode a command's data array at its offset.
    const std::vector<uint8_t>& raw() const { return mData; }

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<uint8_t> mData;
    std::string mName;
    bool mHasMesh = false;
    int mCmbOff = -1;
    uint32_t mCmbSize = 0;
    std::vector<ZsiCommand> mCommands;
};

} // namespace Zelda3D
