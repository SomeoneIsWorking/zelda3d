"""Read-only game identity checks for supported N64 and 3DS ROM inputs."""

from __future__ import annotations

import struct
from pathlib import Path


def normalized_n64_header(path: Path) -> bytes | None:
    try:
        with path.open("rb") as rom:
            header = rom.read(0x40)
    except OSError:
        return None
    if len(header) < 0x40:
        return None
    magic = header[:4]
    if magic == b"\x80\x37\x12\x40":
        return header
    if magic == b"\x40\x12\x37\x80":
        return b"".join(header[index : index + 4][::-1] for index in range(0, 0x40, 4))
    if magic == b"\x37\x80\x40\x12":
        return b"".join(header[index : index + 2][::-1] for index in range(0, 0x40, 2))
    return None


def _n64_identity(path: Path) -> tuple[bytes, bytes] | None:
    header = normalized_n64_header(path)
    if header is None:
        return None
    return header[0x20:0x34].rstrip(b"\x00 ").upper(), header[0x3B:0x3E]


def is_oot_n64_rom(path: Path) -> bool:
    identity = _n64_identity(path)
    return identity is not None and (
        identity[1] == b"CZL" or b"LEGEND OF ZELDA" in identity[0]
    )


def is_mm_n64_rom(path: Path) -> bool:
    identity = _n64_identity(path)
    return identity is not None and (identity[1] == b"NZS" or b"MAJORA" in identity[0])


def _decrypted_3ds_product_code(path: Path) -> bytes | None:
    try:
        with path.open("rb") as rom:
            ncsd = rom.read(0x200)
            if len(ncsd) != 0x200 or ncsd[0x100:0x104] != b"NCSD":
                return None
            partition_offset = struct.unpack_from("<I", ncsd, 0x120)[0] * 0x200
            rom.seek(partition_offset)
            ncch = rom.read(0x200)
    except (OSError, struct.error):
        return None
    if len(ncch) != 0x200 or ncch[0x100:0x104] != b"NCCH":
        return None
    return ncch[0x150:0x160].split(b"\x00", 1)[0]


def is_oot3d_rom(path: Path) -> bool:
    product_code = _decrypted_3ds_product_code(path)
    return product_code is not None and product_code.startswith(b"CTR-P-AQE")


def is_mm3d_rom(path: Path) -> bool:
    product_code = _decrypted_3ds_product_code(path)
    return product_code is not None and product_code.startswith(b"CTR-P-AJR")
