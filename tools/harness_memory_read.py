"""The ONE way to read 3DS process memory out of the Azahar harness.

**This module exists because the same mistake was made twice in one session, and the second time it
refuted a correctly-recorded project finding.** A probe read a structure's `+0x180..0x1C0` region, got
all zeros, and concluded the fragment-lighting configuration object was not the per-material record --
which `oot3d-decomp/docs/fragment_lighting.md` had recorded as FOUND two days earlier. It was not a
wrong inference; the read had never happened. `lit_object_dump.py` runs the title demo before reading
and the probe did not, so the heap was still empty and every statistic drawn from it was a plausible
wrong number. The same failure had already been diagnosed, and gated, inside the first tool -- and the
gate did not help, because it lived in that tool and the second script never called it.

So the precondition is hoisted out of the callers, and `read_memory` refuses to return an all-zero
read. Two rules, and they are the whole content of this module:

* **`warm()` before reading.** The title demo has to be RUNNING for the fragment path to have produced
  a configuration at all. `run 400` is what the reference tool uses; without it the game has rendered
  nothing and the heap reads as zero.
* **An all-zero read is a bug signal, not data.** A live 3DS heap is never uniformly zero over a
  structure's region, so a read that comes back entirely zero means the address, the warm-up or the
  transfer is wrong. That is worth an exception, because the failure it prevents is a confident wrong
  answer rather than a crash.

A *partially* zero read is legitimate and is returned untouched: zeroed material records, empty slot
planes and cleared mode blocks are all real data.
"""

from __future__ import annotations

from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# How many frames the title demo needs before the fragment path has produced a configuration. This is
# the value `lit_object_dump.py` used and validated; it is the reference, not a guess.
WARM_FRAMES = 400
WARM_TIMEOUT = 300.0

# Chunk size for bulk reads. Larger `dumprange` requests killed the harness outright on the sparse
# memory segments, and a dead harness loses the whole run, so this is set by the most fragile region
# rather than the fastest one.
DEFAULT_CHUNK = 0x10000


class MemoryReadUnusable(RuntimeError):
    """The harness returned a read that cannot be a real one. See the module docstring.

    `reason` is one of "unmapped", "short" or "empty", and it is a FIELD rather than something to
    match on the message text. That distinction is load-bearing: "unmapped" is information (the end
    of a memory segment, which a caller walking a range should stop at) while "short" and "empty" are
    bugs. An earlier version keyed that decision off a substring of the message, so a SHORT transfer
    was silently reported as end-of-region -- a test caught it, which is the only reason it did not
    ship.
    """

    def __init__(self, message: str, reason: str) -> None:
        super().__init__(message)
        self.reason = reason


def is_all_zero(blob: bytes) -> bool:
    """True when a read is uniformly zero -- the signature of 'this never happened'."""
    return not any(blob)


def warm(harness) -> str:
    """Boot the core and run the title demo so the renderer has produced real state.

    Returns the harness's `playstate` reply. Raises if the boot did not complete, because every read
    taken after a failed boot is worthless and would otherwise be reported as a result.
    """
    response = harness.send("soh_boot")
    if not response.startswith("ok"):
        raise MemoryReadUnusable(f"soh_boot failed: {response}", reason="unmapped")
    harness.send(f"run {WARM_FRAMES}", per_line_timeout=WARM_TIMEOUT)
    return harness.send("playstate") or ""


def read_memory(harness, va: int, size: int, path: Path) -> bytes:
    """Bulk-read `size` bytes at `va` into `path` and return them.

    Raises `MemoryReadUnusable` when the harness did not transfer exactly `size` bytes, or when what
    arrived is uniformly zero. A short or absent file is the harness declining the range; an all-zero
    payload is the read having happened too early, before `warm()`.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    harness.send(f"dumprange 0x{va:08x} 0x{size:x} {path}")
    if not path.exists() or path.stat().st_size != size:
        got = path.stat().st_size if path.exists() else 0
        # No file at all is the harness declining the range; a file of the wrong size is a broken
        # transfer. Callers may stop on the first and must not on the second.
        raise MemoryReadUnusable(
            f"dumprange 0x{va:08x} 0x{size:x} transferred {got}/{size} bytes; the range is probably "
            f"unmapped. Callers must treat that as end-of-region, not as zero data.",
            reason="unmapped" if got == 0 else "short",
        )
    blob = path.read_bytes()
    if is_all_zero(blob):
        raise MemoryReadUnusable(
            f"dumprange 0x{va:08x} 0x{size:x} returned {size} zero bytes. A live heap is never "
            f"uniformly zero here, so the title demo has not been run -- call warm(harness) first. "
            f"Returning this as data is how a probe concludes a populated structure is empty.",
            reason="empty",
        )
    return blob


def read_region(harness, va: int, size: int, path: Path, allow_zero: bool = False) -> bytes | None:
    """`read_memory` for callers walking a memory range, where end-of-region is expected.

    Returns `None` where the harness declines the range (end of a segment) and raises where the read
    succeeded but came back unusable, so those two are never confused: the first is information, the
    second is a bug. `allow_zero` exists for the rare case of a legitimately all-zero region the caller
    has already established is real.
    """
    try:
        return read_memory(harness, va, size, path)
    except MemoryReadUnusable as error:
        if error.reason == "unmapped":
            return None
        if allow_zero and error.reason == "empty":
            return path.read_bytes() if path.exists() else b""
        raise
