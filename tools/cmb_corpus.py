#!/usr/bin/env python3
"""Shared iterators over every CMB stored in either user-supplied 3DS ROM.

Both games pack their models the same way and both are reached by the same
corpus surveys, so the population a survey measured has to be nameable: these
iterators are the only place that knows which archive container each game uses.

* Ocarina of Time 3D: ZAR members plus the inline CMB in each `.zsi` scene file.
* Majora's Mask 3D: members of `/actors/*.gar[.lzs]` (GAR2, LzS-inflated on
  demand). MM3D ships no `.zsi`-equivalent in `/actors/`, so the MM3D population
  is its actor archives and nothing else.
"""

from __future__ import annotations

import os
import struct
from collections.abc import Iterator

from ctr_romfs import CtrRom
from zar import Zar


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def iter_oot_cmbs() -> Iterator[tuple[str, bytes]]:
    """Yield ``(ROM label, CMB bytes)`` for ZAR members and scene CMBs."""
    rom_path = os.environ.get("ZELDA3D_OOT3D_ROM")
    if not rom_path:
        raise RuntimeError("source .env first (ZELDA3D_OOT3D_ROM)")

    rom = CtrRom(rom_path)
    try:
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
    finally:
        rom.fp.close()


def iter_mm3d_cmbs() -> Iterator[tuple[str, bytes]]:
    """Yield ``(archive:member label, CMB bytes)`` for every MM3D actor archive."""
    from mm_animmap_archive import Gar, Mm3dActors

    actors = Mm3dActors()
    try:
        for basename in sorted(actors.actors):
            rom_file = actors.actors[basename]
            try:
                archive = Gar(actors.rom.read(rom_file))
            except (AssertionError, IndexError, KeyError, ValueError):
                continue
            for member in archive.entries:
                if member.path.endswith(".cmb"):
                    yield f"{rom_file.path}:{member.path}", member.data
    finally:
        actors.rom.fp.close()


CORPORA = {
    "oot": iter_oot_cmbs,
    "mm": iter_mm3d_cmbs,
}


def iter_corpus(game: str):
    """Return the CMB iterator for ``game`` ('oot' or 'mm')."""
    try:
        return CORPORA[game]
    except KeyError:
        raise ValueError(f"unknown corpus {game!r}; expected one of {sorted(CORPORA)}") from None
