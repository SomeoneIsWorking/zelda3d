#!/usr/bin/env python3
"""Positive and negative tests for the shared repo environment grammar."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import mm_animmap_paths
from mm_runtime_launch import RuntimeLaunchProvisioner
from mm_runtime_paths import RuntimePaths
from repo_environment import RepoEnvironmentError, apply_repo_environment

SCRATCH_TESTS = REPO / "scratch" / "tests"
SCRATCH_TESTS.mkdir(parents=True, exist_ok=True)


def _runtime_paths(repo: Path) -> RuntimePaths:
    runtime = REPO / "scratch" / "tests" / "repo_environment_runtime"
    return RuntimePaths(
        repo=repo,
        runtime_dir=runtime,
        binary=repo / "bin" / "zelda3d",
        game_dir=repo / "mm",
        display=":94",
        input_fifo=runtime / "input.fifo",
        repl_fifo=runtime / "repl.fifo",
        log=runtime / "run.log",
        xvfb_log=runtime / "xvfb.log",
        manifest=runtime / "runtime.json",
        pid_file=runtime / "runtime.pid",
        lock=runtime / "runtime.lock",
    )


class RepoEnvironmentTests(unittest.TestCase):
    def test_assignments_are_parsed_without_overriding_caller_values(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            (repo / ".env").write_text(
                "# comment\nexport FROM_FILE='quoted value'\nCALLER=from-file\n"
            )
            environment = {"CALLER": "from-process"}

            apply_repo_environment(repo, environment)

        self.assertEqual(environment["FROM_FILE"], "quoted value")
        self.assertEqual(environment["CALLER"], "from-process")

    def test_unsupported_shell_syntax_is_rejected_instead_of_executed(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            (repo / ".env").write_text("source another-file\n")

            with self.assertRaisesRegex(RepoEnvironmentError, "line 1"):
                apply_repo_environment(repo, {})

    def test_mm_runtime_uses_shared_caller_precedence(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            (repo / ".env").write_text("ZELDA3D_MM_ROM=from-file\n")
            provisioner = RuntimeLaunchProvisioner(_runtime_paths(repo))

            environment = provisioner.environment(
                None, None, environ={"ZELDA3D_MM_ROM": "from-process"}
            )

        self.assertEqual(environment["ZELDA3D_MM_ROM"], "from-process")

    def test_mm_animmap_uses_shared_parser(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            (repo / ".env").write_text("MM_ANIMMAP_FIXTURE='shared parser'\n")
            with (
                mock.patch.object(mm_animmap_paths, "REPO", str(repo)),
                mock.patch.dict(os.environ, {}, clear=True),
            ):
                mm_animmap_paths.load_env()
                self.assertEqual(os.environ["MM_ANIMMAP_FIXTURE"], "shared parser")


if __name__ == "__main__":
    unittest.main()
