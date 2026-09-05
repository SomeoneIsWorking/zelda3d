"""Falsifiers for the complete asset-free product verification boundary."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

from tools import verify_product


class ProductVerificationTests(unittest.TestCase):
    def test_runtime_commands_load_real_cores_and_refuse_empty_ctest(self) -> None:
        build = verify_product.launcher_build.BuildLayout.for_repo(Path.cwd(), jobs=2)
        commands = verify_product.verification_commands(build)
        self.assertIn("--no-tests=error", commands[1])
        self.assertEqual(commands[2], [str(build.binary), "--probe-cores"])
        self.assertFalse(
            any("run.sh" in item for command in commands for item in command)
        )

    def test_missing_core_refuses_before_any_runtime_command(self) -> None:
        with (
            mock.patch.object(verify_product.sys, "platform", "linux"),
            mock.patch.object(verify_product, "ensure_build_sources"),
            mock.patch.object(verify_product.launcher_build, "ensure_product_build"),
            mock.patch.object(verify_product.Path, "is_file", return_value=False),
            mock.patch.object(verify_product.subprocess, "run") as run,
            self.assertRaisesRegex(RuntimeError, "soh/libsoh_core.so"),
        ):
            verify_product.verify(Path.cwd(), 2)
        run.assert_not_called()

    def test_hosted_lint_checks_committed_changes_against_real_product_database(self) -> None:
        build = verify_product.launcher_build.BuildLayout.for_repo(Path.cwd(), jobs=2)
        command = verify_product.verification_commands(build, "HEAD^")[-1]
        self.assertIn(str(build.build_dir / "compile_commands.json"), command)
        self.assertEqual(command[-2:], ["--base", "HEAD^"])

    def test_first_push_checks_all_sources_instead_of_empty_worktree(self) -> None:
        build = verify_product.launcher_build.BuildLayout.for_repo(Path.cwd(), jobs=2)
        commands = verify_product.verification_commands(build, "0" * 40)
        self.assertEqual(commands[-2][-2:], ["--all", "--format-only"])
        self.assertEqual(commands[-1][-2:], ["--compiled", "--tidy-only"])

    def test_unsupported_complete_platform_refuses_instead_of_passing_seams(
        self,
    ) -> None:
        with (
            mock.patch.object(verify_product.sys, "platform", "win32"),
            self.assertRaisesRegex(RuntimeError, "requires Linux"),
        ):
            verify_product.verify(Path.cwd(), 2)
