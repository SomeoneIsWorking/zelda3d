"""The env-light record type must exist exactly once, for both games.

`Zelda3dLightSlot` and `Zelda3dSceneLight` describe the 3DS environment-light record, whose tail is a
byte-for-byte copy of the N64 `EnvLightSettings` in BOTH games -- OoT3D's block sits at `+0x0A` and
MM3D's at `+0x0B`, one byte apart, and each game's table is generated from its own recovered offsets.

That is exactly the shape where a duplicated struct is dangerous. If either game declared its own
copy, the two could drift, and the failure is invisible: MM3D's `fogColor` would be read as its second
light colour -- a wrong value with precisely the right shape, so nothing crashes and nothing looks
broken. MM3D's offsets already differ from OoT3D's by one byte, so the copies *would* drift.

These are source-level checks, not compile checks: they run without building either game, and they
also assert the two generated tables reference the shared spelling rather than a local one.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

SHARED = REPO / "Shipwright" / "zelda3d_shared" / "lighting" / "zelda3d_env_record.h"
SOH_HEADER = REPO / "Shipwright" / "soh" / "src" / "zelda3d" / "lighting" / "zelda3d_lighting.h"
MM_TABLE = REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_scene_lighting.inc"
OOT_TABLE = REPO / "Shipwright" / "soh" / "src" / "zelda3d" / "tables" / "zelda3d_scene_lighting.inc"

TYPES = ("Zelda3dLightSlot", "Zelda3dSceneLight")


def typedefs(text: str, name: str) -> str | None:
    m = re.search(rf"typedef struct \{{(.*?)\}} {name}\s*;", text, re.S)
    return m.group(1) if m else None


class ThereIsOneDefinitionOfEachType(unittest.TestCase):
    def test_the_shared_header_defines_both(self) -> None:
        text = SHARED.read_text()
        for name in TYPES:
            self.assertIsNotNone(typedefs(text, name), f"{SHARED} does not define {name}")

    def test_the_soh_header_uses_the_shared_one(self) -> None:
        text = SOH_HEADER.read_text()
        self.assertIn('lighting/zelda3d_env_record.h', text,
                      "soh's env header no longer includes the shared record header")
        for name in TYPES:
            self.assertIsNone(typedefs(text, name),
                              f"soh re-declares {name}; a second copy is exactly what drifts")

    def test_the_record_has_no_game_specific_dependency(self) -> None:
        """2ship must be able to include it, so it must not pull in either game's own headers.

        Only the `#include` lines are inspected, never the prose: this file names both games' paths
        in its comment precisely because a reader needs to know where the two tables live, and a
        substring test over the whole file would fail on that documentation instead of on a
        dependency.
        """
        includes = re.findall(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]',
                              SHARED.read_text(), re.M)
        for forbidden in ("global.h", "ultra64.h", "z64.h", "z64scene.h", "ultra.h"):
            self.assertNotIn(forbidden, includes,
                             f"the shared record header includes {forbidden}; 2ship cannot "
                             "include a header that depends on one game's own types")
        self.assertEqual(includes, [], "the record type is plain data and needs no includes at all")

    def test_the_two_copies_were_never_allowed_to_agree_only_by_accident(self) -> None:
        """Guards the reason this file exists: the two games' blocks are ONE BYTE apart."""
        from gen_mm3d_scene_lighting import MM3D, OOT3D

        self.assertNotEqual(MM3D.ambient, OOT3D.ambient,
                            "MM3D's and OoT3D's colour blocks are the same offset now; if that is "
                            "true the generator layouts must be re-derived, not this test relaxed")


class BothGeneratedTablesUseTheSharedSpelling(unittest.TestCase):
    def test_the_tables_exist(self) -> None:
        for path in (MM_TABLE, OOT_TABLE):
            self.assertTrue(path.is_file(), f"missing {path}")

    def test_each_table_uses_the_shared_type_names(self) -> None:
        for path, symbol in ((MM_TABLE, "kMm3dSceneLighting"), (OOT_TABLE, "kZelda3dSceneLighting")):
            text = path.read_text()
            self.assertIn(f"static const Zelda3dSceneLight {symbol}[]", text)
            self.assertIn("static const Zelda3dLightSlot", text)

    def test_neither_table_declares_the_structs_itself(self) -> None:
        """A generated .inc that carried its own struct would shadow the shared one silently."""
        for path in (MM_TABLE, OOT_TABLE):
            text = path.read_text()
            for name in TYPES:
                self.assertIsNone(typedefs(text, name),
                                  f"{path} re-declares {name}; a .inc must not define the type it "
                                  "fills, or it shadows the shared header for every includer")

    def test_the_tables_state_which_games_layout_they_came_from(self) -> None:
        """The one-byte difference is the trap; a table must not leave a reader guessing."""
        mm = MM_TABLE.read_text().split("\n\n")[0]
        oot = OOT_TABLE.read_text().split("\n\n")[0]
        self.assertIn("ptr+0x28", mm)
        self.assertIn("0x20", mm)
        self.assertIn("NOT OoT3D's", mm)


if __name__ == "__main__":
    unittest.main()
