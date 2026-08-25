#!/usr/bin/env python3
"""Mechanical ownership checks for the MM Python runtime and phase-tour tools."""

from __future__ import annotations

import ast
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))


class MMPythonStructureTests(unittest.TestCase):
    def test_forwarding_runtime_facade_is_deleted_and_unreferenced(self) -> None:
        self.assertFalse((TOOLS / "mm_runtime.py").exists())
        stale_imports: list[str] = []
        for path in TOOLS.glob("*.py"):
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom) and node.module == "mm_runtime":
                    stale_imports.append(f"{path.name}:{node.lineno}")
                if isinstance(node, ast.Import) and any(
                    alias.name == "mm_runtime" for alias in node.names
                ):
                    stale_imports.append(f"{path.name}:{node.lineno}")
        self.assertEqual(stale_imports, [])

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
