#!/usr/bin/env python3
"""Reader for OoT3D/MM3D CTXB standalone-texture containers.

Python mirror of Shipwright/cmb3d/asset/ctxb.{cpp,h} (which is itself a port of the
noclip oot3d/CTXB.ts layout). The C++ decoder is the authority; this file must stay
byte-for-byte equivalent to it.

  CTXB layout:
    0x00 'ctxb'      0x04 fileSize   0x08 chunkCount
    0x10 texChunkOff 0x14 texDataOff
  texChunkOff -> "tex " chunk, byte-identical to a CMB tex chunk:
    magic@0, count@8, entries@0x0C, each 0x24 bytes:
      data_len@0x00 (u32), width@0x08 (u16), height@0x0A (u16), fmt@0x0C (u16),
      data_type@0x0E (u16), data_offset@0x10 (u32), name@0x14 (16-byte NUL-padded)
  Texel bytes live at file offset texDataOff + entry.data_offset.
  glFormat = (data_type << 16) | fmt, fed to the PICA decoder (tools/pica_texture.py).

CLI:
  python3 tools/ctxb.py list  <rom-path-or-file>            # list sub-textures
  python3 tools/ctxb.py dump  <rom-path-or-file> <outdir>   # PNGs + contact sheet
where <rom-path-or-file> is either a path inside the ROM ("/menu/01_US_ENGLISH/hud_all00.ctxb",
resolved via ZELDA3D_OOT3D_ROM) or a local file.
"""
from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass
from typing import List, Optional

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools import pica_texture


@dataclass
class CtxbTexture:
    index: int
    name: str
    width: int
    height: int
    fmt: int
    data_type: int
    data_offset: int
    data_len: int

    @property
    def gl_format(self) -> int:
        return (self.data_type << 16) | self.fmt

    @property
    def format_name(self) -> str:
        return pica_texture.GLFMT.get(self.gl_format, "0x%08X" % self.gl_format)


class Ctxb:
    """Parsed CTXB container. Raises ValueError on a malformed file."""

    def __init__(self, data: bytes, source: str = "<memory>"):
        self.data = data
        self.source = source
        if len(data) < 0x18 or data[0:4] != b"ctxb":
            raise ValueError("%s: not a ctxb" % source)
        self.file_size = struct.unpack_from("<I", data, 0x04)[0]
        self.chunk_count = struct.unpack_from("<I", data, 0x08)[0]
        tex_chunk_off = struct.unpack_from("<I", data, 0x10)[0]
        self.texdata_off = struct.unpack_from("<I", data, 0x14)[0]
        if tex_chunk_off + 0x0C > len(data) or data[tex_chunk_off:tex_chunk_off + 4] != b"tex ":
            raise ValueError("%s: missing tex chunk" % source)
        n = struct.unpack_from("<I", data, tex_chunk_off + 8)[0]
        o = tex_chunk_off + 0x0C
        self.textures: List[CtxbTexture] = []
        for i in range(n):
            if o + 0x24 > len(data):
                raise ValueError("%s: truncated tex chunk at entry %d" % (source, i))
            dlen = struct.unpack_from("<I", data, o)[0]
            w = struct.unpack_from("<H", data, o + 0x08)[0]
            h = struct.unpack_from("<H", data, o + 0x0A)[0]
            fmt = struct.unpack_from("<H", data, o + 0x0C)[0]
            dtype = struct.unpack_from("<H", data, o + 0x0E)[0]
            doff = struct.unpack_from("<I", data, o + 0x10)[0]
            raw_name = data[o + 0x14:o + 0x24]
            name = raw_name.split(b"\x00", 1)[0].decode("ascii", "replace")
            self.textures.append(CtxbTexture(i, name, w, h, fmt, dtype, doff, dlen))
            o += 0x24

    # ---- data access -------------------------------------------------
    def texture_raw(self, t: CtxbTexture) -> bytes:
        base = self.texdata_off + t.data_offset
        if base + t.data_len > len(self.data):
            raise ValueError("%s: texture %d out of range" % (self.source, t.index))
        return self.data[base:base + t.data_len]

    def decode_rgba(self, i: int):
        """-> (width, height, bytearray RGBA8, row 0 = top)."""
        t = self.textures[i]
        px = pica_texture.decode(t.gl_format, t.width, t.height, self.texture_raw(t))
        return t.width, t.height, px

    def image(self, i: int):
        from PIL import Image
        w, h, px = self.decode_rgba(i)
        return Image.frombytes("RGBA", (w, h), bytes(px))


# ---- loading -------------------------------------------------------------
def load(path: str) -> Ctxb:
    """Load from a local file, or from the OoT3D ROM if `path` looks like a romfs path."""
    if path.startswith("/") and not os.path.exists(path):
        return Ctxb(read_rom_file(path), path)
    if os.path.exists(path):
        with open(path, "rb") as f:
            return Ctxb(f.read(), path)
    return Ctxb(read_rom_file(path), path)


def read_rom_file(rompath: str, rom: Optional[str] = None) -> bytes:
    from tools.ctr_romfs import CtrRom
    rom = rom or os.environ.get("ZELDA3D_OOT3D_ROM") or os.environ.get("SOH3D_3DS_ROM")
    if not rom:
        raise RuntimeError("ZELDA3D_OOT3D_ROM not set (source .env)")
    r = CtrRom(rom)
    want = rompath if rompath.startswith("/") else "/" + rompath
    for f in r.iter_files():
        if f.path == want:
            return r.read(f)
    raise FileNotFoundError("%s not in ROM" % rompath)


# ---- CLI -----------------------------------------------------------------
def _contact_sheet(c: Ctxb, out: str, cols: int = 4, cell: int = 256):
    from PIL import Image, ImageDraw
    n = len(c.textures)
    cols = min(cols, max(1, n))
    rows = (n + cols - 1) // cols
    pad = 18
    sheet = Image.new("RGBA", (cols * (cell + pad), rows * (cell + pad + 14)), (40, 40, 48, 255))
    dr = ImageDraw.Draw(sheet)
    for i, t in enumerate(c.textures):
        im = c.image(i)
        im.thumbnail((cell, cell))
        x = (i % cols) * (cell + pad) + pad // 2
        y = (i // cols) * (cell + pad + 14) + 14
        # checkerboard so alpha is visible
        bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
        for by in range(0, im.size[1], 8):
            for bx in range(0, im.size[0], 8):
                if ((bx // 8) + (by // 8)) % 2:
                    for yy in range(by, min(by + 8, im.size[1])):
                        for xx in range(bx, min(bx + 8, im.size[0])):
                            bg.putpixel((xx, yy), (190, 190, 190, 255))
        bg.alpha_composite(im)
        sheet.paste(bg, (x, y))
        dr.text((x, y - 12), "%d %s %dx%d %s" % (i, t.name, t.width, t.height, t.format_name),
                fill=(255, 230, 120, 255))
    sheet.save(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, path = argv[1], argv[2]
    c = load(path)
    if cmd == "list":
        print("%s: %d textures, texdata@0x%X" % (path, len(c.textures), c.texdata_off))
        for t in c.textures:
            print("  [%2d] %-20s %4dx%-4d %-9s len=%-8d off=0x%X"
                  % (t.index, t.name, t.width, t.height, t.format_name, t.data_len, t.data_offset))
        return 0
    if cmd == "dump":
        outdir = argv[3] if len(argv) > 3 else "scratch/ctxb"
        os.makedirs(outdir, exist_ok=True)
        stem = os.path.basename(path).replace(".ctxb", "")
        for t in c.textures:
            im = c.image(t.index)
            fn = os.path.join(outdir, "%s_%02d_%s.png" % (stem, t.index, t.name or "unnamed"))
            im.save(fn)
            print(fn, t.width, t.height, t.format_name)
        _contact_sheet(c, os.path.join(outdir, "%s_sheet.png" % stem))
        return 0
    print("unknown command", cmd)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
