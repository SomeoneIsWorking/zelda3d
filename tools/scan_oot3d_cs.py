#!/usr/bin/env python3
# Scan an OoT3D cutscene blob (GREZZO_CS_V3 " BDQ" container) and
# emit an opcode histogram + a per-cmd hex dump.
#
# Motivation (debug_journal/2026-07-04-cs-interpreter-located.md):
# FUN_002c5ba0 is the interpreter with ~100 opcodes. The title cs
# only USES some of them; porting those first is Phase 3 priority.
#
# Usage: python3 tools/scan_oot3d_cs.py scratch/oot3d_title_cs/title_cs.bin

import collections
import os
import struct
import sys


def scan(path: str):
    with open(path, "rb") as f:
        blob = f.read()
    if len(blob) < 0x30:
        raise SystemExit(f"{path}: too short ({len(blob)}B)")

    hash16   = blob[0x00:0x10].hex()
    magic    = blob[0x10:0x14]            # b" BDQ" (0x51444220 LE)
    ver      = struct.unpack_from("<I", blob, 0x14)[0]
    hdr_size = struct.unpack_from("<I", blob, 0x18)[0]  # 8
    cs_len   = struct.unpack_from("<I", blob, 0x1C)[0]  # 0x12c0
    cmd_ct   = struct.unpack_from("<I", blob, 0x20)[0]  # 13
    unk_1    = struct.unpack_from("<I", blob, 0x24)[0]
    unk_2    = struct.unpack_from("<I", blob, 0x28)[0]
    end_fr   = struct.unpack_from("<I", blob, 0x2C)[0]  # 0x8ca

    print(f"=== {path} ({len(blob)} B) ===")
    print(f"hash16      = {hash16}")
    print(f"magic       = {magic!r} (expect b' BDQ')")
    print(f"version     = {ver}")
    print(f"hdr_size    = {hdr_size}")
    print(f"cs_len      = 0x{cs_len:x} ({cs_len})")
    print(f"cmd_count   = {cmd_ct}")
    print(f"unk_+0x24   = {unk_1}")
    print(f"unk_+0x28   = {unk_2}")
    print(f"end_frame   = {end_fr} (~ {end_fr/60:.1f} s at 60fps)")
    print()

    # Commands start at +0x30. Interpreter uses 4-byte BE-swapped reads,
    # so per FUN_00470758 pattern: opcode at [ptr+0], next fields at
    # [+4], [+8], [+0xc]. Terminator = opcode == -1 (0xffffffff).
    #
    # But the 16-byte stride is confirmed via FUN_00375750 access to
    # command_table[i * 0x10]. So try that first — walk 16-byte records
    # from +0x30, treating the first u32 (BE-swapped) as opcode.

    off = 0x30
    hist = collections.Counter()
    cmds = []
    while off + 4 <= len(blob):
        opcode = struct.unpack_from(">I", blob, off)[0]  # BE per FUN_00470758
        if opcode == 0xFFFFFFFF:
            print(f"[{len(cmds):3d}] +0x{off:04x}  TERMINATOR (0xffffffff)")
            break
        rec = blob[off:off + 16]
        cmds.append((off, opcode, rec))
        hist[opcode] += 1
        # Also try LE opcode for comparison
        opcode_le = struct.unpack_from("<I", blob, off)[0]
        print(f"[{len(cmds)-1:3d}] +0x{off:04x}  op_be=0x{opcode:08x} op_le=0x{opcode_le:08x}  {rec.hex()}")
        off += 16
        if len(cmds) > 500:
            print("... truncated at 500 cmds")
            break

    print()
    print("=== opcode histogram (BE interpretation) ===")
    for op, ct in sorted(hist.items()):
        marker = ""
        if 0x0b <= op <= 0x50:
            marker = " (N64-like range)"
        elif 0x51 <= op <= 0x8e:
            marker = " (OoT3D-added range)"
        elif op > 0x8e:
            marker = " (out of interpreter range!)"
        print(f"  op=0x{op:08x} × {ct}{marker}")

    # Also do LE view (in case the interpreter reads opcode LE despite
    # FUN_00470758 swap; hard to tell without a live probe).
    hist_le = collections.Counter()
    off = 0x30
    while off + 4 <= len(blob):
        op = struct.unpack_from("<I", blob, off)[0]
        if op == 0xFFFFFFFF:
            break
        hist_le[op] += 1
        off += 16
        if sum(hist_le.values()) > 500:
            break

    print()
    print("=== opcode histogram (LE interpretation) ===")
    for op, ct in sorted(hist_le.items()):
        marker = ""
        if 0x0b <= op <= 0x50:
            marker = " (N64-like range)"
        elif 0x51 <= op <= 0x8e:
            marker = " (OoT3D-added range)"
        elif op > 0x8e:
            marker = " (out of interpreter range!)"
        print(f"  op=0x{op:08x} × {ct}{marker}")


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
