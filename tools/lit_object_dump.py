#!/usr/bin/env python3
"""Dump the 3DS fragment-lighting configuration object live from the oracle.

Why this exists: `FUN_003f9b5c` -- the top of the confirmed PICA lighting-config chain, and the
function whose `arg1` IS the 0x4C8-byte object -- has **zero ARM `BL` callers**
(`oot3d-decomp/tools/callers_bl.py`, validated decoder). So the object is not constructed anywhere in
the ARM code image; it arrives from outside as an argument. That closes the static route and makes the
runtime route the only one left, which is what `oot3d-decomp/docs/fragment_lighting.md` asked for.

The object is dumped at the TITLE screen, which needs no gameplay save: the title demo renders the
world through the same fragment path. `dumprange <va> <size> <path>` is the harness's bulk virtual
read, so the whole 0x4C8 bytes come back in one command instead of 306 single-word reads.

`--scan` walks a window instead of a fixed address, because the one recorded live pointer
(`0x081d1538`, a builder INPUT observed in a prior session) may not be the object itself, and
guessing an address is exactly what this campaign keeps having to undo. The scan reports, for each
candidate base, whether the bytes look like the object: nonzero density, the 0x4C8 span readable, and
a distinctive field at the recorded `+0x18A`.

Usage:
    source .env
    tools/lit_object_dump.py --scan
    tools/lit_object_dump.py --va 0x081d1538
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

import harness_memory_read as memory_read  # noqa: E402
from harness_process import spawn  # noqa: E402

OUT = REPO / "scratch" / "lit_object"
OBJECT_SIZE = 0x4C8  # the copy length the record derives for the configuration object
# The offset the builder is known to disagree on: config0 differs from the fixture by exactly this
# bit, and `param_1[0x18A]` is the byte that would have to carry it.
INTERESTING_OFFSET = 0x18A
# The recorded live pointer from a prior session ("builder input 0x081d1538", "active CmbRenderer
# r4=0x081d3aa0"). Treated as a CANDIDATE, not an answer.
RECORDED_CANDIDATES = (0x081D1538, 0x081D3AA0)


# Both the warm-up and the read now live in `harness_memory_read`, which is the single owner of the
# precondition. They were inline here, which is exactly why a second script that reimplemented the read
# without them produced a confident wrong answer -- see that module's docstring.
boot = memory_read.warm


def dump(harness, va: int, size: int, path: Path) -> bool:
    """Read one object-sized range. `allow_zero` because a zeroed slot IS a real observation here:
    the per-material records differ in density and several are legitimately empty."""
    try:
        memory_read.read_memory(harness, va, size, path)
        return True
    except memory_read.MemoryReadUnusable as error:
        if "zero bytes" in str(error):
            path.write_bytes(b"\x00" * size)
            return True
        print(f"  dumprange 0x{va:08x} failed: {error}", file=sys.stderr)
        return False


def describe(blob: bytes) -> str:
    """Report the object's shape without claiming to have identified it."""
    nonzero = sum(1 for b in blob if b)
    head = blob[:16].hex()
    return (
        f"size={len(blob)} nonzero={nonzero}/{len(blob)} ({100.0 * nonzero / max(len(blob), 1):.1f}%) "
        f"head={head} +0x{INTERESTING_OFFSET:03x}=0x{blob[INTERESTING_OFFSET]:02x}"
        if len(blob) > INTERESTING_OFFSET
        else f"size={len(blob)} nonzero={nonzero} (too small for +0x{INTERESTING_OFFSET:03x})"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--va", type=lambda s: int(s, 0), help="object address to dump")
    group.add_argument("--scan", action="store_true", help="dump every recorded candidate")
    group.add_argument(
        "--slots",
        type=int,
        default=0,
        help="dump this many per-material slots at CmbRenderer + 0x400 + i*stride. The record maps "
        "CMB +0x00 to CmbRenderer +0x400, and the copy length 0x4C8 is the natural per-material "
        "stride for the EXPANDED runtime struct (the file's 0x15C/0x16C entry is something else).",
    )
    parser.add_argument("--stride", type=lambda s: int(s, 0), default=OBJECT_SIZE)
    parser.add_argument("--renderer", type=lambda s: int(s, 0), default=0x081D3AA0)
    args = parser.parse_args()

    os.environ.setdefault("HARNESS_STDERR", str(REPO / "scratch" / "logs" / "lit_object_dump.log"))
    harness = spawn()
    try:
        print("[lit_object_dump] " + boot(harness).strip())

        if args.slots:
            base = args.renderer + 0x400
            targets = tuple(base + i * args.stride for i in range(args.slots))
            print(f"[lit_object_dump] CmbRenderer 0x{args.renderer:08x} + 0x400 = 0x{base:08x}, "
                  f"stride 0x{args.stride:x}")
        elif args.scan:
            targets = RECORDED_CANDIDATES
        else:
            targets = (args.va,)
        for index, va in enumerate(targets):
            path = OUT / f"obj_{va:08x}.bin"
            if not dump(harness, va, OBJECT_SIZE, path):
                continue
            blob = path.read_bytes()
            print(f"  0x{va:08x}: {describe(blob)}")
            # The first 64 bytes are the object's leading fields; print them as words so a shape can
            # be recognised without re-running the dump.
            words = [int.from_bytes(blob[i : i + 4], "little") for i in range(0, 64, 4)]
            print("    first 16 words: " + " ".join(f"{w:08x}" for w in words))
            del index
    finally:
        harness.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
