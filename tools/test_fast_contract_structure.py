#!/usr/bin/env python3
"""Mechanical ownership gate for native Zelda3D renderer contracts."""

from __future__ import annotations

import re
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FAST = REPO / "Shipwright" / "libultraship" / "include" / "fast"
FAST_SOURCE = REPO / "Shipwright" / "libultraship" / "src" / "fast"
FOCUSED_HEADERS = {
    "zelda3d_fog.h",
    "zelda3d_lighting.h",
    "zelda3d_material_overrides.h",
    "zelda3d_model_provider.h",
    "zelda3d_model_types.h",
    "zelda3d_pose.h",
    "zelda3d_submission.h",
}
FOCUSED_IMPLEMENTATIONS = {
    "zelda3d_fog.cpp",
    "zelda3d_instrumentation.cpp",
    "zelda3d_lighting.cpp",
    "zelda3d_material_overrides.cpp",
    "zelda3d_model_provider.cpp",
    "zelda3d_pose.cpp",
    "zelda3d_pose_interpolation.cpp",
    "zelda3d_render_control.cpp",
    "zelda3d_submission.cpp",
}


class FastContractStructureTests(unittest.TestCase):
    def test_compatibility_umbrella_is_removed(self) -> None:
        self.assertFalse((FAST / "zelda3d_gl.h").exists())
        self.assertFalse((FAST_SOURCE / "zelda3d_gl.cpp").exists())

    def test_no_compatibility_umbrella_consumers(self) -> None:
        consumers: set[str] = set()
        result = subprocess.run(
            ["git", "ls-files", "-co", "--exclude-standard"],
            cwd=REPO,
            check=True,
            capture_output=True,
            text=True,
        )
        for relative in result.stdout.splitlines():
            source = REPO / relative
            if source.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"} or not source.is_file():
                continue
            if re.search(
                r'^\s*#\s*include\s*[<"](?:fast/)?zelda3d_gl\.h[>"]',
                source.read_text(errors="replace"),
                re.MULTILINE,
            ):
                consumers.add(relative)
        self.assertEqual(consumers, set())

    def test_focused_contracts_stay_below_source_limit(self) -> None:
        for name in FOCUSED_HEADERS:
            path = FAST / name
            self.assertLessEqual(len(path.read_text().splitlines()), 1200, path)

    def test_fast_host_implementations_are_focused_and_bounded(self) -> None:
        for name in FOCUSED_IMPLEMENTATIONS:
            path = FAST_SOURCE / name
            self.assertTrue(path.is_file(), path)
            self.assertLessEqual(len(path.read_text().splitlines()), 1200, path)


if __name__ == "__main__":
    unittest.main()
