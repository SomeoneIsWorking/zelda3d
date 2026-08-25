"""Tests for non-mutating baseline-aware formatting checks."""

from __future__ import annotations

import contextlib
import io
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

from clang_verifier import VerificationError
from clang_verifier.format_check import verify_format

REPO = Path(__file__).resolve().parents[3]


class FormatCheckTests(unittest.TestCase):
    def make_git_fixture(self) -> tuple[tempfile.TemporaryDirectory[str], Path, str]:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=scratch)
        root = Path(temporary.name)
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        (root / ".clang-format").write_text("BasedOnStyle: LLVM\n")
        source = root / "fixture.cpp"
        legacy = "int  untouched_legacy_drift;\n"
        source.write_text(legacy)
        subprocess.run(
            ["git", "-C", str(root), "add", ".clang-format", "fixture.cpp"], check=True
        )
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
        return temporary, source, legacy

    def test_failure_is_visible_and_non_mutating(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            source = root / "bad.cpp"
            original = "int  badly_formatted;\n"
            source.write_text(original)
            fake_format = root / "clang-format-falsifier"
            fake_format.write_text("#!/usr/bin/env python3\nraise SystemExit(7)\n")
            fake_format.chmod(fake_format.stat().st_mode | stat.S_IXUSR)

            with (
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaisesRegex(
                    VerificationError, "clang-format failed with exit 7"
                ),
            ):
                verify_format([source], str(fake_format), repo=REPO)
            self.assertEqual(source.read_text(), original)

    def test_untouched_legacy_drift_does_not_fail(self) -> None:
        temporary, source, legacy = self.make_git_fixture()
        with temporary:
            source.write_text(legacy + "int added() { return 1; }\n")
            verify_format(
                [source],
                shutil.which("clang-format") or "clang-format",
                repo=source.parent,
            )
            self.assertEqual(source.read_text(), legacy + "int added() { return 1; }\n")

    def test_dirty_tracked_hunk_fails(self) -> None:
        temporary, source, legacy = self.make_git_fixture()
        with temporary:
            dirty = legacy + "int  added( ){return 1;}\n"
            source.write_text(dirty)
            with (
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaisesRegex(
                    VerificationError, "git clang-format found formatting changes"
                ),
            ):
                verify_format(
                    [source],
                    shutil.which("clang-format") or "clang-format",
                    repo=source.parent,
                )
            self.assertEqual(source.read_text(), dirty)

    def test_dirty_untracked_file_fails_full_check(self) -> None:
        temporary, source, _legacy = self.make_git_fixture()
        with temporary:
            untracked = source.parent / "untracked.cpp"
            dirty = "int  untracked( ){return 1;}\n"
            untracked.write_text(dirty)
            with (
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaisesRegex(VerificationError, "clang-format failed"),
            ):
                verify_format(
                    [untracked],
                    shutil.which("clang-format") or "clang-format",
                    repo=source.parent,
                )
            self.assertEqual(untracked.read_text(), dirty)
