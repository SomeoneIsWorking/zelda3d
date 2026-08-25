"""Tests for source ceilings and legacy no-growth rules."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

from clang_verifier.source_structure import (
    LEGACY_LINE_LIMITS,
    SOURCE_LINE_LIMIT,
    verify_legacy_limit_changes,
    verify_structure,
)

REPO = Path(__file__).resolve().parents[3]


class SourceStructureTests(unittest.TestCase):
    def assert_oversized(self, suffix: str) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            path = Path(raw) / f"oversized{suffix}"
            path.write_text("x\n" * (SOURCE_LINE_LIMIT + 1))
            failures = verify_structure(REPO, [path], legacy_limits={})

        self.assertEqual(len(failures), 1)
        self.assertIn(f"oversized{suffix}: 1201 lines (limit 1200)", failures[0])

    def test_translation_unit_ceiling(self) -> None:
        self.assert_oversized(".cpp")

    def test_header_ceiling(self) -> None:
        self.assert_oversized(".h")

    def test_python_ceiling(self) -> None:
        self.assert_oversized(".py")

    def test_legacy_ceiling_cannot_be_raised(self) -> None:
        path = "tools/soh3d_harness/main.cpp"
        failures = verify_legacy_limit_changes(REPO, {path: 2991}, {path: 2990})
        self.assertEqual(failures, [f"{path}: legacy ceiling increased 2990 -> 2991"])

    def test_new_legacy_ceiling_is_rejected(self) -> None:
        failures = verify_legacy_limit_changes(REPO, {"new_monolith.cpp": 1300}, {})
        self.assertEqual(
            failures,
            [
                "new_monolith.cpp: new legacy ceiling is not allowed; split the file to 1200 lines"
            ],
        )

    def test_main_harness_no_longer_needs_a_legacy_ceiling(self) -> None:
        path = "tools/soh3d_harness/main.cpp"
        self.assertNotIn(path, LEGACY_LINE_LIMITS)
        self.assertLessEqual(len((REPO / path).read_text().splitlines()), 1200)

    def test_zelda3d_core_no_longer_needs_a_legacy_ceiling(self) -> None:
        path = "Shipwright/soh/src/zelda3d/core/zelda3d.c"
        self.assertNotIn(path, LEGACY_LINE_LIMITS)
        self.assertLessEqual(len((REPO / path).read_text().splitlines()), 1200)

    def test_zelda3d_player_no_longer_needs_a_legacy_ceiling(self) -> None:
        path = "Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp"
        self.assertNotIn(path, LEGACY_LINE_LIMITS)
        self.assertLessEqual(len((REPO / path).read_text().splitlines()), 1200)

    def test_newly_split_sources_no_longer_need_legacy_ceilings(self) -> None:
        paths = (
            "Shipwright/libultraship/src/ship/window/gui/rml/SohRmlUi.cpp",
            "Shipwright/soh/soh/Enhancements/randomizer/randomizer.cpp",
        )
        for path in paths:
            with self.subTest(path=path):
                self.assertNotIn(path, LEGACY_LINE_LIMITS)
                self.assertLessEqual(len((REPO / path).read_text().splitlines()), 1200)

    def test_modified_decomp_seam_cannot_grow_while_vendor_remains_excluded(
        self,
    ) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            seam = root / "2ship/src/code/z_actor.c"
            vendor = root / "Shipwright/ZAPDTR/ZAPD/main.cpp"
            seam.parent.mkdir(parents=True)
            vendor.parent.mkdir(parents=True)
            seam.write_text("int actor;\n")
            vendor.write_text("int vendor;\n")
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(root),
                    "-c",
                    "user.name=Verifier Test",
                    "-c",
                    "user.email=verifier@example.invalid",
                    "commit",
                    "-qm",
                    "fixture",
                ],
                check=True,
            )
            seam.write_text("int actor;\nint added;\n")
            vendor.write_text("int vendor;\nint added;\n")

            failures = verify_structure(root, legacy_limits={})

        self.assertEqual(len(failures), 1)
        self.assertIn(
            "2ship/src/code/z_actor.c: modified legacy decomp seam grew 1 -> 2 lines",
            failures[0],
        )
