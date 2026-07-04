#!/usr/bin/env python3
# Dumps the OoT3D spot00 (SCENE_HYRULE_FIELD) scene-header ZSI + the
# cutscene script blob referenced by cmd 0x18 entry [0] (CS_CMD 0x17).
#
# This is Phase 1 of the title-scripted-path port
# (debug_journal/PLAN-title-scripted-port.md): locate + dump the CS
# bytes. Phase 2 will decode the OoT3D CS opcode format (starts with a
# 20-byte "OHHH…" signature we haven't decoded yet — likely a GREZZO
# hash/header prefix in front of the actual command stream).
#
# Reads $ZELDA3D_OOT3D_ROM or $SOH3D_3DS_ROM (decrypted OoT3D NCSD).
# Emits to scratch/oot3d_title_cs/ (gitignored).

import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from ctr_romfs import CtrRom  # noqa: E402


def _u32le(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _u32be(b, o):
    return struct.unpack_from(">I", b, o)[0]


def walk_zsi_cmds(data):
    if data[:3] != b"ZSI":
        raise SystemExit("not a ZSI")
    off = 16
    while off + 8 <= len(data):
        cmd1 = _u32be(data, off)
        ptr = _u32le(data, off + 4)
        ctype = (cmd1 >> 24) & 0xFF
        count = (cmd1 >> 16) & 0xFF
        yield off, ctype, count, ptr
        off += 8
        if ctype == 0x14:
            break


def main():
    rom_path = os.environ.get("ZELDA3D_OOT3D_ROM") or os.environ.get("SOH3D_3DS_ROM")
    if not rom_path:
        raise SystemExit("set ZELDA3D_OOT3D_ROM (see .env)")

    rom = CtrRom(rom_path)
    zsi = rom.read(rom.get("/scene/spot00_info.zsi"))
    print(f"spot00_info.zsi bytes={len(zsi)}")

    print("\n=== scene-header commands ===")
    cs_table_ptr = None
    cs_table_count = None
    for off, ctype, count, ptr in walk_zsi_cmds(zsi):
        print(f"  off=0x{off:04x} cmd=0x{ctype:02x} count={count:3d} ptr=0x{ptr:08x}")
        if ctype == 0x18:
            cs_table_ptr = ptr
            cs_table_count = count

    if cs_table_ptr is None:
        raise SystemExit("no cmd 0x18 (cutscene/alt-header table)")

    print(f"\n=== cmd 0x18 CS/AltHeader table @ 0x{cs_table_ptr:x} × {cs_table_count} ===")
    entries = []
    for i in range(cs_table_count):
        o = cs_table_ptr + i * 8
        a = _u32le(zsi, o)
        b = _u32le(zsi, o + 4)
        entries.append((a, b))
        print(f"  [{i:2d}] a=0x{a:08x} b=0x{b:08x}")

    # Entry [0] shape (0x17, ptr) matches N64 CS_CMD_CUTSCENE_DATA:
    # cmd type 0x17 with script pointer at second word.
    cs_type, cs_ptr = entries[0]
    print(f"\n=== entry[0]: cmd_type=0x{cs_type:02x} script_ptr=0x{cs_ptr:08x} ===")

    # Dump the CS blob until the next entry pointer (or file end).
    stop = len(zsi)
    for a, b in entries[1:]:
        for p in (a, b):
            if cs_ptr < p < stop:
                stop = p
    cs_blob = zsi[cs_ptr:stop]
    print(f"cs_blob length = {len(cs_blob)} bytes (0x{len(cs_blob):x})")

    out_dir = os.path.join(os.path.dirname(__file__), "..", "scratch", "oot3d_title_cs")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "spot00_info.zsi"), "wb") as f:
        f.write(zsi)
    with open(os.path.join(out_dir, "title_cs.bin"), "wb") as f:
        f.write(cs_blob)
    print(f"wrote {out_dir}/spot00_info.zsi")
    print(f"wrote {out_dir}/title_cs.bin ({len(cs_blob)} B)")

    print("\n=== title_cs.bin first 64B ===")
    for i in range(0, min(64, len(cs_blob)), 16):
        print(f"  +{i:04x}: {cs_blob[i:i+16].hex()}")


if __name__ == "__main__":
    main()
