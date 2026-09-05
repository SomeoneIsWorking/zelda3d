"""Release orchestration preserves the canonical cold-checkout source contract."""

from __future__ import annotations

import unittest
import tempfile
from pathlib import Path
from unittest import mock

from launcher_bootstrap.source_provision import SourceProvisionError
from tools import build_appimage_release as release


class AppImageReleaseTests(unittest.TestCase):
    def test_public_build_sources_are_restored_before_container_commands(self) -> None:
        operations = []
        with (
            mock.patch.object(release.sys, "argv", ["build_appimage_release.py"]),
            mock.patch.object(release.shutil, "which", return_value="podman"),
            mock.patch.object(release, "require_file"),
            mock.patch.object(
                release, "prepare_build_context", return_value=Path("context")
            ),
            mock.patch.object(
                release,
                "ensure_build_sources",
                side_effect=lambda repo: operations.append(repo),
            ),
            mock.patch.object(
                release, "run", side_effect=lambda command: operations.append(command)
            ),
        ):
            self.assertEqual(release.main(), 0)
        self.assertEqual(operations[0], release.ROOT)
        self.assertEqual(operations[1][:2], ["podman", "build"])

    def test_source_provision_failure_prevents_build_or_packaging(self) -> None:
        with (
            mock.patch.object(release.sys, "argv", ["build_appimage_release.py"]),
            mock.patch.object(release.shutil, "which", return_value="podman"),
            mock.patch.object(release, "require_file"),
            mock.patch.object(
                release,
                "ensure_build_sources",
                side_effect=SourceProvisionError("missing pinned source"),
            ),
            mock.patch.object(release, "run") as run,
            self.assertRaisesRegex(SourceProvisionError, "missing pinned source"),
        ):
            release.main()
        run.assert_not_called()

    def test_container_context_copies_only_allowlisted_builder_inputs(self) -> None:
        scratch = release.ROOT / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            repo = Path(raw)
            for relative in release.CONTEXT_INPUTS:
                target = repo / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("authored builder input\n")
            (repo / "game.z64").write_text("must not enter build context\n")
            context = release.prepare_build_context(repo)
            files = {
                path.relative_to(context)
                for path in context.rglob("*")
                if path.is_file()
            }
            self.assertEqual(files, set(release.CONTEXT_INPUTS))
            (context / "game.z64").write_text("unexpected input\n")
            with self.assertRaisesRegex(SystemExit, "unexpected file"):
                release.prepare_build_context(repo)
