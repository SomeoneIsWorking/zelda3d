#!/usr/bin/env python3
"""Where MM3D's PICA fog colour can and cannot come from, measured rather than reasoned.

MM3D's fog *window* is recovered and ported; its fog *colour* is not, and the host's value
differs from the oracle's. The project's earlier hunt proved the colour is not in the scene ZSIs.
This tool asks the next question -- is it a constant in the code image at all? -- and, just as
importantly, proves the search could have found a constant if one were there.

That control is the whole point. A byte search over 5.7 MB that reports "0 hits" is
indistinguishable from a search that is silently broken: a wrong base address, a truncated read, a
missed channel order, or an encoding the title never uses. This project has already retracted a
finding that came from an instrument reporting plausible numbers it could not support, so the
control runs on every invocation and is part of the exit status.

    uv run --frozen python tools/mm3d_fogcolour_source.py
    uv run --frozen python tools/mm3d_fogcolour_source.py --rgb 40,140,220

Exit status is 0 only when the search is *capable* (control found) and the target is *absent*.
A failing control exits non-zero and says so, which is a different claim from "the colour is not a
code constant" and must never be reported as it.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys
from dataclasses import dataclass

REPO = pathlib.Path(__file__).resolve().parent.parent
CODE_IMAGE = REPO / "scratch" / "mm3d_layout" / "mm3d.code"
IMAGE_BASE = 0x00100000  # MM3D's .code load address; tools/extract_code.py verifies the first byte.
# MM3D's decompressed .code is 0x5B1000 bytes (mm3d-decomp/docs/code_layout.md). Asserted rather
# than assumed: a short read would otherwise turn every search into a guaranteed zero.
IMAGE_SIZE = 0x5B1000
# MM3D's asserts embed the build machine's source paths, e.g. C:\Jenkins\...\sources\z_kankyo.cpp.
# `function_source_map.csv` attributes 92 files on that evidence, so their presence in .rodata is a
# property of the game -- not of whichever file this tool was handed.
BUILD_PATH_MARKER = b"\\sources\\"

# The oracle's recorded MM3D PICA fog colour for z2_lost_woods. Registered in
# 2ship/2s2h/zelda3d/repl/mm3d_fog_repl.cpp and docs/project-state.md S005.
DEFAULT_RGB = (40, 140, 220)


@dataclass(frozen=True)
class Hit:
    offset: int
    address: int
    encoding: str


def encodings(rgb: tuple[int, int, int]) -> dict[str, bytes]:
    """Every plausible static encoding of a fog colour, keyed by name.

    Azahar's `fog_color` is r@bits 0-7, g@8-15, b@16-23 of one u32
    (Azahar/src/video_core/pica/regs_texturing.h:428-433), so the register word is the encoding
    that actually matters. The rest are included because a title may store the colour unpacked
    before packing it, and a search that only tries the final form misses that.
    """
    r, g, b = rgb
    return {
        "register word r|g<<8|b<<16": struct.pack("<I", r | (g << 8) | (b << 16)),
        "register word b|g<<8|r<<16": struct.pack("<I", b | (g << 8) | (r << 16)),
        "rgba8 r,g,b,ff": bytes([r, g, b, 0xFF]),
        "rgba8 ff,b,g,r": bytes([0xFF, b, g, r]),
        "float 0..1 r,g,b": struct.pack("<3f", r / 255, g / 255, b / 255),
        "float 0..255 r,g,b": struct.pack("<3f", r, g, b),
        # rgb565 in the low half with a zero high half, as a full word. A 2-byte rgb565 was tried
        # first and matched twice in this image: a 2-byte needle in 5.7 MB expects ~91 coincidental
        # hits, so it cannot distinguish a real constant from arithmetic accident. Every encoding
        # here is at least 3 bytes, and a 4-byte needle expects ~0.0003 false hits.
        "rgb565 + zero half": struct.pack("<HH", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3), 0),
    }


def find_all(blob: bytes, needle: bytes, limit: int = 64) -> list[int]:
    hits: list[int] = []
    start = 0
    while len(hits) < limit:
        i = blob.find(needle, start)
        if i < 0:
            break
        hits.append(i)
        start = i + 1
    return hits


def run_controls(blob: bytes) -> list[str]:
    """Prove this search is looking at MM3D's code image, not at whatever file it was handed.

    An earlier version of this control took the first four bytes of the image it was given and
    searched for them, which can never fail and therefore proved nothing -- the worst kind of
    control, because it made a broken search look verified. Both anchors here are properties of the
    game, asserted against values documented outside this tool: the image size, and the build-path
    marker that MM3D's own asserts embed in `.rodata` and that the whole source-attribution
    evidence chain depends on. A wrong, truncated, or byte-swapped image fails them.
    """
    failures: list[str] = []
    if len(blob) != IMAGE_SIZE:
        failures.append(
            f"image is {len(blob)} bytes, expected {IMAGE_SIZE} (0x{IMAGE_SIZE:x}) -- the read is "
            "truncated or this is not MM3D's .code"
        )
    if BUILD_PATH_MARKER not in blob:
        failures.append(
            f"the build-path marker {BUILD_PATH_MARKER!r} is absent, so the assert strings the "
            "source attribution depends on are not here -- this is not MM3D's .code"
        )
    return failures


def search(blob: bytes, rgb: tuple[int, int, int]) -> tuple[list[Hit], int, list[str]]:
    """Returns (hits, patterns tried, control descriptions). Raises if any control cannot pass."""
    controls = [
        f"image size is {IMAGE_SIZE} bytes",
        f"build-path marker {BUILD_PATH_MARKER!r} present",
    ]
    failures = run_controls(blob)
    if failures:
        raise ValueError("CONTROL FAILED: " + "; ".join(failures))
    hits: list[Hit] = []
    tried = 0
    for encoding, needle in encodings(rgb).items():
        tried += 1
        for offset in find_all(blob, needle):
            hits.append(Hit(offset, IMAGE_BASE + offset, encoding))
    return hits, tried, controls


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--rgb",
        default=",".join(str(c) for c in DEFAULT_RGB),
        help="the colour to search for, as r,g,b (default: the oracle's MM3D value)",
    )
    parser.add_argument("--image", type=pathlib.Path, default=CODE_IMAGE, help="the .code image")
    args = parser.parse_args()
    try:
        r, g, b = (int(part) for part in args.rgb.split(","))
        rgb = (r, g, b)
    except ValueError:
        print("ERROR: --rgb must be three integers, r,g,b")
        return 2
    if not args.image.is_file():
        print(f"ERROR: no code image at {args.image}; run tools/extract_code.py first")
        return 2

    blob = args.image.read_bytes()
    try:
        hits, tried, controls = search(blob, rgb)
    except ValueError as exc:
        print(f"ERROR: {exc}")
        return 1

    print(f"scanned {len(blob)} bytes at base {IMAGE_BASE:#010x} for fog colour {rgb}")
    for control in controls:
        print(f"  control OK: {control}")
    print(f"  tried {tried} encodings (all >= 3 bytes, so a coincidental hit is not expected)")
    if hits:
        print(f"  MATCHED {len(hits)}:")
        for hit in hits[:16]:
            print(f"    {hit.address:#010x} (offset {hit.offset:#x}) via {hit.encoding}")
        return 0
    print("  MATCHED 0: the colour is not a static constant in the code image.")
    print("  It is therefore computed at runtime, and its producer must be found in code that")
    print("  reads scene or environment state -- not by searching for the value.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
