#!/usr/bin/env python3
"""A RAM scan for the 3DS fragment-lighting configuration object -- KEPT AS A DOCUMENTED DEAD END.

**Do not read its results as findings. The signature it searches for is refuted by the live data.**

It looked for `+0x18A == 1 AND +0x18D == 1`, on the strength of `FUN_004c6264` being the object's
constructor and setting exactly those two bytes. But the object is already located and live-readable at
`CmbRenderer + 0x400 + index * 0x4C8` (`oot3d-decomp/docs/fragment_lighting.md`, 2026-09-27), and
measured there the bytes are `+0x18A = 0x80, +0x18D = 0x00` on slot 0 and `0x00/0x00` on slots 1-3 --
never `1`. So the signature matches no real object at its known address, and all 7508 hits were false
positives. `+0x18A` is *source data* carried in the object, not a marker the constructor leaves behind,
which is exactly the doc's reading; assuming otherwise is what made this scan pointless.

Its 4500 and 7508 counts, and its stage-2 copy-window result, are therefore void.

**What is worth keeping is the method, and this file is kept for that.** The non-zero-density gate in
`verdict()` exists because a first run reported "no candidate" from **0 non-zero bytes in 32 MB** -- the
title demo had not been run, so the fragment path had produced no configuration and the read was simply
empty. A verdict drawn from an unpopulated read is a broken instrument, not a negative result, and the
two are indistinguishable from the output. That mistake was then made a SECOND time in a separate probe,
which produced a false negative that refuted a correctly-recorded project finding; re-measured with
`run 400`, the region it claimed was zero is populated (32/64 non-zero on slot 0).

So the two things to take from this file:

* **`run 400` is a precondition of every memory read in this campaign.** The title demo has to be
  running for the fragment path to have produced a configuration; without it the heap reads as zero and
  every statistic drawn from it is a plausible wrong number.
* **Gate on density before concluding anything.** `verdict()` refuses below 0.1% non-zero, and
  `window_verdict()` refuses to turn a negative about one offset into a negative about the object.

For the real question -- the PROVENANCE of the object's 0x4C8 source -- use
`oot3d-decomp/docs/fragment_lighting.md`, which locates the object, and read that row before starting
anything new.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_process import spawn  # noqa: E402
from lit_object_dump import boot  # noqa: E402

# 64 KiB per dumprange. Larger probes killed the harness outright on the sparse segments, and a dead
# harness loses the whole run, so the chunk size is set by the most fragile region rather than the
# fastest one.
CHUNK = 0x10000
MARKER_A = 0x18A
MARKER_B = 0x18D
# Where `FUN_00371758` copies, relative to the object base, and how much.
COPY_WINDOW = 0x180
COPY_LEN = 0x20
CONTROL_SAMPLES = 200
# A source/copy PAIR is one specific block shared with a small number of other places, so its
# occurrence count is SMALL. The first version of this stage counted any duplicate at all, and the top
# hits were windows occurring 1,102,270 times -- degenerate repetitive data in the asset region, not a
# pair. Restricting to a small count, and to windows not made of a handful of repeated bytes, is what
# makes the number comparable to the control.
COPY_MAX_OCCURRENCES = 64
COPY_MIN_DISTINCT_BYTES = 8
REGIONS = (
    ("arm11-heap", 0x08000000, 0x0A000000),
    ("linear-heap", 0x00100000, 0x01000000),
)
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


def window_verdict(shared: int, total: int, control_shared: int, control_total: int) -> tuple[int, str]:
    """Decide what the copy-window stage may claim. Pure, so both outcomes are testable.

    The measured case: 31 of 7508 candidates versus 23 of 200 control windows. Candidates share a
    window at 0.41% and random windows at 11.5%, so candidates duplicate roughly 28x LESS than chance
    rather than more. A genuine source/copy pair must duplicate MORE than chance -- that is the whole
    premise -- so this stage rules out the +0x180 window as the copied one, and says nothing about the
    object beyond that.

    And the premise is conditional: `FUN_00371758` is known to be a pure 32-byte block copy, but the
    offset it copies at is NOT recovered. +0x180 is inferred, so a negative here is a negative about
    +0x180 only. The tool must not let that read as a negative about the object.
    """
    rate = shared / total if total else 0.0
    control_rate = control_shared / control_total if control_total else 0.0
    text = (f"candidates sharing a window: {shared}/{total} = {rate:.4f}; control "
            f"{control_shared}/{control_total} = {control_rate:.4f}")
    if not shared:
        return 1, text + " -- no candidate is a copy. Rules out the +0x180 window, not the object."
    if rate <= control_rate:
        return 1, (text + " -- candidates share LESS than chance, where a source/copy pair must share "
                        "MORE. Rules out the +0x180 window, not the object.")
    if control_shared == 0:
        return 0, text + " -- a rate above a control that never fires, against a populated read."
    return 1, text + " -- INCONCLUSIVE: the control fires too, so the test does not separate."


def scan(harness) -> tuple[bytearray, int, int, list[int], int]:
    """Read every region and return (image, bytes read, non-zero bytes, candidates, single-byte hits).

    Addresses are recorded as absolute, not as offsets into `image`, because the two regions are not
    contiguous and a window search over the concatenated image would then be meaningless.
    """
    image = bytearray()
    read = nonzero = single = 0
    candidates: list[int] = []
    for name, base, limit in REGIONS:
        print(f"[lit_object_scan] region {name} 0x{base:08x}..0x{limit:08x}")
        va = base
        while va < limit:
            size = min(CHUNK, limit - va)
            if OUT.exists():
                OUT.unlink()
            harness.send(f"dumprange 0x{va:08x} 0x{size:x} {OUT}")
            if not OUT.exists() or OUT.stat().st_size != size:
                print(f"  stopped at 0x{va:08x}: dumprange returned nothing (region ends here?)")
                break
            blob = OUT.read_bytes()
            image += blob
            read += len(blob)
            nonzero += sum(1 for byte in blob if byte)
            for i in range(0, len(blob) - MARKER_B):
                if blob[i + MARKER_A] == 1:
                    single += 1
                    if blob[i + MARKER_B] == 1:
                        candidates.append(va + i)
            va += size
    return image, read, nonzero, candidates, single


def main() -> int:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    harness = spawn()
    try:
        boot(harness)
        # The title demo has to be RUNNING for the fragment path to have produced a configuration at
        # all. My first scan skipped this and read a still-empty heap. Same remedy as
        # lit_object_dump.py, same reason.
        harness.send("run 400", per_line_timeout=300.0)
        print("[lit_object_scan] " + (harness.send("playstate") or "").strip())

        image, read, nonzero, both, single = scan(harness)
        print(f"scanned {read} bytes across {len(REGIONS)} region(s)")
        print(f"non-zero density: {nonzero}/{read} = {nonzero / read if read else 0.0:.4f}")
        print(f"candidates with +0x{MARKER_A:X}==1 AND +0x{MARKER_B:X}==1: {len(both)}")
        for addr in both[:8]:
            print(f"  0x{addr:08x}")
        print(f"CONTROL (+0x{MARKER_A:X}==1 alone): {single}")

        code, text = verdict(read, nonzero, both, single)
        print(f"VERDICT (markers): {text}")
        if code == 2:
            return code

        # Stage two: the 32-byte copy window. A real object's window is a verbatim slice of its
        # 0x4C8 source, so it must be shared -- with only a few other places, or it is just data that
        # repeats, which is what this region is full of.
        print(f"\nstage 2: the {COPY_LEN}-byte copy window at +0x{COPY_WINDOW:X}")

        def informative(block: bytes, count: int) -> bool:
            return 1 < count <= COPY_MAX_OCCURRENCES and len(set(block)) >= COPY_MIN_DISTINCT_BYTES

        shared: list[tuple[int, int]] = []
        for addr in both:
            start = addr + COPY_WINDOW
            if start + COPY_LEN > len(image):
                continue
            block = bytes(image[start : start + COPY_LEN])
            count = image.count(block)
            if informative(block, count):
                shared.append((addr, count))
        print(f"candidates whose window is shared by 2..{COPY_MAX_OCCURRENCES} places and is not "
              f"degenerate: {len(shared)}/{len(both)}")
        for addr, count in shared[:12]:
            print(f"  0x{addr:08x}  window occurs {count}x")

        # The control that makes that number mean anything: windows from a spread-out sample of the
        # same image, put through the identical test. Without it, a hit rate is just a rate.
        stride = max(1, len(image) // CONTROL_SAMPLES)
        duplicated_controls = 0
        for i in range(CONTROL_SAMPLES):
            start = i * stride
            block = bytes(image[start : start + COPY_LEN])
            if informative(block, image.count(block)):
                duplicated_controls += 1
        print(f"CONTROL: {duplicated_controls}/{CONTROL_SAMPLES} sampled windows are also shared by "
              f"2..{COPY_MAX_OCCURRENCES} places")
        code, text = window_verdict(len(shared), len(both), duplicated_controls, CONTROL_SAMPLES)
        print(f"VERDICT (copy window): {text}")
        return code
    finally:
        getattr(harness, "quit", lambda: None)()


if __name__ == "__main__":
    raise SystemExit(main())
