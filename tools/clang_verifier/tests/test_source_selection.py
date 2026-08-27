"""Tests for first-party source classification."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

from clang_verifier.source_selection import (
    STRUCTURE_SUFFIXES,
    is_first_party,
    repository_files,
)

REPO = Path(__file__).resolve().parents[3]


class SourceSelectionTests(unittest.TestCase):
    def test_vendor_and_generated_trees_are_excluded(self) -> None:
        self.assertFalse(is_first_party("2ship/src/code/z_actor.c"))
        self.assertFalse(is_first_party("Shipwright/ZAPDTR/ZAPD/main.cpp"))
        self.assertFalse(is_first_party("Shipwright/soh/assets/generated.c"))

    def test_product_sources_are_first_party(self) -> None:
        self.assertTrue(is_first_party("2ship/2s2h/BenPort.cpp"))
        self.assertTrue(is_first_party("Shipwright/soh/src/zelda3d/core/zelda3d.c"))
        self.assertTrue(is_first_party("tools/verify_clang.py"))

    def test_generated_and_vendored_python_are_excluded(self) -> None:
        self.assertFalse(is_first_party("generated/bindings.py"))
        self.assertFalse(is_first_party("Shipwright/ZAPDTR/tools/release.py"))
        self.assertFalse(is_first_party("Shipwright/zelda3d_shared/thirdparty/tool.py"))

    def test_repository_files_ignores_unstaged_deleted_tracked_files(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            deleted = root / "deleted.py"
            deleted.write_text("print('tracked')\n")
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "add", "deleted.py"], check=True)
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
            deleted.unlink()

            files = repository_files(root, STRUCTURE_SUFFIXES)

        self.assertEqual(files, [])

    def test_repository_files_ignores_source_symlink_entry_points(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            fixture = Path(raw)
            root = fixture / "repo"
            root.mkdir()
            authority = fixture / "authority.py"
            authority.write_text("print('shared authority')\n")
            (root / "entry.py").symlink_to(authority)
            subprocess.run(["git", "init", "-q", str(root)], check=True)

            files = repository_files(root, STRUCTURE_SUFFIXES)

        self.assertEqual(files, [])
