#!/usr/bin/env python3
"""Parser for OoT3D scene collision (the N64 cmd-0x03 analogue) inside `<scene>_info.zsi`.

Reverse-engineered from the USA decrypted ROM (session 15) — noclip has the
`Collision = 0x03` enum but never parses it, so there was no reference. Verified by
(a) the plane identity n.vA == -dist holding for 100% of polys and (b) the per-poly
stored normal matching the geometric face normal for 99.9% of polys, across multiple
scenes (see __main__ self-test).

Scene-header command list: 8-byte commands from 0x10; cmd byte0 = type. Command
addresses are PLAIN file offsets (proven via the Rooms cmd resolving to room-name
strings). cmd 0x03's cmd2 points at the CollisionHeader.

CollisionHeader (at the cmd-0x03 file offset):
    +0x1c u16 nVtx
    +0x1e u16 nPoly
    +0x28 u32 vtxList   (raw pointer)
    +0x2c u32 polyList  (raw pointer)
    +0x30 u32 surfaceTypeList
    +0x34 u32 camDataList
    +0x38 u32 waterBoxList
  Internal data pointers are NOT plain file offsets; the vertex array actually begins
  at (vtxList + 0x10). The polygon array is anchored at (polyList - 2) — the three
  vertex indices of poly 0 sit at file offset polyList-2 (see record layout below).

Vertex: Vec3s (s16 x,y,z), stride 6, starting at vtxList+0x10.

CollisionPoly: 20 bytes, stride 20, starting at polyList-2:
    +0x00 u16 vA  (index & 0x1FFF; top 3 bits are flags)
    +0x02 u16 vB
    +0x04 u16 vC
    +0x06 u16 (flags/material-ish; e.g. 0x55da-family constants)
    +0x08 s16 nx  (normal, / 32767)
    +0x0a s16 ny
    +0x0c s16 nz
    +0x0e f32 dist  (plane: nx*x+ny*y+nz*z == -dist for any poly vertex)
    +0x12 u16 (pad/extra)
"""
from __future__ import annotations
import struct
import math

ZSI_MAGIC_OOT = b"ZSI\x01"
ZSI_MAGIC_MM = b"ZSI\x09"
CMD_END = 0x14
CMD_COLLISION = 0x03


class Collision:
    def __init__(self, data: bytes):
        self.data = data
        self.ok = False
        self.error = ""
        self.verts = []   # list of (x,y,z) ints
        self.polys = []   # list of (vA,vB,vC, (nx,ny,nz) floats, dist float)
        self._parse()

    def _find_collision_cmd(self):
        d = self.data
        off = 0x10
        while off + 8 <= len(d):
            ctype = d[off]  # cmd1 is big-endian; byte0 = type
            cmd2 = struct.unpack_from("<I", d, off + 4)[0]
            if ctype == CMD_COLLISION:
                return cmd2
            off += 8
            if ctype == CMD_END:
                break
        return None

    def _parse(self):
        d = self.data
        if len(d) < 16 or d[:4] not in (ZSI_MAGIC_OOT, ZSI_MAGIC_MM):
            self.error = "bad ZSI magic"
            return
        hdr = self._find_collision_cmd()
        if hdr is None or hdr + 0x3c > len(d):
            self.error = "no collision command"
            return
        self.hdr_off = hdr
        nVtx = struct.unpack_from("<H", d, hdr + 0x1c)[0]
        nPoly = struct.unpack_from("<H", d, hdr + 0x1e)[0]
        pVtx = struct.unpack_from("<I", d, hdr + 0x28)[0]
        pPoly = struct.unpack_from("<I", d, hdr + 0x2c)[0]
        self.nVtx, self.nPoly = nVtx, nPoly
        vbase = pVtx + 0x10
        pbase = pPoly - 2
        if vbase + nVtx * 6 > len(d) or pbase + nPoly * 20 > len(d):
            self.error = "array out of bounds"
            return
        for i in range(nVtx):
            self.verts.append(struct.unpack_from("<3h", d, vbase + i * 6))
        for k in range(nPoly):
            o = pbase + k * 20
            vA = struct.unpack_from("<H", d, o)[0] & 0x1fff
            vB = struct.unpack_from("<H", d, o + 2)[0] & 0x1fff
            vC = struct.unpack_from("<H", d, o + 4)[0] & 0x1fff
            s = struct.unpack_from("<3h", d, o + 8)
            n = (s[0] / 32767.0, s[1] / 32767.0, s[2] / 32767.0)
            dist = struct.unpack_from("<f", d, o + 14)[0]
            self.polys.append((vA, vB, vC, n, dist))
        self.ok = True

    def triangles(self):
        for vA, vB, vC, n, dist in self.polys:
            if max(vA, vB, vC) < len(self.verts):
                yield (self.verts[vA], self.verts[vB], self.verts[vC], n, dist)


if __name__ == "__main__":
    import os, sys
    sys.path.insert(0, os.path.dirname(__file__))
    from ctr_romfs import CtrRom

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    scenes = sys.argv[1:] or ["spot04", "spot01", "gerudoway", "spot00", "ydan", "ddan"]

    def cross(a, b):
        return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])

    for sc in scenes:
        try:
            d = rom.read(rom.get(f"/scene/{sc}_info.zsi"))
        except Exception as e:
            print(f"{sc}: ROM read err {e}")
            continue
        c = Collision(d)
        if not c.ok:
            print(f"{sc}: parse FAIL: {c.error}")
            continue
        plane = nrm = tot = 0
        for A, B, C, n, dist in c.triangles():
            nA = n[0]*A[0] + n[1]*A[1] + n[2]*A[2]
            tot += 1
            if abs(nA + dist) < 2:
                plane += 1
            e1 = (B[0]-A[0], B[1]-A[1], B[2]-A[2])
            e2 = (C[0]-A[0], C[1]-A[1], C[2]-A[2])
            gn = cross(e1, e2)
            L = math.hypot(*gn) or 1
            if abs((gn[0]*n[0]+gn[1]*n[1]+gn[2]*n[2]) / L) > 0.99:
                nrm += 1
        xs = [v[0] for v in c.verts]; ys = [v[1] for v in c.verts]; zs = [v[2] for v in c.verts]
        print(f"{sc}: nVtx={c.nVtx} nPoly={c.nPoly} plane={plane}/{tot} "
              f"normal={nrm}/{tot} bbox x[{min(xs)},{max(xs)}] y[{min(ys)},{max(ys)}] z[{min(zs)},{max(zs)}]")
