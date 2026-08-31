#!/usr/bin/env python3
"""Parser for OoT3D ZSI scene/room info files (the per-room "Zelda Scene Info").

A ROOM file (`<scene>_<R>_info.zsi`) wraps the room's GEOMETRY as a single embedded
CMB. A SCENE header file (`<scene>_info.zsi`, no room number) holds the room list,
actor/spawn lists, collision and lighting (the N64 scene-header analogue) and has NO
embedded CMB.

Format (see noclip OcarinaOfTime3D/zsi.ts):
  0x00  magic  "ZSI\\x01" (Ocarina) / "ZSI\\x09" (Majora)
  0x04  name   12 bytes (e.g. "ShUnqueen")
  0x10  command list: 8-byte commands until type 0x14 (End).
          cmd1 = u32 big-endian   -> byte0 = command type, byte1 = entry count
          cmd2 = u32 little-endian -> address/data

  Command 0x0A (Mesh) marks that the room has renderable geometry.

Locating the room CMB
  noclip resolves cmd-0x0A's cmd2 -> mesh header -> entries -> opaque/transparent CMB
  pointers. On the USA decrypted ROM those data-section addresses do NOT resolve to
  the embedded CMB at a plain file offset (the addressing carries a base/segment this
  port hasn't pinned down), and EMPIRICALLY every one of the game's 390 room files
  contains EXACTLY ONE `cmb ` blob (verified). OoT3D rooms are a single multi-material
  CMB; the opaque/transparent split that N64 splits across mesh entries lives WITHIN
  that CMB's materials (alpha-blend flags), which the renderer already honours per
  material. So we (a) require a 0x0A Mesh command to exist (proves geometry, not a
  header-only file), then (b) take the single `cmb ` blob, sized by its own header.
  This is robust and not a hardcoded offset (gerudoway happens to put it at 96).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

ZSI_MAGIC_OOT = b"ZSI\x01"
ZSI_MAGIC_MM = b"ZSI\x09"
CMD_END = 0x14
CMD_MESH = 0x0A
CMD_ACTOR_LIST = 0x01


@dataclass(frozen=True)
class Actor:
    """One 16-byte room actor record, decoded from the ZSI's little-endian table."""

    actor_id: int
    x: int
    y: int
    z: int
    rot_x: int
    rot_y: int
    rot_z: int
    params: int


class Zsi:
    def __init__(self, data: bytes):
        self.data = data
        self.ok = False
        self.error = ""
        self.name = ""
        self.commands = []          # list of (type, count, cmd2)
        self.has_mesh = False
        self.cmb_off = -1
        self.cmb_size = 0
        self.actors: list[Actor] = []
        self._parse()

    def _parse(self):
        d = self.data
        if len(d) < 16 or d[:4] not in (ZSI_MAGIC_OOT, ZSI_MAGIC_MM):
            self.error = "bad ZSI magic"
            return
        self.name = d[4:16].split(b"\x00")[0].decode("ascii", "replace")
        off = 16
        while off + 8 <= len(d):
            cmd1 = struct.unpack_from(">I", d, off)[0]
            cmd2 = struct.unpack_from("<I", d, off + 4)[0]
            ctype = (cmd1 >> 24) & 0xFF
            count = (cmd1 >> 16) & 0xFF
            self.commands.append((ctype, count, cmd2))
            if ctype == CMD_MESH:
                self.has_mesh = True
            elif ctype == CMD_ACTOR_LIST:
                self._parse_actor_list(count, cmd2)
            off += 8
            if ctype == CMD_END:
                break
        # Locate the single embedded room CMB (see module docstring).
        j = d.find(b"cmb ", 16)
        if j >= 0:
            self.cmb_off = j
            self.cmb_size = struct.unpack_from("<I", d, j + 4)[0]
            # Sanity: exactly one CMB in a room file.
            k = d.find(b"cmb ", j + 1)
            if k >= 0:
                self.error = "unexpected: >1 embedded CMB"
        self.ok = True

    def _parse_actor_list(self, count: int, offset: int) -> None:
        """Decode the documented direct-offset actor table, refusing truncated data."""
        stride = 16
        end = offset + count * stride
        if end > len(self.data):
            self.error = (
                f"actor list at 0x{offset:x} with {count} entries exceeds file size"
            )
            return
        for index in range(count):
            fields = struct.unpack_from("<8h", self.data, offset + index * stride)
            self.actors.append(Actor(*fields))

    def has_geometry(self) -> bool:
        """True for a room file with a renderable embedded CMB."""
        return self.has_mesh and self.cmb_off >= 0 and self.cmb_size > 0

    def cmb_bytes(self) -> bytes:
        if not self.has_geometry():
            return b""
        return self.data[self.cmb_off:self.cmb_off + self.cmb_size]


if __name__ == "__main__":
    import os
    import sys
    sys.path.insert(0, os.path.dirname(__file__))
    import cmb as cmbmod
    from ctr_romfs import CtrRom

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    path = sys.argv[1] if len(sys.argv) > 1 else "/scene/gerudoway_0_info.zsi"
    z = Zsi(rom.read(rom.get(path)))
    print(f"{path}: ok={z.ok} name={z.name!r} has_mesh={z.has_mesh} "
          f"cmb_off={z.cmb_off} cmb_size={z.cmb_size}")
    print("  commands:", [(hex(t), c, hex(a)) for t, c, a in z.commands])
    if z.has_geometry():
        m = cmbmod.Cmb(z.cmb_bytes())
        tris = list(m.triangles())  # (sepd_idx, mat_idx, [(idx,pos,nrm,uv) x3])
        verts = [v for _, _, tri in tris for v in tri]
        xs = [v[1][0] for v in verts]
        ys = [v[1][1] for v in verts]
        zs = [v[1][2] for v in verts]
        print(f"  CMB {m.name!r}: {len(tris)} tris, {len(m.materials)} mats, "
              f"{len(m.textures)} texs")
        print(f"  bbox x[{min(xs):.0f},{max(xs):.0f}] "
              f"y[{min(ys):.0f},{max(ys):.0f}] z[{min(zs):.0f},{max(zs):.0f}]")
