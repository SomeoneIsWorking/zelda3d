#!/usr/bin/env python3
"""Tests for the two scene-name generators.

Both generators write a table that the game indexes positionally, and both previously had a way to
produce a table **nothing builds**. `gen_scene_names.py` wrote to
`Shipwright/soh/src/zelda3d/zelda3d_scene_names.inc` while the build includes
`Shipwright/soh/src/zelda3d/tables/zelda3d_scene_names.inc` (via `scene_replacement.c`'s
`#include "../tables/zelda3d_scene_names.inc"`). Regenerating therefore created a second, untracked
copy of the same data and left the tracked one stale — and the two had already drifted in their header
count (102/111 generated vs 101/110 written) while every NAME still matched, which is the worst kind of
divergence: it looks correct until someone edits the copy that is not the one that compiles.

The properties pinned here:

* each generator's OUT path is the file the build actually includes, so "regenerate" cannot mean
  "write a different file";
* regenerating is IDEMPOTENT — a second run must produce no diff, which is the only way a generated
  table's staleness is detectable without reading it;
* the generated header count equals the number of names actually in the table, so the count in the
  file cannot drift from the file;
* the one hand-written comment inside the generated table is preserved and stays attached to the
  sceneNum it documents, keyed by id rather than by position so adding a row cannot shift it.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# The two tables spell the N64 segment differently: OoT writes `(ydan)`, MM writes `(Z2_20SICHITAI2)`
# and `((unset))` for its own empty slots. The paren group therefore has to tolerate a nested `(` --
# `[^)]*` stops at MM's `((unset)` and leaves a stray `)` that no longer matches `\s*\*/`, so all 11
# UNSET rows silently drop out and the table looks 102 rows of pure names instead of 102 + 11 NULLs.
# That undercount is invisible unless the header count is checked against the file, which is why
# TheHeaderCountMatchesTheTable exists.
ENTRY = re.compile(
    r'/\* 0x([0-9A-F]{2}) SCENE_(\S+)\s+\((?P<seg>[^()]*|\([^()]*\))\)'
    r'\s*\*/\s*(?P<val>NULL|"[a-z0-9_]+")'
)

# The one table each game compiles, and the generator that owns it.
TABLES = {
    "oot": (
        REPO / "tools" / "gen_scene_names.py",
        REPO / "Shipwright" / "soh" / "src" / "zelda3d" / "tables" / "zelda3d_scene_names.inc",
        'include "../tables/zelda3d_scene_names.inc"',
        REPO / "Shipwright" / "soh" / "src" / "zelda3d" / "scene" / "scene_replacement.c",
    ),
    "mm": (
        REPO / "tools" / "gen_mm_scene_names.py",
        REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_scene_names.inc",
        '"mm3d_scene_names.inc"',
        None,
    ),
}

TITLE_SCENE_ID = 0x6E
TITLE_COMMENT_MARK = "OoT3D's dedicated title-demo scene"


def _rom_key(game: str) -> str | None:
    keys = ("ZELDA3D_OOT3D_ROM", "ZELDA3D_MM3D_ROM", "ZELDA3D_MM_ROM")
    for key in keys:
        if os.environ.get(key):
            return key
    return None


def _run_generator(game: str) -> None:
    script, _table, _inc, _src = TABLES[game]
    env = dict(os.environ)
    subprocess.run(
        [sys.executable, str(script)], cwd=REPO, env=env, check=True, capture_output=True
    )


class TheGeneratorWritesTheTableTheBuildIncludes(unittest.TestCase):
    def test_out_path_is_the_compiled_table(self) -> None:
        for game, (script, table, include, src) in TABLES.items():
            with self.subTest(game=game):
                text = script.read_text()
                out_line = next(l for l in text.splitlines() if l.startswith("OUT = "))
                # Resolve the declared path and require it to be the compiled table.
                declared = out_line.split('"')[1]
                self.assertEqual(
                    (REPO / declared).resolve(),
                    table.resolve(),
                    f"{game}: generator writes {declared}, but the build includes {table.name}",
                )
                if src is not None:
                    self.assertIn(include, src.read_text(), f"{game}: include form changed")

    def test_no_stray_second_copy_exists(self) -> None:
        """The failure mode this file exists for: a generator output nothing includes."""
        strays = [
            p
            for p in (REPO / "Shipwright" / "soh" / "src" / "zelda3d").glob("*scene_names.inc")
            if "tables" not in p.parts
        ]
        self.assertEqual(strays, [], f"untracked generator output that nothing builds: {strays}")


class RegenerationIsIdempotent(unittest.TestCase):
    def _assert_idempotent(self, game: str) -> None:
        _script, table, _inc, _src = TABLES[game]
        before = table.read_text()
        _run_generator(game)
        after = table.read_text()
        self.assertEqual(before, after, f"{game}: regenerating changed the table; it is not reproducible")

    def test_oot_regeneration_is_idempotent(self) -> None:
        if not _rom_key("oot"):
            self.skipTest("no OoT3D ROM in the environment")
        self._assert_idempotent("oot")

    def test_mm_regeneration_is_idempotent(self) -> None:
        if not _rom_key("mm"):
            self.skipTest("no MM3D ROM in the environment")
        self._assert_idempotent("mm")


class TheHeaderCountMatchesTheTable(unittest.TestCase):
    def _check(self, game: str) -> None:
        _script, table, _inc, _src = TABLES[game]
        text = table.read_text()
        entries = ENTRY.findall(text)
        names = sum(1 for e in entries if e[3] != "NULL")
        header = re.search(r"(\d+)/(\d+) scenes mapped", text)
        self.assertIsNotNone(header, f"{game}: the generated header must state the mapping count")
        self.assertEqual(int(header.group(1)), names, f"{game}: header count vs actual names")
        self.assertEqual(int(header.group(2)), len(entries), f"{game}: header total vs table rows")

    def test_oot_header_count(self) -> None:
        self._check("oot")

    def test_mm_header_count(self) -> None:
        self._check("mm")


class TheOneHandWrittenCommentSurvives(unittest.TestCase):
    def test_the_title_scene_note_is_preserved_and_on_the_right_row(self) -> None:
        text = TABLES["oot"][1].read_text()
        self.assertIn(TITLE_COMMENT_MARK, text, "the title-scene note must survive regeneration")
        lines = text.splitlines()
        note_at = next(i for i, l in enumerate(lines) if TITLE_COMMENT_MARK in l)
        row_after = next(l for l in lines[note_at:] if ENTRY.match(l.strip()))
        self.assertTrue(
            row_after.lstrip().startswith(f"/* 0x{TITLE_SCENE_ID:02X} "),
            f"the note must sit above its own row; found {row_after.strip()[:40]!r}",
        )

    def test_it_is_keyed_by_id_not_position(self) -> None:
        """A positional key would let an inserted row shift the note onto another scene."""
        script = TABLES["oot"][0].read_text()
        self.assertIn("PRESERVED", script)
        keyed = re.search(r"PRESERVED\s*=\s*\{\s*\n\s*(0x[0-9A-Fa-f]+)\s*:", script)
        self.assertIsNotNone(keyed, "the preserved-comment map must be keyed by sceneNum")
        self.assertEqual(int(keyed.group(1), 16), TITLE_SCENE_ID)


if __name__ == "__main__":
    unittest.main()
