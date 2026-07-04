#!/usr/bin/env python3
# Structural scanner for an OoT3D cutscene blob (GREZZO_CS_V3 " BDQ").
#
# Walks the outer command loop per FUN_002c5ba0 (oot3d-decomp interpreter):
#   header  = [32B GREZZO prefix][cs_len u32][cmd_ct u32][unk u32][unk u32][end_frame u32]
#   cmd[i]  = [opcode u32][sub_count u32][sub_count * 48B sub-records]
#   terminator = opcode == 0xFFFFFFFF (also: outer loop stops at cmd_count)
#
# Verifies: after walking cmd_count cmds, the running byte offset should
# equal (blob_end or 32 + cs_len) — anything else = format mis-decode.
#
# Emits:
#   1. Header dump
#   2. Per-cmd (opcode, sub_count, span) with first sub-record byte peek
#   3. Verification: expected end offset vs actual
#   4. Opcode → cmd-count histogram

import collections
import os
import struct
import sys

# Ranges from FUN_002c5ba0 (see zelda3d_cutscene_oot3d_opcodes.h)
N64_MIRROR_LO, N64_MIRROR_HI = 0x0B, 0x50
OOT3D_ADDED_LO, OOT3D_ADDED_HI = 0x51, 0x8E


def scan(path):
    blob = open(path, "rb").read()
    if len(blob) < 0x30:
        raise SystemExit(f"{path}: too short")

    hash16   = blob[0x00:0x10].hex()
    magic    = blob[0x10:0x14]
    ver      = struct.unpack_from("<I", blob, 0x14)[0]
    hdr_size = struct.unpack_from("<I", blob, 0x18)[0]
    cs_len   = struct.unpack_from("<I", blob, 0x1C)[0]
    cmd_ct   = struct.unpack_from("<I", blob, 0x20)[0]
    unk_1    = struct.unpack_from("<I", blob, 0x24)[0]
    unk_2    = struct.unpack_from("<I", blob, 0x28)[0]
    end_fr   = struct.unpack_from("<I", blob, 0x2C)[0]

    print(f"=== {path} ({len(blob)} B) ===")
    print(f"hash16   = {hash16}")
    print(f"magic    = {magic!r}  (expect b' BDQ')")
    print(f"version  = {ver}")
    print(f"hdr_size = {hdr_size}")
    print(f"cs_len   = 0x{cs_len:x} ({cs_len})")
    print(f"cmd_ct   = {cmd_ct}")
    print(f"unk_+0x24 = {unk_1}")
    print(f"unk_+0x28 = {unk_2}")
    print(f"end_fr   = {end_fr}  (~{end_fr/60:.1f}s @60fps)")

    # Walk commands from +0x30. Per FUN_002c5ba0's outer loop:
    #   read op (4B), read sub_count (4B), skip sub_count * 48B, repeat.
    off  = 0x30
    hist = collections.Counter()
    i = 0
    print()
    print("=== command walk (48B sub-record stride) ===")
    while i < cmd_ct and off + 8 <= len(blob):
        opcode    = struct.unpack_from("<I", blob, off)[0]
        sub_count = struct.unpack_from("<I", blob, off + 4)[0]
        if opcode == 0xFFFFFFFF:
            print(f"[{i:2d}] +0x{off:04x}  TERMINATOR")
            off += 4
            break

        # Classify range
        if opcode == 0xFFFFFFFE:
            tag = "STOP_MARK"
        elif N64_MIRROR_LO <= opcode <= N64_MIRROR_HI:
            tag = "n64-mirror"
        elif OOT3D_ADDED_LO <= opcode <= OOT3D_ADDED_HI:
            tag = "oot3d-added"
        else:
            tag = "??"

        span = 8 + sub_count * 48
        # Peek first 16B of first sub-record
        peek = blob[off + 8:off + 24].hex() if sub_count > 0 else ""
        print(f"[{i:2d}] +0x{off:04x}  op=0x{opcode:02x} ({tag})  sub_ct={sub_count:3d}  span={span:4d}  sub0={peek}")
        hist[opcode] += 1

        off += span
        i += 1

    print()
    expected_end = 0x30 + cs_len
    if off == expected_end:
        print(f"OK: end offset 0x{off:x} matches 0x30 + cs_len = 0x{expected_end:x}")
    elif off == len(blob):
        print(f"OK: end offset 0x{off:x} matches blob end")
    else:
        print(f"MISMATCH: end offset 0x{off:x}  vs  0x30+cs_len 0x{expected_end:x}  vs  blob-end 0x{len(blob):x}")
    print(f"cmds walked: {i}  (expected {cmd_ct})")

    print()
    print("=== opcode histogram ===")
    for op, ct in sorted(hist.items()):
        if N64_MIRROR_LO <= op <= N64_MIRROR_HI:
            tag = "  (N64-mirror — port via existing SoH handler)"
        elif OOT3D_ADDED_LO <= op <= OOT3D_ADDED_HI:
            tag = "  (OoT3D-added — RE the case block)"
        else:
            tag = "  (outlier)"
        print(f"  op=0x{op:02x} × {ct}{tag}")


def main():
    if len(sys.argv) < 2:
        default = os.path.join(os.path.dirname(__file__), "..",
                               "scratch", "oot3d_title_cs", "title_cs.bin")
        if os.path.exists(default):
            scan(default)
            return
        raise SystemExit("usage: scan_oot3d_cs.py <path.bin>")
    for p in sys.argv[1:]:
        scan(p)


if __name__ == "__main__":
    main()
