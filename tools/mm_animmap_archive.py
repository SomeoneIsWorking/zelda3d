"""LzS and GAR archive decoding for the MM animation-map generator."""

from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional

from mm_animmap_paths import REPO

# =============================================================== LzS (lzs.cpp)

LZS_MAGIC = b"LzS\x01"


def lzs_is_compressed(data: bytes) -> bool:
    return len(data) >= 16 and data[:4] == LZS_MAGIC


def lzs_decompress(data: bytes) -> bytes:
    if not lzs_is_compressed(data):
        raise ValueError("not an LzS\\1 buffer")
    dec_size, comp_size = struct.unpack_from("<II", data, 8)
    if comp_size == 0 or 16 + comp_size > len(data):
        raise ValueError("LzS comp_size overruns buffer")
    src = data[16 : 16 + comp_size]
    in_len = comp_size
    out = bytearray()
    buf = bytearray(4096)
    writeidx, fidx = 0xFEE, 0
    while fidx < in_len:
        flags8 = src[fidx]
        fidx += 1
        for _ in range(8):
            if fidx >= in_len:
                break
            if flags8 & 1:
                b = src[fidx]
                fidx += 1
                out.append(b)
                buf[writeidx] = b
                writeidx = (writeidx + 1) & 0xFFF
            else:
                if fidx + 1 >= in_len:
                    break
                b1, b2 = src[fidx], src[fidx + 1]
                fidx += 2
                readidx = b1 | ((b2 & 0xF0) << 4)
                for _j in range((b2 & 0x0F) + 3):
                    v = buf[readidx]
                    out.append(v)
                    buf[writeidx] = v
                    readidx = (readidx + 1) & 0xFFF
                    writeidx = (writeidx + 1) & 0xFFF
            flags8 >>= 1
    if len(out) != dec_size:
        raise ValueError(
            "LzS decompressed size mismatch: %d != %d" % (len(out), dec_size)
        )
    return bytes(out)


# =============================================================== GAR2 (gar.cpp)

GAR2_MAGIC = b"GAR\x02"
CSAB_MAGIC = b"csab"


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _cstr(b: bytes, o: int) -> str:
    if o >= len(b):
        return ""
    end = b.find(b"\x00", o)
    return b[o : (end if end >= 0 else len(b))].decode("ascii", "replace")


@dataclass
class GarFile:
    name: str = ""
    path: str = ""
    type: str = ""
    offset: int = 0
    size: int = 0
    data: bytes = b""


class Gar:
    """GAR version-2 archive; transparently LzS-inflates a compressed blob.

    NOTE (measured): the ".gar.lzs" extension is NOT a compression indicator -- most
    archives so named are stored raw. Always sniff the LzS magic, never the filename.
    """

    def __init__(self, data: bytes):
        self.was_compressed = False
        if lzs_is_compressed(data):
            data = lzs_decompress(data)
            self.was_compressed = True
        self.blob = b = data
        n = len(b)
        if n < 0x20 or b[:4] != GAR2_MAGIC:
            raise ValueError("not a GAR2 archive")
        n_types, n_files = _u16(b, 0x08), _u16(b, 0x0A)
        types_off, files_off, datahdr_off = _u32(b, 0x0C), _u32(b, 0x10), _u32(b, 0x14)
        self.codec = _cstr(b, 0x18)
        if (
            types_off + 16 * n_types > n
            or files_off + 12 * n_files > n
            or datahdr_off + 4 * n_files > n
        ):
            raise ValueError("GAR2 table out of range")
        self.entries: List[GarFile] = []
        for i in range(n_files):
            fe = files_off + 12 * i
            fsize = _u32(b, fe)
            off = _u32(b, datahdr_off + 4 * i)
            f = GarFile(
                name=_cstr(b, _u32(b, fe + 4)),
                path=_cstr(b, _u32(b, fe + 8)),
                offset=off,
                size=fsize,
            )
            f.data = b[off : off + fsize] if off + fsize <= n else b""
            self.entries.append(f)
        for t in range(n_types):
            e = types_off + 16 * t
            cnt, idx_off = _u32(b, e), _u32(b, e + 4)
            tname = _cstr(b, _u32(b, e + 8))
            if idx_off == 0xFFFFFFFF or idx_off + 4 * cnt > n:
                continue
            for k in range(cnt):
                fi = _u32(b, idx_off + 4 * k)
                if fi < len(self.entries):
                    self.entries[fi].type = tname

    def clip_names(self, verify: bool = True) -> List[str]:
        """CSAB clip names, i.e. the kMMAnimMaps csab-side strings.

        The name is the GAR member SHORT name minus a .csab extension -- MM3D CSABs
        (subversion 5) carry no internal name field. Members are selected by the GAR type
        table ('csab'), falling back to the suffix, and (when verify) checked for the
        'csab' magic so a mistyped member can't leak in.
        """
        out, seen = [], set()
        for f in self.entries:
            leaf = os.path.basename((f.name or f.path).replace("\\", "/"))
            is_csab = (
                f.type == "csab"
                or leaf.lower().endswith(".csab")
                or f.path.lower().endswith(".csab")
            )
            if not is_csab:
                continue
            if verify and f.data and f.data[:4] != CSAB_MAGIC:
                continue
            if leaf.lower().endswith(".csab"):
                leaf = leaf[:-5]
            if leaf and leaf not in seen:
                seen.add(leaf)
                out.append(leaf)
        return out


class Mm3dActors:
    """Index of /actors/*.gar[.lzs] in the MM3D ROM, keyed by archive basename."""

    def __init__(self, rom_path: Optional[str] = None):
        sys.path.insert(0, os.path.join(REPO, "tools"))
        from ctr_romfs import CtrRom  # noqa: E402

        rom_path = rom_path or os.environ.get("ZELDA3D_MM3D_ROM")
        if not rom_path:
            raise SystemExit("ZELDA3D_MM3D_ROM not set (see <repo>/.env)")
        self.rom_path = rom_path
        self.rom = CtrRom(rom_path)
        self.actors: Dict[str, object] = {}
        for f in self.rom.iter_files():
            if not f.path.startswith("/actors/"):
                continue
            base = os.path.basename(f.path)
            for suf in (".gar.lzs", ".gar"):
                if base.endswith(suf):
                    self.actors.setdefault(base[: -len(suf)], f)
                    break
        self._cache: Dict[str, List[str]] = {}

    def clips(self, basename: str) -> Optional[List[str]]:
        if basename in self._cache:
            return self._cache[basename]
        fe = self.actors.get(basename)
        if fe is None:
            return None
        names = Gar(self.rom.read(fe)).clip_names()
        self._cache[basename] = names
        return names
