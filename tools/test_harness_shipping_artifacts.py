#!/usr/bin/env python3
"""Unit tests for portable shipping-core artifact discovery."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
MODULE = REPO / "tools" / "soh3d_harness" / "shipping_artifacts.cmake"


class ShippingArtifactTests(unittest.TestCase):
    def setUp(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.root = Path(self.temporary.name)
        self.result = self.root / "result.txt"
        self.script = self.root / "discover.cmake"
        self.script.write_text(
            f'include("{MODULE}")\n'
            "zelda3d_find_shipping_artifact(found \"${BUILD_ROOT}\" soh_core \"lib\" \".so\" \"Release\")\n"
            'file(WRITE "${RESULT_FILE}" "${found}")\n',
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_discovery(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                "cmake",
                f"-DBUILD_ROOT={self.root / 'shipping'}",
                f"-DRESULT_FILE={self.result}",
                "-P",
                str(self.script),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def write_artifact(self, directory: str) -> Path:
        artifact = self.root / "shipping" / directory / "libsoh_core.so"
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_text("fixture\n", encoding="utf-8")
        return artifact

    def test_release_configuration_wins_over_debug(self) -> None:
        self.write_artifact("core/Debug")
        expected = self.write_artifact("different-layout/Release")

        completed = self.run_discovery()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(self.result.read_text(encoding="utf-8"), str(expected))

    def test_missing_artifact_is_an_error(self) -> None:
        completed = self.run_discovery()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("needs libsoh_core.so", completed.stderr)

    def test_ambiguous_configuration_is_an_error(self) -> None:
        self.write_artifact("first/Release")
        self.write_artifact("second/Release")

        completed = self.run_discovery()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("found multiple libsoh_core.so artifacts", completed.stderr)


if __name__ == "__main__":
    unittest.main()
