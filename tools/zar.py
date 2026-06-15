#!/usr/bin/env python3
"""Parser for OoT3D ZAR archives (the container holding CMB models, textures, etc).

Format: CloudModding 3D:ZAR_format, verified against zelda_tsubo.zar.
Files inside a ZAR are NOT compressed in OoT3D (the on-disk RomFS .zar is plain);
each file's data is located via the per-file data-offset table in the data section.
"""
from __future__ import annotations
import struct
from dataclasses import dataclass


def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _u32(b, o): return struct.unpack_from("<I", b, o)[0]
def _cstr(b, o):
    e = b.index(b"\x00", o)
    return b[o:e].decode("ascii", "replace")


@dataclass
class ZarFile:
    name: str
    type: str
    size: int
    offset: int   # offset into the ZAR blob


class Zar:
    def __init__(self, data: bytes):
        self.data = data
        if data[0:4] != b"ZAR\x01":
            raise ValueError("not a ZAR archive")
        self.size       = _u32(data, 0x04)
        n_types         = _u16(data, 0x08)
        n_files         = _u16(data, 0x0A)
        types_off       = _u32(data, 0x0C)
        meta_off        = _u32(data, 0x10)
        data_off        = _u32(data, 0x14)
        # per-file data offsets live at the start of the data section
        self._data_offsets = [_u32(data, data_off + 4 * i) for i in range(n_files)]
        # file metadata: size + name
        self.files: list[ZarFile] = []
        for i in range(n_files):
            m = meta_off + 8 * i
            fsize = _u32(data, m)
            fname = _cstr(data, _u32(data, m + 4))
            self.files.append(ZarFile(fname, "", fsize, self._data_offsets[i]))
        # file types: assign a type string to each file index
        for t in range(n_types):
            e = types_off + 16 * t
            cnt = _u32(data, e)
            idx_off = _u32(data, e + 4)
            tname = _cstr(data, _u32(data, e + 8))
            if idx_off == 0xFFFFFFFF:
                continue
            for k in range(cnt):
                fi = _u32(data, idx_off + 4 * k)
                self.files[fi].type = tname

    def read(self, f: ZarFile) -> bytes:
        return self.data[f.offset: f.offset + f.size]

    def by_type(self, t: str):
        return [f for f in self.files if f.type == t]


if __name__ == "__main__":
    import sys
    path = sys.argv[1] if len(sys.argv) > 1 else "scratch/extract/zelda_tsubo.zar"
    z = Zar(open(path, "rb").read())
    print(f"{path}: {len(z.files)} files, archive size {z.size}")
    for f in z.files:
        print(f"  [{f.type or '?':6}] {f.name:32} size={f.size:>8} @0x{f.offset:x}")
