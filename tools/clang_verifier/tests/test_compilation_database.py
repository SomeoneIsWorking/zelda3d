"""Tests for compiler identification from compile_commands.json entries."""

import unittest
from pathlib import Path

from clang_verifier.compilation_database import verify_compilers


class CompilationDatabaseTests(unittest.TestCase):
    def test_compiler_gate_discriminates_clang_from_gcc(self) -> None:
        clang = [
            {
                "file": "ok.cpp",
                "arguments": ["ccache", "/usr/bin/clang++", "-c", "ok.cpp"],
            }
        ]
        gcc = [{"file": "bad.cpp", "command": "ccache /usr/bin/g++ -c bad.cpp"}]

        self.assertEqual(
            verify_compilers(clang, Path("missing/compile_commands.json")), []
        )
        failures = verify_compilers(gcc, Path("missing/compile_commands.json"))
        self.assertEqual(len(failures), 1)
        self.assertIn("non-Clang compiler g++", failures[0])
