"""Tests for evidence-backed override verification."""

from __future__ import annotations

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

from mm_animmap_paths import REPO
from mm_animmap_verify import verify_overrides


class OverrideVerificationTests(unittest.TestCase):
    def test_missing_n64_archive_refuses_to_claim_verification(self) -> None:
        scratch = Path(REPO) / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            missing = Path(raw) / "missing.o2r"
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = verify_overrides(str(missing))

        self.assertEqual(result, 2)
        self.assertIn("VERIFY: REFUSING", output.getvalue())
        self.assertIn("Checked 0", output.getvalue())


if __name__ == "__main__":
    unittest.main()
