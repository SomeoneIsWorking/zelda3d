#!/usr/bin/env python3
"""Prepare a content-decrypted 3DS image so the Azahar oracle will load it.

**Why this exists.** The Azahar harness is game-agnostic: it takes a ROM path (`main.cpp:117-134`) and
hands it to `retro_load_game` with no identity check. The OoT3D dump loads. The Majora's Mask 3D dump
does not, and it fails in about two milliseconds with no visible reason -- Azahar reports it through
`RETRO_ENVIRONMENT_SET_MESSAGE`, which the libretro shim does not handle
(`src/core/frontend/../libretro_callbacks.cpp:119`). The reason is one header bit:

    Azahar/src/core/file_sys/ncch_container.cpp:281
        if (!ncch_header.no_crypto) {
            // Encrypted NCCH are not supported
            return Loader::ResultStatus::ErrorEncrypted;
        }

MM3D's image is *content*-decrypted -- this repo's own `tools/gen_mm_scene_names.py` reads 102 scenes
out of it, and `cmb_corpus` reads its material chunks -- but its NCCH header flags byte is `0x00` where
OoT3D's is `0x04`. So the bit describes a payload that is already plaintext as encrypted, and setting
it in a copy is the header correction "decrypted" means. It is not a patch to the game and it does not
touch content: the ROM is copied first and exactly one byte changes.

**This is what makes MM3D observable at all.** Without it the project has no MM3D frames and no MM3D
PICA register reads, so every MM3D graphics claim is uncheckable.

Usage:
    python3 tools/ctr_oracle_rom.py <src.3ds> <dst.3ds>
    python3 tools/ctr_oracle_rom.py <src.3ds> <dst.3ds> --check     # report, do not write
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path

# The 3DS card is NCSD; partition 0 (the game partition) starts at block 0x20 and its header is
# "NCCH\0" followed by the 0x180-byte NCCH_Header. `flags` is the byte at 0x18F of that header.
#
# The magic is NOT reliably AT the partition base on the dumps this project has: both its ROMs carry
# a 0x100-byte prefix, so "NCCH" sits at 0x4100 while the header the emulator uses is the one at
# 0x4000. Getting this wrong is silent and expensive -- the first version of this tool assumed the
# magic was at the base, its guard fired on the known-good OoT3D image, and the only reason that was
# caught is that the OoT3D control is in the test suite. So: the base is the block-aligned partition
# start, the magic is ACCEPTED anywhere in the first few blocks of it (to absorb a dump prefix), and
# the flags offset is always base + 0x18F, which is the field Azahar reads.
PARTITION0_BLOCK = 0x20
BLOCK_SIZE = 0x200
NCCH_BASE = PARTITION0_BLOCK * BLOCK_SIZE  # 0x4000
NCCH_MAGIC_SEARCH = 0x800  # how far into the partition to accept the magic for a dump prefix
NCCH_FLAGS_OFFSET = NCCH_BASE + 0x18F
NO_CRYPTO_BIT = 0x04

# The media ID is the 3DS container's own identity field, and it is what distinguishes a game's image
# from the other's. Reading it is strictly better than trusting a filename: with both a 3DS release
# and its N64 counterpart in play, a glob like `*.3ds` can hand one game's ROM to the other game's
# oracle, and every reading taken through it is then confidently about the wrong game.
#
# The two values below were each measured off the real images and cross-checked against the program
# ID the same header carries (OoT3D `0x0004000000033500`, MM3D `0x0004000000125500`).
NCCH_MEDIA_ID_OFFSET = NCCH_BASE + 0x150
OOT3D_MEDIA_ID = b"CTR-P-AQEE"
MM3D_MEDIA_ID = b"CTR-P-AJRE"


def read_media_id(path: Path) -> bytes:
    """The image's NCCH media ID, e.g. `CTR-P-AQEE`, or raise if this is not the assumed layout.

    The field is 11 bytes of NUL-padded ASCII — `CTR-P-AQEE` is 10 characters plus its terminator —
    so it is read as text and right-stripped. Comparing raw field bytes against a 10-character
    constant fails, and reading a fixed 10 bytes would silently ignore the padding rather than fix it.
    """
    _require_ncch(path)
    with path.open("rb") as handle:
        handle.seek(NCCH_MEDIA_ID_OFFSET)
        return handle.read(11).rstrip(b"\x00")


def describe(path: Path) -> str:
    """A one-line identity summary, for logs and for refusing an ambiguous drop-in.

    Deliberately only the two fields verified against both real images: the media ID and the
    `no_crypto` flag. The header also carries the 64-bit program ID, but its byte order is not
    established here — a first attempt at it read byte-swapped, and an unverified field in a
    diagnostic is exactly how a confident wrong reading gets made downstream. It is left out rather
    than shipped approximately.
    """
    try:
        media = read_media_id(path).decode("ascii", "replace")
        flags = read_flags(path)
    except ValueError as error:
        return f"unidentified ({error})"
    return f"{media} no_crypto={flags >> 2 & 1}"


def _require_ncch(path: Path) -> None:
    with path.open("rb") as handle:
        handle.seek(NCCH_BASE)
        window = handle.read(NCCH_MAGIC_SEARCH)
    if b"NCCH" not in window:
        raise ValueError(
            f"{path}: no NCCH header within 0x{NCCH_MAGIC_SEARCH:x} of partition 0 "
            f"(0x{NCCH_BASE:x}); refusing to guess a layout"
        )


def read_flags(path: Path) -> int:
    """The NCCH `flags` byte of partition 0, or raise if this is not the layout assumed.

    Refusing is the point: flipping a bit at a guessed offset in a container we do not understand is
    how a ROM gets corrupted, and the failure is silent because the emulator's own complaint
    (`ErrorEncrypted`) arrives through a libretro message callback nobody handles.
    """
    _require_ncch(path)
    with path.open("rb") as handle:
        handle.seek(NCCH_FLAGS_OFFSET)
        return handle.read(1)[0]


def set_no_crypto(path: Path) -> tuple[int, int]:
    """OR the no_crypto bit into the NCCH header. Returns (before, after)."""
    before = read_flags(path)
    with path.open("r+b") as handle:
        handle.seek(NCCH_FLAGS_OFFSET)
        handle.write(bytes([before | NO_CRYPTO_BIT]))
    return before, read_flags(path)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--check", action="store_true", help="report the flags without writing")
    args = parser.parse_args(argv)

    source = Path(args.source)
    if not source.is_file():
        print(f"ctr_oracle_rom: no such ROM: {source}", file=sys.stderr)
        return 2
    try:
        before = read_flags(source)
    except ValueError as error:
        print(f"ctr_oracle_rom: {error}", file=sys.stderr)
        return 2

    print(f"ctr_oracle_rom: source NCCH flags @0x{NCCH_FLAGS_OFFSET:x} = 0x{before:02x} "
          f"(no_crypto={before >> 2 & 1})")
    if args.check:
        print("ctr_oracle_rom: --check, nothing written")
        return 0

    destination = Path(args.destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    was, now = set_no_crypto(destination)
    size = os.path.getsize(destination)
    print(f"ctr_oracle_rom: wrote {destination} ({size} bytes, sha256 {hashlib.sha256(destination.read_bytes()).hexdigest()[:16]})")
    print(f"ctr_oracle_rom: NCCH flags 0x{was:02x} -> 0x{now:02x} (no_crypto={now >> 2 & 1})")
    if not now >> 2 & 1:
        print("ctr_oracle_rom: no_crypto is still clear; the emulator will refuse this image",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
