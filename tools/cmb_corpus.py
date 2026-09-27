#!/usr/bin/env python3
"""Shared iterators over every CMB stored in either user-supplied 3DS ROM.

Both games pack their models the same way and both are reached by the same
corpus surveys, so the population a survey measured has to be nameable: these
iterators are the only place that knows which archive container each game uses.

* Ocarina of Time 3D: every `.zar` member that is a CMB, plus the inline CMB in
  each `.zsi` scene file.
* Majora's Mask 3D: every `/actors/*.gar[.lzs]` member that is a CMB (GAR2,
  LzS-inflated on demand), every `/scenes/*.gar` member that is a CMB, plus the
  inline CMB in each `/scenes/*.zsi` scene file.

**Both games reach scenes, and an earlier version of this note was wrong about
MM3D.** It said MM3D "ships no `.zsi`-equivalent in `/actors/`" and concluded the
MM3D population was its actor archives and nothing else. The qualifier was true
and the conclusion was not: MM3D ships **424** `.zsi` files under `/scenes/`,
each carrying an inline CMB exactly as OoT3D's do, and this iterator simply never
looked there. The visible effect was that every "MM3D's corpus" percentage was an
actor-only figure while OoT3D's included 610 scene CMBs -- 1,387 + 610 for OoT3D
against 1,448 + 0 for MM3D -- so the two games were being compared over
populations of different shape. Read `fragment_lighting.py`'s printed `corpus:`
line for the current composition rather than trusting any remembered number.
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
        yield from _iter_zsi_inline_cmbs(rom, lambda path: True)
    finally:
        rom.fp.close()


def _iter_zsi_inline_cmbs(rom, predicate) -> Iterator[tuple[str, bytes]]:
    """Yield the inline CMB of every scene file `predicate` accepts.

    Shared by both games because it is the same container layout: a `.zsi` scene file carries one
    inline `cmb ` record, whose declared size is the authoritative end. Both games ship these, which
    is why the MM3D population is not actor-only.
    """
    for rom_file in rom.iter_files():
        if not rom_file.path.endswith(".zsi") or not predicate(rom_file.path):
            continue
        data = rom.read(rom_file)
        offset = data.find(b"cmb ")
        if offset < 0:
            continue
        size = _u32(data, offset + 4)
        if size <= 0 or offset + size > len(data):
            continue
        yield rom_file.path, data[offset : offset + size]


def iter_mm3d_cmbs() -> Iterator[tuple[str, bytes]]:
    """Yield ``(archive:member label, CMB bytes)`` for MM3D's actor and scene populations.

    Actors under `/actors/*.gar[.lzs]` plus scenes: `/scenes/*.gar` members and the inline CMB of
    every `/scenes/*.zsi`. The scene half is what makes the MM3D population the same shape as OoT3D's.
    """
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

        for rom_file in actors.rom.iter_files():
            if not rom_file.path.startswith("/scenes/") or not rom_file.path.endswith(".gar"):
                continue
            try:
                archive = Gar(actors.rom.read(rom_file))
            except (AssertionError, IndexError, KeyError, ValueError):
                continue
            for member in archive.entries:
                if member.path.endswith(".cmb"):
                    yield f"{rom_file.path}:{member.path}", member.data

        yield from _iter_zsi_inline_cmbs(actors.rom, lambda path: path.startswith("/scenes/"))
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
