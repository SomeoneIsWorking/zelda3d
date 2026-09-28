#!/usr/bin/env python3
"""The RAM scan must refuse to conclude from an unpopulated read, and must admit a weak signature.

Both of these were learned the expensive way on this scan. Its first version reported "no candidate"
from a region that read all zeros, because it never let the title demo run; that is a broken
instrument, not a negative result, and the two look identical from the output. Its second version ran
correctly and found 4500 candidates for the constructor's two marker bytes, so the signature -- not the
read -- was the limit. `verdict()` is pure so both are pinned here without a ROM or a harness.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lit_object_scan import verdict  # noqa: E402


class LitObjectScanVerdictTests(unittest.TestCase):
    def test_empty_read_refuses_to_conclude(self) -> None:
        # The false negative this exists to prevent: 0 non-zero in 32 MB is an unpopulated region.
        code, text = verdict(read=32 * 1024 * 1024, nonzero=0, candidates=[], single=0)
        self.assertEqual(code, 2)
        self.assertIn("UNUSABLE", text)
        self.assertIn("run 400", text)

    def test_zero_byte_read_refuses_to_conclude(self) -> None:
        code, _ = verdict(read=0, nonzero=0, candidates=[], single=0)
        self.assertEqual(code, 2)

    def test_sparse_read_refuses_to_conclude(self) -> None:
        # 0.05% non-zero is still an empty page for this purpose.
        self.assertEqual(verdict(read=1_000_000, nonzero=50, candidates=[], single=0)[0], 2)

    def test_populated_read_with_no_candidate_is_a_real_negative(self) -> None:
        code, text = verdict(read=1_000_000, nonzero=200_000, candidates=[], single=0)
        self.assertEqual(code, 1)
        self.assertIn("real negative", text)
        # And it must scope the negative to the SIGNATURE, not to the object.
        self.assertIn("not for the object", text)

    def test_unique_candidate_is_the_only_claim_of_a_match(self) -> None:
        code, text = verdict(read=1_000_000, nonzero=200_000, candidates=[0x8000000], single=5)
        self.assertEqual(code, 0)
        self.assertIn("unique", text)

    def test_many_candidates_are_reported_as_too_weak(self) -> None:
        # The measured case: 4500 candidates from the constructor's two marker bytes.
        code, text = verdict(read=32 * 1024 * 1024, nonzero=6_782_353, candidates=[1] * 4500, single=115_182)
        self.assertEqual(code, 1)
        self.assertIn("NOT unique", text)
        self.assertIn("115182", text)

    def test_density_boundary_is_inclusive_of_real_data(self) -> None:
        # 0.1% exactly is the floor; just above it must be allowed to reason.
        self.assertEqual(verdict(read=10_000, nonzero=10, candidates=[], single=0)[0], 1)
        self.assertEqual(verdict(read=10_000, nonzero=9, candidates=[], single=0)[0], 2)


if __name__ == "__main__":
    unittest.main()
