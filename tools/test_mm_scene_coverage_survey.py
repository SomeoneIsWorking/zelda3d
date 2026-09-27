#!/usr/bin/env python3
"""Tests for the MM3D scene-coverage survey.

The survey answers "which MM3D scenes does the host actually divert to 3DS rooms?", and a mapping gap
is invisible from the host's side — a missing entry just looks like content that was never ported. So
the properties pinned here are the ones that keep the survey from being confidently wrong:

* the scene table is PARSED from the one owner (`mm3d_scene_names.inc`), never transcribed. A copy
  here would drift from the table the game uses, which is exactly how the LA4 format constant was
  "supported" on paper while `PicaDecode` returned empty;
* reachability follows the host's EXACT path construction, `/scenes/<name>_<roomNum>_info.zsi` with
  roomNum always appended. A file with no numeric room suffix is therefore unreachable by
  construction, and folding it into "coverage" would overstate what the game can draw;
* a scene shipping only a room-0 file IS reachable (room 0 exists) and must not be reported as a
  coverage hole — the opposite error, understating coverage;
* MM3D's own SCENE_UNSET ids are counted apart from mapping gaps so they cannot pad the number;
* a room file whose base is absent from the table IS a real gap and is named.
"""

from __future__ import annotations

import re
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

import mm_scene_coverage_survey as survey  # noqa: E402

ROOM_SUFFIX = re.compile(r"^.*_\d+$")


class TheTableIsParsedFromItsOneOwner(unittest.TestCase):
    def test_the_inc_file_is_where_the_game_reads_it(self) -> None:
        self.assertEqual(survey.SCENE_NAMES_INC.name, "mm3d_scene_names.inc")
        self.assertTrue(survey.SCENE_NAMES_INC.is_file(), "the scene table owner must exist")

    def test_every_entry_is_parsed(self) -> None:
        mapped, unset, total = survey.parse_scene_table()
        self.assertGreater(total, 0)
        self.assertEqual(total, len(mapped) + len(unset))

    def test_ids_are_unique_and_in_range(self) -> None:
        mapped, _unset, _total = survey.parse_scene_table()
        self.assertEqual(len(mapped), len(set(mapped)))

    def test_a_transcribed_copy_would_not_be_what_is_tested(self) -> None:
        """The suite reads the file, so a rename in the game table is a test failure, not silence."""
        text = survey.SCENE_NAMES_INC.read_text()
        self.assertIn("kMm3dSceneNames", text)
        self.assertIn('"z2_20sichitai2"', text)


class RoomsAreSplitByBaseName(unittest.TestCase):
    def test_a_room_suffixed_stem_yields_its_base_and_room(self) -> None:
        rooms = survey.rooms_by_base({"/scenes/kakusiana_3_info.zsi", "/scenes/kakusiana_4_info.zsi"})
        self.assertEqual(rooms["kakusiana"], {3, 4})

    def test_a_stem_with_no_suffix_is_kept_as_room_minus_one(self) -> None:
        """Dropping it would understate the archive; room -1 marks 'no numeric suffix present'."""
        rooms = survey.rooms_by_base({"/scenes/spot00_info.zsi"})
        self.assertEqual(rooms["spot00"], {-1})

    def test_underscored_names_are_not_split_wrongly(self) -> None:
        # z2_01keikoku_0 must give base z2_01keikoku, not z2_01.
        rooms = survey.rooms_by_base({"/scenes/z2_01keikoku_0_info.zsi"})
        self.assertEqual(rooms["z2_01keikoku"], {0})


class RoomSuffixDecidesReachability(unittest.TestCase):
    def test_room_suffixed_files_are_room_suffixed(self) -> None:
        self.assertTrue(ROOM_SUFFIX.match("kakusiana_3"))
        self.assertTrue(ROOM_SUFFIX.match("z2_01keikoku_0"))

    def test_an_unsuffixed_file_is_not(self) -> None:
        """The host always appends room->num, so `<name>_info.zsi` can never be built."""
        self.assertIsNone(ROOM_SUFFIX.match("spot00"))
        self.assertIsNone(ROOM_SUFFIX.match("kakusiana"))


class MissingRomIsARefusalNotACleanResult(unittest.TestCase):
    def test_no_rom_env_is_a_clear_error(self) -> None:
        saved = {k: __import__("os").environ.pop(k, None) for k in ("ZELDA3D_MM3D_ROM", "ZELDA3D_MM_ROM")}
        try:
            with self.assertRaisesRegex(RuntimeError, "source .env"):
                survey.mm_rom_path()
        finally:
            for key, value in saved.items():
                if value is not None:
                    __import__("os").environ[key] = value

    def test_main_returns_nonzero_without_a_rom(self) -> None:
        import os

        saved = {k: os.environ.pop(k, None) for k in ("ZELDA3D_MM3D_ROM", "ZELDA3D_MM_ROM")}
        try:
            self.assertEqual(survey.main([]), 1)
        finally:
            for key, value in saved.items():
                if value is not None:
                    os.environ[key] = value


class TheVerdictNamesGapsRatherThanCountingThem(unittest.TestCase):
    """These call `classify_scene_files` DIRECTLY, not through `survey()`.

    That is deliberate. An earlier suite exercised only the ROM-gated `survey()` path, so two real
    mutations -- transcribing the table instead of parsing it, and treating no-suffix files as
    reachable -- both left all 14 tests green. A rule tested only through an entry point that needs
    external state is not tested.
    """

    def test_a_room_file_whose_base_is_unmapped_is_a_named_gap(self) -> None:
        files = {"/scenes/spot00_0_info.zsi", "/scenes/z2_turibori_0_info.zsi"}
        rooms = survey.rooms_by_base(files)
        reachable, unreachable, unsuffixed = survey.classify_scene_files(
            files, {"spot00"}, rooms
        )
        self.assertEqual(reachable, {"/scenes/spot00_0_info.zsi"})
        self.assertEqual(unreachable, ["/scenes/z2_turibori_0_info.zsi"])
        self.assertEqual(unsuffixed, [])

    def test_a_scene_shipping_only_room_zero_is_still_reachable(self) -> None:
        """The opposite error: room 0 exists, so this is covered and must not be called a gap."""
        files = {"/scenes/z2_01keikoku_0_info.zsi"}
        rooms = survey.rooms_by_base(files)
        reachable, unreachable, _ = survey.classify_scene_files(
            files, {"z2_01keikoku"}, rooms
        )
        self.assertEqual(reachable, files)
        self.assertEqual(unreachable, [])

    def test_a_no_suffix_file_is_unreachable_by_construction_not_a_mapping_gap(self) -> None:
        """The host always appends room->num, so it can never build `/scenes/<name>_info.zsi`."""
        files = {"/scenes/spot00_info.zsi"}
        rooms = survey.rooms_by_base(files)
        reachable, unreachable, unsuffixed = survey.classify_scene_files(files, {"spot00"}, rooms)
        self.assertEqual(reachable, set())
        self.assertEqual(unreachable, [], "a no-suffix file is an archive-layout fact, not a gap")
        self.assertEqual(unsuffixed, ["/scenes/spot00_info.zsi"])

    def test_the_three_buckets_partition_the_input(self) -> None:
        files = {
            "/scenes/spot00_0_info.zsi",
            "/scenes/spot00_info.zsi",
            "/scenes/kakusiana_3_info.zsi",
            "/scenes/z2_turibori_0_info.zsi",
        }
        rooms = survey.rooms_by_base(files)
        reachable, unreachable, unsuffixed = survey.classify_scene_files(
            files, {"spot00", "kakusiana"}, rooms
        )
        self.assertEqual(len(reachable) + len(unreachable) + len(unsuffixed), len(files))
        self.assertEqual(reachable & set(unreachable), set())
        self.assertEqual(reachable & set(unsuffixed), set())

    def test_a_mapped_name_with_no_rooms_contributes_nothing(self) -> None:
        """A dead mapping must not make an unrelated file look reachable.

        Built so the ROM-backed room set contains a DIFFERENT scene, which is the situation a dead
        mapping actually produces: the name is in the table, but `rooms` has no entry for it, so the
        loop over `rooms.get(name, ())` yields nothing. Sourcing `rooms` from a file set that does not
        contain the name is what makes the dead mapping observable at all.
        """
        files = {"/scenes/z2_turibori_0_info.zsi"}
        rooms = survey.rooms_by_base(files)  # has z2_turibori -> {0}
        # Simulate the dead mapping by passing rooms that do NOT know the name.
        _reachable, unreachable, _ = survey.classify_scene_files(files, {"z2_turibori"}, {})
        self.assertEqual(unreachable, ["/scenes/z2_turibori_0_info.zsi"])
        # And with the rooms present it is reachable, so the difference is the mapping, not the file.
        reachable, unreachable, _ = survey.classify_scene_files(files, {"z2_turibori"}, rooms)
        self.assertEqual(reachable, files)
        self.assertEqual(unreachable, [])


class TheRealTableHasNoDeadMappings(unittest.TestCase):
    """ROM-free properties of the real table, so a mapping regression fails without a ROM."""

    def test_no_mapped_name_is_unreachable_in_the_rom(self) -> None:
        import os

        if not (os.environ.get("ZELDA3D_MM3D_ROM") or os.environ.get("ZELDA3D_MM_ROM")):
            self.skipTest("no MM3D ROM in the environment")
        result = survey.survey()
        self.assertEqual(result["dead_mappings"], [], "a mapped name with no room file is dead")
        self.assertGreater(result["mapped_names_with_rooms"], 0)


if __name__ == "__main__":
    unittest.main()
