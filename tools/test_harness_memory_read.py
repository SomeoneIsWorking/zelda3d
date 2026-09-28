#!/usr/bin/env python3
"""The memory-read owner must refuse to hand back a read that cannot be real.

These are the two failures that cost the fragment-lighting investigation a refutation of a correctly
recorded finding, and they are pinned here so neither can recur silently in a new probe:

* reading before the title demo has run, so the heap is empty and a populated structure reads as zero;
* treating a declined range (end of a memory segment) as though it were zero data.

`read_memory` needs a harness, so the transfer and the gate are tested through a tiny fake that
reproduces exactly those two shapes.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from harness_memory_read import (  # noqa: E402
    DEFAULT_CHUNK,
    WARM_FRAMES,
    MemoryReadUnusable,
    is_all_zero,
    read_memory,
    read_region,
    warm,
)


class FakeHarness:
    """Stands in for the Azahar harness: records sends, and writes a file for each dumprange.

    `payload` is what the next dumprange produces. `None` means the harness declines the range.
    """

    def __init__(self, payload: bytes | None, boot_ok: bool = True) -> None:
        self.payload = payload
        self.boot_ok = boot_ok
        self.sent: list[str] = []

    def send(self, command: str, per_line_timeout: float | None = None) -> str:
        self.sent.append(command)
        if command.startswith("soh_boot"):
            return "ok" if self.boot_ok else "err boot"
        if command.startswith("run "):
            return f"ok {command}"
        if command == "playstate":
            return "playstate ok"
        if command.startswith("dumprange "):
            _, va, size, path = command.split()
            if self.payload is None:
                return "err dumprange: unmapped"
            Path(path).write_bytes(self.payload[: int(size, 16)])
            return "ok"
        return "ok"


class MemoryReadOwnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.path = Path(self.dir.name) / "read.bin"

    def test_warm_runs_the_title_demo_before_any_read(self) -> None:
        harness = FakeHarness(b"\x01" * 16)
        warm(harness)
        run_index = next(i for i, c in enumerate(harness.sent) if c.startswith("run "))
        self.assertGreaterEqual(run_index, 0)
        self.assertIn(f"run {WARM_FRAMES}", harness.sent)
        # The frame count is the reference tool's validated value, not a guess; pin it so a "tune it
        # down for speed" change has to be deliberate.
        self.assertEqual(WARM_FRAMES, 400)

    def test_failed_boot_raises_rather_than_yielding_worthless_reads(self) -> None:
        with self.assertRaises(MemoryReadUnusable):
            warm(FakeHarness(b"\x01", boot_ok=False))

    def test_real_read_returns_bytes(self) -> None:
        payload = bytes(range(64))
        got = read_memory(FakeHarness(payload), 0x1000, 64, self.path)
        self.assertEqual(got, payload)

    def test_all_zero_read_raises_instead_of_looking_like_data(self) -> None:
        # THE failure. A populated structure read as all zeros, and a probe concluded it was empty.
        with self.assertRaises(MemoryReadUnusable) as caught:
            read_memory(FakeHarness(b"\x00" * 64), 0x1000, 64, self.path)
        self.assertIn("has not been run", str(caught.exception))

    def test_partially_zero_read_is_data_and_must_pass(self) -> None:
        # Zeroed material records, empty slot planes and cleared mode blocks are all real, so a
        # mostly-zero read must NOT be gated -- only a uniformly zero one is a bug signal.
        payload = b"\x00" * 62 + b"\x01\x02"
        self.assertEqual(read_memory(FakeHarness(payload), 0x1000, 64, self.path), payload)
        self.assertEqual(is_all_zero(payload), False)

    def test_declined_range_is_reported_as_end_of_region_not_zero(self) -> None:
        # A declined range is information; an all-zero read is a bug. read_region must not blur them.
        self.assertIsNone(read_region(FakeHarness(None), 0x1000, 64, self.path))

    def test_reasons_are_distinct_and_structured(self) -> None:
        # Callers branch on `reason`, never on message text, so the three outcomes stay separable.
        with self.assertRaises(MemoryReadUnusable) as unmapped:
            read_memory(FakeHarness(None), 0x1000, 64, self.path)
        self.assertEqual(unmapped.exception.reason, "unmapped")
        with self.assertRaises(MemoryReadUnusable) as short:
            read_memory(FakeHarness(b"\x01\x02"), 0x1000, 64, self.path)
        self.assertEqual(short.exception.reason, "short")
        with self.assertRaises(MemoryReadUnusable) as empty:
            read_memory(FakeHarness(b"\x00" * 64), 0x1000, 64, self.path)
        self.assertEqual(empty.exception.reason, "empty")

    def test_declined_range_never_silently_yields_zeros(self) -> None:
        harness = FakeHarness(None)
        self.assertIsNone(read_region(harness, 0x1000, 64, self.path))
        self.assertIsNone(read_region(harness, 0x2000, 64, self.path, allow_zero=True))

    def test_short_transfer_is_a_bug_not_end_of_region(self) -> None:
        # Fewer bytes than asked for is a broken read, and must raise rather than be read as a short
        # region -- otherwise a caller trims its loop on a real fault.
        # A caller walking a range would stop its loop on `None`; a short transfer must therefore RAISE,
        # or the loop quietly stops early and the rest of the region is never examined.
        harness = FakeHarness(b"\x01\x02\x03\x04")
        with self.assertRaises(MemoryReadUnusable) as caught:
            read_region(harness, 0x1000, 64, self.path)
        self.assertEqual(caught.exception.reason, "short")

    def test_allow_zero_is_an_explicit_opt_in(self) -> None:
        harness = FakeHarness(b"\x00" * 64)
        self.assertEqual(len(read_region(harness, 0x1000, 64, self.path, allow_zero=True)), 64)
        with self.assertRaises(MemoryReadUnusable):
            read_region(harness, 0x1000, 64, self.path)

    def test_chunk_size_is_small_enough_for_the_sparse_segments(self) -> None:
        # Larger dumprange requests killed the harness outright, losing the whole run.
        self.assertLessEqual(DEFAULT_CHUNK, 0x10000)


if __name__ == "__main__":
    unittest.main()
