#!/usr/bin/env python3
"""Find the 0x4C8-byte SOURCE that the fragment-lighting object is copied from.

**The question.** `oot3d-decomp/docs/fragment_lighting.md` locates the object -- live runtime state at
`CmbRenderer + 0x400 + index * 0x4C8` -- and localises what is left to a provenance question: the
`0x4C8` bytes arrive by a bulk copy from a separate per-material object in game data, not from the CMB
material. Nothing on the recovered chain writes `+0x18A`, so the byte is source data.

**Why this can be answered without a breakpoint.** The object is the DESTINATION of that copy, so if the
copy is total and verbatim -- and `FUN_00371758` is a pure block copy with no field logic, which is the
whole reason the question is about bytes rather than about an absent writer -- then the source's 0x4C8
bytes are IDENTICAL to the object's, and they are in memory too. So: dump the object, and look for it
elsewhere.

That deliberately assumes nothing about the copy's offset or length, which is where the previous
attempt went wrong: it searched a 32-byte window at an INFERRED `+0x180` and, when that came back
empty, nearly read as a negative about the object. This searches the whole object, so it has no offset
to get wrong.

**Controls, because a 1224-byte match means nothing without one.** A control slice of the same length,
taken from elsewhere in the same image, is searched the same way and must find nothing. And a real
object must be distinctive, so the object is chosen by density: an all-but-empty slot would match
anywhere. Both numbers are printed.

Reads go through `harness_memory_read`, which owns the warm-up and refuses an all-zero read -- the
precondition whose absence produced a refutation of a correctly-recorded finding earlier in this
campaign.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

import harness_memory_read as memory_read  # noqa: E402
from harness_process import spawn  # noqa: E402

RENDERER = 0x081D3AA0
SLOT_BASE = RENDERER + 0x400
STRIDE = 0x4C8
OBJECT_SIZE = 0x4C8
# Slots to try, in order. The object must be distinctive enough to be found at most once, so a slot is
# only used if it is not almost entirely zero.
SLOTS = (0, 1, 2, 3, 4, 5)
MIN_OBJECT_DENSITY = 0.05
# `FUN_00371758` copies 32 bytes, so 32 is the window a source would have to be shared with.
COPY_LEN = 0x20
WINDOW_COUNT = OBJECT_SIZE - COPY_LEN + 1
# A window of mostly repeated bytes is shared by accident; those are not searched.
MIN_WINDOW_DENSITY = 0.10
# "A window with only a handful of partners" is the shape a real source/copy pair would take; above
# that the window is simply common data.
RARE_PARTNERS = 2
CONTROL_SAMPLES = 200
CHUNK = memory_read.DEFAULT_CHUNK
# Every populated region the harness exposes, found by probing: 0x10000000 (6.1% non-zero) and
# 0x14000000 (1.7%) are real and were missed by the first two-region version, so the negative this
# tool reports is only as complete as this list. 0x0A000000, 0x0C000000, 0x12000000, 0x1F000000,
# 0x1FFF0000 and 0x02000000 all read as zero and are excluded.
REGIONS = (
    ("arm11-heap", 0x08000000, 0x0A000000),
    ("linear-heap", 0x00100000, 0x01000000),
    ("region-0x10000000", 0x10000000, 0x12000000),
    ("region-0x14000000", 0x14000000, 0x16000000),
)
OUT = REPO / "scratch" / "lit_source.bin"


def distinctive(blob: bytes, minimum: float) -> bool:
    """Whether a 0x4C8 blob is specific enough to be searched for at all.

    An almost-empty object would match in a dozen places and prove nothing, so those are refused
    rather than reported as ambiguous hits.
    """
    return len(blob) > 0 and (sum(1 for b in blob if b) / len(blob)) >= minimum


def window_verdict(rare: int, control_rare: int, tested: int) -> tuple[int, str]:
    """What the per-offset window search may claim. Pure, so both outcomes are testable.

    Across ~1200 tested windows in a 116 MB image, a window with one or two partners is not evidence
    on its own. The only claim available is RELATIVE: the object's rare-window rate against the same
    statistic for windows sampled elsewhere in the same image.
    """
    rate = rare / tested if tested else 0.0
    control_rate = control_rare / CONTROL_SAMPLES if CONTROL_SAMPLES else 0.0
    text = (f"rare windows: {rare}/{tested} = {rate:.4f}; control {control_rare}/{CONTROL_SAMPLES} = "
            f"{control_rate:.4f}")
    if not rare:
        return 1, text + " -- no window of the object is rare, so none is a source."
    if rate > control_rate:
        return 0, text + " -- richer than the control, so at least one window may be a real source."
    return 1, (text + " -- no richer than the control, so the rare windows are chance and no window of "
                    "this object is identified as a source.")


def verdict(hits: int, controls: int, density: float) -> tuple[int, str]:
    """What a search is allowed to claim. Pure, so both outcomes are testable without a harness."""
    if density < MIN_OBJECT_DENSITY:
        return 2, (f"the object is only {density:.4f} non-zero, so it is not distinctive enough to "
                   f"search for. Refused rather than reported as ambiguous.")
    if hits == 1 and controls == 0:
        return 0, "exactly one match and the control found none: that is the source."
    if hits == 0 and controls == 0:
        return 1, ("no match anywhere, with a clean control -- so the source is NOT resident in the "
                   "scanned regions, or the copy is not total.")
    if hits == 0:
        return 1, f"no match and the control matched {controls} times, so the search is unreliable."
    return 1, f"{hits} matches against {controls} control matches: ambiguous."


def read_image(harness) -> tuple[bytearray, list[tuple[int, int, int]]]:
    """Read every region, keeping each chunk's absolute address AND length.

    The length is not optional bookkeeping. The first version of this lookup tested membership with
    `off <= h < off + OBJECT_SIZE`, which is wrong for any hit in the middle of a 0x10000 chunk -- so
    EVERY hit failed to resolve and was printed as a bare image offset, and the object's own address
    came back labelled "CANDIDATE SOURCE". A location report that mislabels where it found something is
    worse than none.
    """
    image = bytearray()
    spans: list[tuple[int, int, int]] = []
    for _, base, limit in REGIONS:
        va = base
        while va < limit:
            size = min(CHUNK, limit - va)
            blob = memory_read.read_region(harness, va, size, OUT, allow_zero=True)
            if blob is None:
                break
            spans.append((va, len(image), len(blob)))
            image += blob
            va += size
    return image, spans


def locate(spans: list[tuple[int, int, int]], offset: int) -> int | None:
    """Map an image offset back to the absolute address it was read from."""
    for base, off, length in spans:
        if off <= offset < off + length:
            return base + (offset - off)
    return None


def find_all(image: bytes, needle: bytes) -> list[int]:
    hits, at = [], image.find(needle)
    while at != -1 and len(hits) < 64:
        hits.append(at)
        at = image.find(needle, at + 1)
    return hits


def main() -> int:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    harness = spawn()
    try:
        print("[lit_source_hunt] " + memory_read.warm(harness).strip())
        image, spans = read_image(harness)
        print(f"[lit_source_hunt] image: {len(image)} bytes across {len(spans)} chunks")

        # A control slice of the same length, from the middle of the image, searched the same way.
        control_at = max(0, len(image) // 2 - OBJECT_SIZE // 2)
        control = bytes(image[control_at : control_at + OBJECT_SIZE])
        control_hits = len(find_all(image, control)) - 1 if control and any(control) else 0

        for slot in SLOTS:
            va = SLOT_BASE + slot * STRIDE
            try:
                obj = memory_read.read_memory(harness, va, OBJECT_SIZE, OUT)
            except memory_read.MemoryReadUnusable as error:
                print(f"  slot {slot} @0x{va:08x}: unreadable ({error.reason})")
                continue
            density = sum(1 for b in obj if b) / len(obj)
            hits = find_all(image, obj)
            # The object lives in the arm11 heap, which is inside the image, so one of the matches is
            # the object ITSELF. Only matches at some other address are candidate sources, and the
            # verdict is about those.
            addresses = [locate(spans, h) for h in hits]
            elsewhere = [a for a in addresses if a is not None and a != va]
            print(f"  slot {slot} @0x{va:08x}: density={density:.3f} matches={len(hits)} "
                  f"(control {control_hits}) other={len(elsewhere)}")
            if not distinctive(obj, MIN_OBJECT_DENSITY):
                print("      refused: not distinctive enough to name a source")
                continue
            code, text = verdict(len(elsewhere), control_hits, density)
            print(f"      VERDICT: {text}")
            for absolute in addresses:
                if absolute is None:
                    continue
                label = "the object itself" if absolute == va else "CANDIDATE SOURCE"
                print(f"      match at 0x{absolute:08x} ({label})")

            # The whole-object search finding nothing is itself informative, and it RECONCILES the two
            # records: `FUN_00371758` is a 32-BYTE block copy, not a 0x4C8 one. The constructor builds
            # the rest, so only 32 bytes have a source at all. Which 32 is not recorded, so rather
            # than assume an offset -- the mistake that made the previous attempt worthless -- every
            # offset in the object is tried.
            print(f"      stage 2: every {COPY_LEN}-byte window of the object, {WINDOW_COUNT} offsets")
            rare: list[tuple[int, int, list[int]]] = []
            for offset in range(0, OBJECT_SIZE - COPY_LEN + 1):
                window = obj[offset : offset + COPY_LEN]
                if not distinctive(window, MIN_WINDOW_DENSITY):
                    continue
                hits = find_all(image, window)
                partners = [h for h in hits if locate(spans, h) != va]
                if 0 < len(partners) <= RARE_PARTNERS:
                    rare.append((offset, len(partners), [locate(spans, h) for h in partners]))
            for offset, partners, addresses in rare:
                shown = ", ".join(f"0x{a:08x}" for a in addresses[:4] if a is not None)
                print(f"        +0x{offset:03X}: {partners} partner(s) [{shown}]")

            # THE CONTROL, and it is the only reason any of the above means anything. Across
            # WINDOW_COUNT tested windows in a 116 MB image, finding a window with a single partner is
            # not surprising by chance -- so the same statistic is computed for windows sampled from
            # elsewhere in the image, and a candidate is only interesting if it is RARER than that.
            control_rare = 0
            stride = max(1, len(image) // CONTROL_SAMPLES)
            for i in range(CONTROL_SAMPLES):
                start = i * stride
                window = bytes(image[start : start + COPY_LEN])
                if not distinctive(window, MIN_WINDOW_DENSITY):
                    continue
                if 0 < len(find_all(image, window)) - 1 <= RARE_PARTNERS:
                    control_rare += 1
            print(f"      CONTROL: {control_rare}/{CONTROL_SAMPLES} sampled windows also have 1..{RARE_PARTNERS} "
                  f"partner(s); the object has {len(rare)}/{WINDOW_COUNT} such offsets")
            code, text = window_verdict(len(rare), control_rare, WINDOW_COUNT)
            print(f"      VERDICT (windows): {text}")
    finally:
        getattr(harness, "quit", lambda: None)()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
