#!/usr/bin/env python3
"""Stable test entry point composing the focused Clang verifier suites."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from clang_verifier.tests import (
    test_compilation_database,
    test_format_check,
    test_source_selection,
    test_source_structure,
    test_tidy_config_check,
)


def load_tests(
    loader: unittest.TestLoader, _tests: unittest.TestSuite, _pattern: str | None
) -> unittest.TestSuite:
    suite = unittest.TestSuite()
    for module in (
        test_compilation_database,
        test_format_check,
        test_source_selection,
        test_source_structure,
        test_tidy_config_check,
    ):
        suite.addTests(loader.loadTestsFromModule(module))
    return suite


if __name__ == "__main__":
    unittest.main()
