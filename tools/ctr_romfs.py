#!/usr/bin/env python3
"""Minimal, dependency-free reader for a *decrypted* 3DS cartridge image (CCI/.3ds).

Parses NCSD -> NCCH (partition 0, the game) -> RomFS (IVFC level 3) so we can
list and extract the game's data files (the ZAR archives, etc.) without any
emulator, bootrom, or external tool. Crypto is intentionally not handled: this
only works on already-decrypted dumps (which is what we have).

Format references: 3dbrew NCSD/NCCH/RomFS, CloudModding wiki.
"""
from __future__ import annotations
import struct, io, os
from dataclasses import dataclass, field
from typing import Optional

MEDIA = 0x200  # media unit size


def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _u32(b, o): return struct.unpack_from("<I", b, o)[0]
def _u64(b, o): return struct.unpack_from("<Q", b, o)[0]
def _align(x, a): return (x + a - 1) & ~(a - 1)


@dataclass
class FileEntry:
    name: str
    path: str
    offset: int   # absolute offset into the ROM file
    size: int


@dataclass
class DirEntry:
    name: str
    path: str
    dirs: dict = field(default_factory=dict)
    files: dict = field(default_factory=dict)


class CtrRom:
    def __init__(self, path: str):
        self.path = path
        self.fp = open(path, "rb")
        self._parse_ncsd()
        self._parse_ncch()
        self._parse_romfs()

    # ---- NCSD ----
    def _parse_ncsd(self):
        self.fp.seek(0)
        hdr = self.fp.read(0x200)
        if hdr[0x100:0x104] != b"NCSD":
            raise ValueError("not an NCSD/CCI image (missing NCSD magic at 0x100)")
        # partition table: 8 entries of (u32 offset_media, u32 size_media) at 0x120
        off = _u32(hdr, 0x120) * MEDIA
        size = _u32(hdr, 0x124) * MEDIA
        if size == 0:
            raise ValueError("partition 0 is empty")
        self.ncch_off = off
        self.ncch_size = size

    # ---- NCCH ----
    def _parse_ncch(self):
        self.fp.seek(self.ncch_off)
        h = self.fp.read(0x200)
        if h[0x100:0x104] != b"NCCH":
            raise ValueError("partition 0 is not an NCCH (missing magic)")
        flags = h[0x188:0x190]
        # flags[7]: bit0x01 fixedkey, bit0x04 nocrypto. We require decrypted.
        self.nocrypto = bool(flags[7] & 0x04)
        self.romfs_off = self.ncch_off + _u32(h, 0x1B0) * MEDIA
        self.romfs_size = _u32(h, 0x1B4) * MEDIA
        self.exefs_off = self.ncch_off + _u32(h, 0x1A0) * MEDIA
        self.exefs_size = _u32(h, 0x1A4) * MEDIA
        self.product_code = h[0x150:0x160].split(b"\x00")[0].decode("ascii", "replace")
        if self.romfs_size == 0:
            raise ValueError("NCCH has no RomFS")

    # ---- RomFS (IVFC) ----
    def _parse_romfs(self):
        self.fp.seek(self.romfs_off)
        iv = self.fp.read(0x60)
        if iv[0:4] != b"IVFC" or _u32(iv, 4) != 0x10000:
            raise ValueError("RomFS IVFC header not found / unexpected")
        master_hash_size = _u32(iv, 0x08)
        lvl3_blocksize = 1 << _u32(iv, 0x4C)
        # Level 3 (the actual metadata + file data) begins after the IVFC header
        # + master hash, aligned up to the level-3 block size.
        base = self.romfs_off + _align(0x60 + master_hash_size, lvl3_blocksize)
        self.l3_base = base
        self.fp.seek(base)
        mh = self.fp.read(0x28)  # RomFS level3 metadata header
        self.dir_hash_off  = base + _u32(mh, 0x04)
        self.dir_meta_off  = base + _u32(mh, 0x0C)
        self.dir_meta_len  = _u32(mh, 0x10)
        self.file_hash_off = base + _u32(mh, 0x14)
        self.file_meta_off = base + _u32(mh, 0x1C)
        self.file_meta_len = _u32(mh, 0x20)
        self.file_data_off = base + _u32(mh, 0x24)
        # read whole metadata tables into memory (small)
        self.fp.seek(self.dir_meta_off);  self._dirmeta  = self.fp.read(self.dir_meta_len)
        self.fp.seek(self.file_meta_off); self._filemeta = self.fp.read(self.file_meta_len)
        self.root = DirEntry(name="", path="")
        self._walk_dir(0, self.root)

    def _read_dir(self, off):
        b = self._dirmeta
        parent   = _u32(b, off + 0x00)
        sibling  = _u32(b, off + 0x04)
        child    = _u32(b, off + 0x08)
        first_f  = _u32(b, off + 0x0C)
        namelen  = _u32(b, off + 0x14)
        name = b[off + 0x18: off + 0x18 + namelen].decode("utf-16-le") if namelen else ""
        return sibling, child, first_f, name

    def _read_file(self, off):
        b = self._filemeta
        sibling  = _u32(b, off + 0x04)
        data_off = _u64(b, off + 0x08)
        data_sz  = _u64(b, off + 0x10)
        namelen  = _u32(b, off + 0x1C)
        name = b[off + 0x20: off + 0x20 + namelen].decode("utf-16-le") if namelen else ""
        return sibling, data_off, data_sz, name

    def _walk_dir(self, off, node: DirEntry):
        INVALID = 0xFFFFFFFF
        sibling, child, first_f, _ = self._read_dir(off)
        # files
        f = first_f
        while f != INVALID:
            fsib, fdo, fds, fname = self._read_file(f)
            p = node.path + "/" + fname
            node.files[fname] = FileEntry(fname, p, self.file_data_off + fdo, fds)
            f = fsib
        # child dirs
        c = child
        while c != INVALID:
            csib, cchild, cfirst, cname = self._read_dir(c)
            sub = DirEntry(name=cname, path=node.path + "/" + cname)
            node.dirs[cname] = sub
            self._walk_dir(c, sub)
            c = csib

    # ---- public helpers ----
    def iter_files(self, node: Optional[DirEntry] = None):
        node = node or self.root
        for fe in node.files.values():
            yield fe
        for d in node.dirs.values():
            yield from self.iter_files(d)

    def get(self, path: str) -> FileEntry:
        parts = [p for p in path.split("/") if p]
        node = self.root
        for p in parts[:-1]:
            node = node.dirs[p]
        return node.files[parts[-1]]

    def read(self, fe: FileEntry) -> bytes:
        self.fp.seek(fe.offset)
        return self.fp.read(fe.size)

    def extract(self, fe: FileEntry, dest: str):
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as o:
            o.write(self.read(fe))


if __name__ == "__main__":
    import sys
    rom = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("ZELDA3D_OOT3D_ROM")
    if not rom:
        sys.exit("usage: ctr_romfs.py <oot3d.3ds>  (or set ZELDA3D_OOT3D_ROM; see .env)")
    r = CtrRom(rom)
    print(f"product={r.product_code} nocrypto={r.nocrypto} "
          f"romfs@0x{r.romfs_off:x} size=0x{r.romfs_size:x}")
    files = list(r.iter_files())
    print(f"total files: {len(files)}")
    # summarize by extension
    from collections import Counter
    ext = Counter(os.path.splitext(f.name)[1].lower() for f in files)
    for e, n in ext.most_common():
        print(f"  {e or '(noext)':10} {n}")
