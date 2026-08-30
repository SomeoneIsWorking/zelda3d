#!/usr/bin/env python3
"""Shared iterator over every OoT3D CMB stored in the user-supplied ROM."""

from __future__ import annotations

import os
import struct
from collections.abc import Iterator

from ctr_romfs import CtrRom
from zar import Zar


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def iter_cmbs() -> Iterator[tuple[str, bytes]]:
    """Yield ``(ROM label, CMB bytes)`` for ZAR members and scene CMBs."""
    rom_path = os.environ.get("ZELDA3D_OOT3D_ROM")
    if not rom_path:
        raise RuntimeError("source .env first (ZELDA3D_OOT3D_ROM)")

    rom = CtrRom(rom_path)
    for rom_file in rom.iter_files():
        if rom_file.path.endswith(".zar"):
            try:
                archive = Zar(rom.read(rom_file))
            except (AssertionError, IndexError, KeyError, ValueError):
                continue
            for member in archive.files:
                if member.name.endswith(".cmb"):
                    yield f"{rom_file.path}:{member.name}", archive.read(member)
        elif rom_file.path.endswith(".zsi"):
            data = rom.read(rom_file)
            offset = data.find(b"cmb ")
            if offset >= 0:
                size = _u32(data, offset + 4)
                yield rom_file.path, data[offset : offset + size]
