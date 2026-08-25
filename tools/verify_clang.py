#!/usr/bin/env python3
"""Verify agent Clang metadata, source structure, formatting, and lint."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from clang_verifier import (
    VerificationError,
    clang_tool_runner,
    compilation_database,
    format_check,
    source_selection,
    source_structure,
    tidy_check,
    tidy_config_check,
)

REPO = Path(__file__).resolve().parents[1]
DEFAULT_COMPILE_COMMANDS = REPO / "Shipwright" / "build-cmake" / "compile_commands.json"
ADDITIONAL_COMPILE_COMMANDS = (
    REPO / "Azahar" / "build-harness" / "compile_commands.json",
)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        "--all", action="store_true", help="check every tracked first-party C/C++ file"
    )
    selection.add_argument(
        "--files", nargs="+", metavar="PATH", help="check only these first-party files"
    )
    parser.add_argument(
        "--compile-commands", type=Path, default=DEFAULT_COMPILE_COMMANDS
    )
    parser.add_argument(
        "--clang-format", dest="clang_format", help="clang-format executable override"
    )
    parser.add_argument(
        "--clang-tidy", dest="clang_tidy", help="clang-tidy executable override"
    )
    parser.add_argument(
        "--format-only",
        action="store_true",
        help="skip clang-tidy (toolchain and structure still run)",
    )
    parser.add_argument(
        "--tidy-only",
        action="store_true",
        help="skip clang-format (toolchain and structure still run)",
    )
    args = parser.parse_args(argv)
    if args.format_only and args.tidy_only:
        parser.error("--format-only and --tidy-only are mutually exclusive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        structural_failures = source_structure.verify_structure(REPO)
        if structural_failures:
            details = "\n".join(f"  {failure}" for failure in structural_failures)
            raise VerificationError(f"source structure check failed:\n{details}")

        compile_commands = args.compile_commands.resolve()
        databases = [compile_commands]
        databases.extend(path for path in ADDITIONAL_COMPILE_COMMANDS if path.is_file())
        entries_by_database = [
            (path, compilation_database.load_compile_commands(path))
            for path in databases
        ]
        entries = [
            entry
            for _path, database_entries in entries_by_database
            for entry in database_entries
        ]
        compiler_failures = [
            failure
            for path, database_entries in entries_by_database
            for failure in compilation_database.verify_compilers(database_entries, path)
        ]
        if compiler_failures:
            details = "\n".join(f"  {failure}" for failure in compiler_failures)
            raise VerificationError(f"agent Clang build check failed:\n{details}")

        if args.all:
            files = source_selection.repository_files(
                REPO, source_selection.FORMAT_SUFFIXES
            )
        elif args.files:
            files = source_selection.explicit_files(REPO, args.files)
        else:
            files = source_selection.changed_files(REPO)

        compile_entries_by_database = {
            path: compilation_database.first_party_entries(REPO, database_entries)
            for path, database_entries in entries_by_database
        }
        compile_entries = {
            source: entry
            for database_entries in compile_entries_by_database.values()
            for source, entry in database_entries.items()
        }
        tidy_files = [
            path.resolve()
            for path in files
            if path.suffix.lower() in source_selection.SOURCE_SUFFIXES
        ]
        missing = [
            source_selection.repo_relative(path, REPO)
            for path in tidy_files
            if path not in compile_entries
        ]
        if missing and not args.format_only:
            raise VerificationError(
                "touched first-party translation units missing from compile_commands.json:\n  "
                + "\n  ".join(missing)
            )

        clang_tidy = None
        tidy_config_count = 0
        if not args.format_only:
            clang_tidy = clang_tool_runner.require_tool(args.clang_tidy, "clang-tidy")
            tidy_config_count = tidy_config_check.validate_repository_tidy_configs(
                REPO, clang_tidy
            )

        if not args.tidy_only and files:
            format_check.verify_format(
                files,
                clang_tool_runner.require_tool(args.clang_format, "clang-format"),
                repo=REPO,
            )
        if not args.format_only and tidy_files:
            assert clang_tidy is not None
            remaining = set(tidy_files)
            for database, database_entries in compile_entries_by_database.items():
                database_files = sorted(remaining.intersection(database_entries))
                if database_files:
                    tidy_check.verify_tidy(
                        database_files, clang_tidy, database, repo=REPO
                    )
                    remaining.difference_update(database_files)

        format_count = 0 if args.tidy_only else len(files)
        tidy_count = 0 if args.format_only else len(tidy_files)
        print(
            "clang verification passed: "
            f"{len(entries)} compile entries use Clang; {format_count} format-checked; "
            f"{tidy_count} tidied; {tidy_config_count} clang-tidy configs validated; "
            f"{len(source_selection.repository_files(REPO, source_selection.STRUCTURE_SUFFIXES))} "
            "source files structure-checked"
        )
        return 0
    except VerificationError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
