"""Tests for repository clang-tidy configuration validation."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

from clang_verifier import VerificationError
from clang_verifier.tidy_config_check import (
    repository_tidy_configs,
    validate_tidy_config,
)

REPO = Path(__file__).resolve().parents[3]


def completed(*, returncode: int = 0, stdout: str = "Checks: ''\n", stderr: str = ""):
    return subprocess.CompletedProcess(
        args=["clang-tidy"], returncode=returncode, stdout=stdout, stderr=stderr
    )


class TidyConfigCheckTests(unittest.TestCase):
    def test_discovers_tracked_and_nonignored_untracked_configs(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            tracked = root / ".clang-tidy"
            untracked = root / "engine/.clang-tidy"
            ignored = root / "ignored/.clang-tidy"
            tracked.write_text("Checks: ''\n")
            untracked.parent.mkdir()
            untracked.write_text("Checks: ''\n")
            ignored.parent.mkdir()
            ignored.write_text("Checks: ''\n")
            (root / ".gitignore").write_text("ignored/\n")
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "add", ".clang-tidy"], check=True)

            configs = repository_tidy_configs(root)

        self.assertEqual(configs, [tracked, untracked])

    def test_clean_parser_result_passes(self) -> None:
        commands = []

        def runner(command, **_kwargs):
            commands.append(command)
            return completed()

        validate_tidy_config(REPO, REPO / ".clang-tidy", "clang-tidy", runner=runner)

        self.assertIn("--verify-config", commands[0])
        self.assertIn("--dump-config", commands[0])

    def test_stderr_diagnostic_fails_even_when_exit_is_zero(self) -> None:
        def runner(_command, **_kwargs):
            return completed(stderr="error: invalid configuration value")

        with self.assertRaisesRegex(
            VerificationError, "reported config diagnostics despite exit 0"
        ):
            validate_tidy_config(
                REPO, REPO / ".clang-tidy", "clang-tidy", runner=runner
            )

    def test_nonzero_exit_fails(self) -> None:
        def runner(_command, **_kwargs):
            return completed(returncode=1, stderr="unknown check")

        with self.assertRaisesRegex(VerificationError, "exited 1"):
            validate_tidy_config(
                REPO, REPO / ".clang-tidy", "clang-tidy", runner=runner
            )

    def test_empty_dump_fails(self) -> None:
        def runner(_command, **_kwargs):
            return completed(stdout="")

        with self.assertRaisesRegex(VerificationError, "no parsed configuration dump"):
            validate_tidy_config(
                REPO, REPO / ".clang-tidy", "clang-tidy", runner=runner
            )


if __name__ == "__main__":
    unittest.main()
