#!/usr/bin/env python3
"""Walk an OoT3D " BDQ" cutscene stream per FUN_002c5ba0's exact stride rules.

Format (RE'd 2026-07-07, verified on spot99 title cs — walks all 13 commands
to the -1 terminator exactly at file end):

  param_3 (interpreter arg) points AT the " BDQ" magic:
    +0x00 u32  magic " BDQ"
    +0x04 u16  version (3)
    +0x06 u16  0
    +0x08 s32  cmd_count      (outer loop runs exactly this many commands)
    +0x0C s32  end_frame      (stored to csCtx+0x18)
    +0x10      commands

  Command = u32 opcode + opcode-specific payload:
    opcode -1                        terminator
    1,2,5,6   (N64-style camera)     12B header (u16 start@+6, u16 end@+8)
                                     + 16B atoms until atom[0]==0xFF
    7,8       (camera rel)           28 bytes fixed
    9                                u32 cnt; odd: +12; then 24B per pair
    0x8c                             u32 cnt + cnt*12B records (time advance)
    0x96                             12B header (u16 cnt@+10) + cnt*32B
    0x97                             u32 byte_len + byte_len (spline block:
                                     camera + actor motion, Grezzo-new)
    1000                             16 bytes fixed
    everything else                  u32 cnt + cnt*48B records
                                     record: u16 sub-op, u16 start_frame@+2,
                                             u16 end_frame@+4, payload

Usage: walk_oot3d_cs.py <file> <hex-offset-of-BDQ>
(spot99_info.zsi title cs: offset 0x3528; container prefix is 16 bytes
before the " BDQ", after the cmd-0x17 alt-header entry points to it +0x10.)
"""
import struct, sys

CAM = {1, 2, 5, 6}

def walk(d, base, out=print):
    def u32(o): return struct.unpack_from('<I', d, o)[0]
    def s32(o): return struct.unpack_from('<i', d, o)[0]
    def u16(o): return struct.unpack_from('<H', d, o)[0]
    assert d[base:base+4] == b' BDQ', 'not a BDQ cs stream'
    cmd_count, end_frame = s32(base+8), s32(base+12)
    out(f"magic=' BDQ' ver={u16(base+4)} cmd_count={cmd_count} end_frame={end_frame}")
    cmds = []
    p = base + 0x10
    for _ in range(cmd_count):
        op = s32(p)
        if op == -1:
            out(f"+{p:#06x}: END(-1)")
            break
        start = p
        recs = []
        if op in CAM:
            s, e = u16(p+6), u16(p+8)
            q = p + 12
            while True:
                first = d[q]; q += 16
                if first == 0xFF: break
            desc = f"CAM frames {s}..{e} atoms={(q-p-12)//16}"
            p = q
        elif op in (7, 8):
            desc = f"CAM-REL frames {u16(p+6)}..{u16(p+8)}"; p += 28
        elif op == 9:
            cnt = s32(p+4); q = p+8; i = 0
            if cnt > 0 and cnt & 1: q = p+20; i = 1
            while i < cnt: q += 24; i += 2
            desc = f"OP9 cnt={cnt}"; p = q
        elif op == 0x8c:
            cnt = s32(p+4)
            recs = [(u16(p+8+i*12), u16(p+10+i*12), u16(p+12+i*12), p+8+i*12)
                    for i in range(max(cnt, 0))]
            desc = f"REC12 cnt={cnt}"; p += 8 + max(cnt, 0)*12
        elif op == 0x96:
            cnt = u16(p+10); desc = f"OP96 cnt={cnt}"; p += 12 + cnt*32
        elif op == 0x97:
            ln = u32(p+4); desc = f"OP97(spline) len={ln:#x}"; p += 8 + ln
        elif op == 1000:
            desc = "OP1000"; p += 16
        else:
            cnt = s32(p+4)
            recs = [(u16(p+8+i*48), u16(p+10+i*48), u16(p+12+i*48), p+8+i*48)
                    for i in range(max(cnt, 0))]
            desc = f"REC48 cnt={cnt}"
            p += 8 + max(cnt, 0)*48
        rtxt = ' '.join(f"[{a:#x} f{b}..{c}]" for a, b, c, _ in recs[:6])
        out(f"+{start:#06x}: op={op:#04x} ({op:4d}) size={p-start:5d}  {desc} {rtxt}")
        cmds.append((op, start, p, recs))
    out(f"stream ends at +{p:#x}; next u32 = {u32(p):#x}")
    return cmds

if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    walk(data, int(sys.argv[2], 16))
