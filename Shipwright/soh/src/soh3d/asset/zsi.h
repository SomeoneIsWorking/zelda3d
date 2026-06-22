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

namespace SoH3D {

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

  private:
    bool mOk = false;
    std::string mErr;
    std::vector<uint8_t> mData;
    std::string mName;
    bool mHasMesh = false;
    int mCmbOff = -1;
    uint32_t mCmbSize = 0;
};

} // namespace SoH3D
