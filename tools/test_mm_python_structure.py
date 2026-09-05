#!/usr/bin/env python3
"""Mechanical ownership checks for the MM Python runtime and phase-tour tools."""

from __future__ import annotations

import ast
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from clang_verifier.source_selection import PYTHON_SUFFIXES, repository_files


def stale_runtime_imports(tools: Path) -> list[str]:
    """Inspect project-owned tools, not externally owned discovery links."""
    sources = [
        path
        for path in repository_files(tools.parent, PYTHON_SUFFIXES)
        if path.parent == tools
    ]
    if not sources:
        raise RuntimeError(f"no first-party Python tools found in {tools}")
    stale_imports: list[str] = []
    for path in sources:
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        for node in ast.walk(tree):
            if isinstance(node, ast.ImportFrom) and node.module == "mm_runtime":
                stale_imports.append(f"{path.name}:{node.lineno}")
            if isinstance(node, ast.Import) and any(
                alias.name == "mm_runtime" for alias in node.names
            ):
                stale_imports.append(f"{path.name}:{node.lineno}")
    return stale_imports


class MMPythonStructureTests(unittest.TestCase):
    def test_forwarding_runtime_facade_is_deleted_and_unreferenced(self) -> None:
        self.assertFalse((TOOLS / "mm_runtime.py").exists())
        self.assertEqual(stale_runtime_imports(TOOLS), [])

    def test_source_census_detects_local_imports_without_a_shared_checkout(self) -> None:
        scratch = TOOLS.parent / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            tools = root / "tools"
            tools.mkdir()
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            source = tools / "local.py"
            source.write_text("import mm_runtime\nfrom mm_runtime import MMRuntime\n")
            self.assertEqual(
                stale_runtime_imports(tools), ["local.py:1", "local.py:2"]
            )
            source.write_text("import mm_runtime_launcher\n")
            self.assertEqual(stale_runtime_imports(tools), [])
            source.write_text("this is not valid Python!\n")
            with self.assertRaises(SyntaxError):
                stale_runtime_imports(tools)
            source.unlink()
            with self.assertRaisesRegex(RuntimeError, "no first-party Python tools"):
                stale_runtime_imports(tools)

    def test_phase_tour_entry_point_is_cli_composition_only(self) -> None:
        source = (TOOLS / "mm_phase_tour.py").read_text(encoding="utf-8")
        tree = ast.parse(source)
        functions = {
            node.name for node in tree.body if isinstance(node, ast.FunctionDef)
        }
        self.assertEqual(functions, {"build_parser", "main"})
        owner_reexports = [
            node.module
            for node in tree.body
            if isinstance(node, ast.ImportFrom)
            and node.module is not None
            and node.module.startswith("mm_")
        ]
        self.assertEqual(owner_reexports, [])
        for implementation_token in (
            "FifoRpcClient(",
            "MMRuntime(",
            ".write_text(",
            "shutil.copyfile(",
            "time.sleep(",
        ):
            self.assertNotIn(implementation_token, source)

    def test_phase_owners_remain_below_source_ceiling(self) -> None:
        for name in (
            "mm_phase_tour.py",
            "mm_phase_session.py",
            "mm_phase_artifacts.py",
            "mm_phase_orchestration.py",
        ):
            with self.subTest(name=name):
                lines = (TOOLS / name).read_text(encoding="utf-8").count("\n") + 1
                self.assertLessEqual(lines, 1200)


if __name__ == "__main__":
    unittest.main()
