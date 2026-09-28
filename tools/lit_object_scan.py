#!/usr/bin/env python3
#!/usr/bin/env python3
"""Find the 3DS fragment-lighting configuration object live, by its constructor's signature.

`FUN_004c6264` is the object's constructor: it zeroes four 8-byte slot planes and the mode block, and
sets ONLY `+0x18A` and `+0x18D` to 1. `FUN_003f9b5c`, whose `arg1` IS the 0x4C8-byte object, has zero ARM
BL callers, so the object is not built in the code image -- it arrives from outside. The per-material
record at `CmbRenderer + 0x400 + i*0x4C8` is NOT it: its `+0x180..0x1C0` region reads zero for every
record measured, so the mode/slot-enable bytes are not stored there.

So: search RAM for the two marker bytes. A hit is a candidate base `b` with `blob[b+0x18A] == 1` and
`blob[b+0x18D] == 1`. The scan prints how much it read and how many candidates it found, and it also
prints the count for a CONTROL signature (`+0x18A == 1` alone), because a single byte pattern will hit
all over a heap and only the conjunction is meaningful. A real object should be rare; a control that
hits thousands of times says the conjunction is still too weak.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_process import spawn  # noqa: E402
from lit_object_dump import boot  # noqa: E402

CHUNK = 0x40000  # 256 KiB per dumprange, so a failure is a chunk boundary and not a lost hour
BASE = 0x08000000
LIMIT = 0x0A000000
MARKER_A = 0x18A
MARKER_B = 0x18D
OUT = REPO / "scratch" / "lit_scan.bin"


def verdict(read: int, nonzero: int, candidates: list[int], single: int) -> tuple[int, str]:
    """Decide what a scan is allowed to claim. Pure, so both outcomes are testable without a ROM.

    The density gate is the part that matters. A first version of this scan reported "no candidate"
    from a region that read ALL ZEROS -- 0 non-zero bytes in 32 MB -- because it never let the title
    demo run, so the fragment path had not produced a configuration. A verdict drawn from an
    unpopulated read is a broken instrument, not a negative result, and the two are indistinguishable
    unless the tool checks. So an empty read refuses to conclude, and says why.
    """
    density = nonzero / read if read else 0.0
    if read == 0 or density < 0.001:
        return 2, (f"THE READ IS UNUSABLE ({nonzero}/{read} non-zero), so NO conclusion about the "
                    f"object follows. Warm the harness with `run 400` first -- the title demo has to be "
                    f"running for the fragment path to have produced a configuration. Reporting 'no "
                    f"candidate' here would be a false negative wearing a verdict's clothes.")
    if not candidates:
        return 1, ("no candidate met the signature, and the read was populated, so that is a real "
                   "negative for this signature (not for the object).")
    if len(candidates) == 1:
        return 0, "a unique candidate; dump it and check the mode bytes against the recorded fixture."
    return 1, (f"NOT unique: {len(candidates)} candidates, so the signature is too weak to name the "
               f"object. The single-byte control hit {single} times.")


def main() -> int:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    harness = spawn()
    read = 0
    nonzero = 0
    both: list[int] = []
    single = 0
    try:
        boot(harness)
        # The title demo has to be RUNNING for the fragment path to have produced a configuration at
        # all. My first scan skipped this and read a still-empty heap: 0 non-zero bytes in 32 MB, and
        # a confidently wrong "no candidate". Same remedy as lit_object_dump.py, same reason.
        harness.send("run 400", per_line_timeout=300.0)
        print("[lit_object_scan] " + (harness.send("playstate") or "").strip())
        va = BASE
        while va < LIMIT:
            size = min(CHUNK, LIMIT - va)
            if OUT.exists():
                OUT.unlink()
            harness.send(f"dumprange 0x{va:08x} 0x{size:x} {OUT}")
            if not OUT.exists() or OUT.stat().st_size != size:
                print(f"  stopped at 0x{va:08x}: dumprange returned nothing (region ends here?)")
                break
            blob = OUT.read_bytes()
            read += len(blob)
            nonzero += sum(1 for byte in blob if byte)
            for i in range(0, len(blob) - MARKER_B):
                if blob[i + MARKER_A] == 1:
                    single += 1
                    if blob[i + MARKER_B] == 1:
                        both.append(BASE + i)
            va += size
        print(f"scanned 0x{BASE:08x}..0x{BASE + read:08x} ({read} bytes)")
        print(f"non-zero density: {nonzero}/{read} = {nonzero / read if read else 0.0:.4f}")
        code, text = verdict(read, nonzero, both, single)
        print(f"VERDICT: {text}")
        if code == 2:
            return code
        print(f"candidates with +0x{MARKER_A:X}==1 AND +0x{MARKER_B:X}==1: {len(both)}")
        for addr in both[:16]:
            print(f"  0x{addr:08x}")
        print(f"CONTROL (+0x{MARKER_A:X}==1 alone): {single}")
    finally:
        getattr(harness, "close", lambda: None)()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
