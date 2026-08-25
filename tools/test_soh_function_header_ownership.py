#!/usr/bin/env python3
"""Structural contract for SoH's responsibility-owned function headers."""

from __future__ import annotations

import re
import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
UMBRELLA = REPO_ROOT / "Shipwright/soh/include/functions.h"
OWNER_DIR = UMBRELLA.with_suffix("")
GLOBAL_HEADER = REPO_ROOT / "Shipwright/soh/include/global.h"
SOH_CMAKE = REPO_ROOT / "Shipwright/soh/CMakeLists.txt"
LEGACY_DECOMP_ROOT = "Shipwright/soh/src/"
AUTHORED_DECOMP_EXCEPTION = "Shipwright/soh/src/zelda3d/"
SOURCE_LINE_LIMIT = 1200
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
COMMENT_OR_LITERAL = re.compile(
    r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL
)


def code_without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*", "", source)


def declared_function_names(header: Path) -> set[str]:
    source = code_without_comments(header.read_text())
    names = set()
    for statement in source.split(";"):
        statement = "\n".join(
            line for line in statement.splitlines() if not line.lstrip().startswith("#")
        )
        match = re.search(r"\b([A-Za-z_]\w*)\s*\(", statement)
        if match:
            names.add(match.group(1))
    return names


class FunctionHeaderOwnershipTests(unittest.TestCase):
    @staticmethod
    def tracked_soh_sources() -> list[str]:
        return subprocess.check_output(
            ["git", "ls-files", "-co", "--exclude-standard", "Shipwright/soh"],
            cwd=REPO_ROOT,
            text=True,
        ).splitlines()

    def test_compatibility_umbrella_is_deleted_and_unreferenced(self) -> None:
        self.assertFalse(
            UMBRELLA.exists(), "focused contracts must not retain an aggregate umbrella"
        )
        offenders = []
        include_pattern = re.compile(
            r'^\s*#\s*include\s*[<"](?:include/)?functions\.h[>"]', re.MULTILINE
        )
        for relative in self.tracked_soh_sources():
            path = REPO_ROOT / relative
            if path.suffix not in SOURCE_SUFFIXES or not path.exists():
                continue
            if include_pattern.search(
                code_without_comments(path.read_text(errors="ignore"))
            ):
                offenders.append(relative)
        self.assertEqual(offenders, [], "legacy function umbrella still has consumers")

    def test_consumers_include_every_function_owner_they_use(self) -> None:
        symbol_owner = {}
        for header in OWNER_DIR.glob("*.h"):
            for symbol in declared_function_names(header):
                self.assertNotIn(symbol, symbol_owner, f"duplicate owner for {symbol}")
                symbol_owner[symbol] = header.name

        missing = []
        owner_root = OWNER_DIR.relative_to(REPO_ROOT).as_posix() + "/"
        for relative in self.tracked_soh_sources():
            path = REPO_ROOT / relative
            if (
                path.suffix not in SOURCE_SUFFIXES
                or not path.exists()
                or relative.startswith(owner_root)
                or (
                    relative.startswith(LEGACY_DECOMP_ROOT)
                    and not relative.startswith(AUTHORED_DECOMP_EXCEPTION)
                )
            ):
                continue
            source = path.read_text(errors="ignore")
            tokens = set(
                re.findall(r"\b[A-Za-z_]\w*\b", COMMENT_OR_LITERAL.sub(" ", source))
            )
            required = {symbol_owner[token] for token in tokens & symbol_owner.keys()}
            included = set(
                re.findall(
                    r'^\s*#\s*include\s*"functions/([^\"]+\.h)"', source, re.MULTILINE
                )
            )
            for header in sorted(required - included):
                missing.append(f"{relative}: functions/{header}")
        self.assertEqual(
            missing, [], "consumer relies on an undeclared transitive function owner"
        )

    def test_legacy_decomp_keeps_generated_include_boundary(self) -> None:
        offenders = []
        include_pattern = re.compile(
            r'^\s*#\s*include\s*[<"]functions/', re.MULTILINE
        )
        for relative in self.tracked_soh_sources():
            if not relative.startswith(LEGACY_DECOMP_ROOT) or relative.startswith(
                AUTHORED_DECOMP_EXCEPTION
            ):
                continue
            path = REPO_ROOT / relative
            if path.suffix in SOURCE_SUFFIXES and path.exists() and include_pattern.search(
                code_without_comments(path.read_text(errors="ignore"))
            ):
                offenders.append(relative)
        self.assertEqual(
            offenders,
            [],
            "legacy decomp source must not absorb authored function-header imports",
        )

    def test_global_header_does_not_aggregate_function_owners(self) -> None:
        source = code_without_comments(GLOBAL_HEADER.read_text())
        self.assertNotRegex(
            source,
            r'^\s*#\s*include\s*[<"]functions/',
            msg="global.h must not replace the umbrella",
        )

    def test_cmake_discovers_focused_headers_recursively(self) -> None:
        source = code_without_comments(SOH_CMAKE.read_text())
        self.assertRegex(
            source,
            r'file\s*\(\s*GLOB_RECURSE\s+Header_Files__include\b[^)]*"include/\*\.h"',
        )
        self.assertRegex(
            source,
            r'set\s*\(\s*SOH_FUNCTION_OWNER_HEADERS\s+\$\{Header_Files__include\}\s*\)',
        )
        self.assertRegex(
            source,
            r'list\s*\(\s*FILTER\s+SOH_FUNCTION_OWNER_HEADERS\s+INCLUDE\s+REGEX\s+'
            r'"/include/functions/\[\^/\]\+\\\\\.h\$"\s*\)',
        )

    def test_cmake_forces_focused_owners_only_at_legacy_boundary(self) -> None:
        source = code_without_comments(SOH_CMAKE.read_text())
        # The forced-include wiring lives in the shared helper
        # zelda3d_shared/cmake/LegacySourceContracts.cmake (MM reuses it); this game's CMakeLists
        # must call it and apply the options to the legacy decomp boundary ONLY.
        self.assertIn(
            "zelda3d_c_forced_include_options(SOH_FUNCTION_OWNER_COMPILE_OPTIONS",
            source,
        )
        self.assertIn(
            "HEADERS ${SOH_FUNCTION_OWNER_HEADERS}", source,
        )
        helper = code_without_comments(
            (REPO_ROOT / "Shipwright/zelda3d_shared/cmake/LegacySourceContracts.cmake").read_text()
        )
        self.assertRegex(
            helper,
            r'if\s*\(\s*uses_msvc_frontend\s*\)',
        )
        self.assertIn('list(APPEND options "/FI${header}")', helper)
        self.assertIn('list(APPEND options "-include" "${header}")', helper)
        self.assertRegex(
            source,
            r'list\s*\(\s*FILTER\s+SOH_LEGACY_DECOMP_SOURCES\s+EXCLUDE\s+REGEX\s+'
            r'"\^src/zelda3d/"\s*\)',
        )
        self.assertRegex(
            source,
            r'set_source_files_properties\s*\(\s*\$\{SOH_LEGACY_DECOMP_SOURCES\}\s+'
            r'PROPERTIES\s+'
            r'COMPILE_OPTIONS\s+"\$\{SOH_LEGACY_DECOMP_COMPILE_OPTIONS\}"\s*\)',
        )

    def test_focused_headers_stay_bounded_and_cpp_safe(self) -> None:
        for header in OWNER_DIR.glob("*.h"):
            with self.subTest(header=header.name):
                source = header.read_text()
                self.assertLessEqual(len(source.splitlines()), SOURCE_LINE_LIMIT)
                self.assertNotRegex(
                    source,
                    r"\bthis\b",
                    msg="use an ABI-neutral parameter name such as thisx",
                )


if __name__ == "__main__":
    unittest.main()
