#!/usr/bin/env python3
"""Tests for the MM3D actor-archive prefix fallback in `ResolveObjectModel`.

MM3D keeps its MM-only actor models at `/actors/zelda2_<name>.gar.lzs`, but actors it SHARES with
OoT3D keep OoT3D's archive name and live at `/actors/zelda_<name>.gar.lzs` — 72 of the 460 archives in
the MM3D ROM use that shared prefix. Probing only `zelda2_` left **60 object-table ids with no MM3D
model at all**, silently falling back to the N64 actor: 324 of 460 object-table names resolved, 384 do
with the fallback.

The properties pinned here are the ones that keep this from becoming a coin flip:

* the shared prefix is tried **second**, so the five names present under BOTH prefixes
  (`gi_ocarina`, `mag`, `mir_ray`, `ny`, `sb`) keep resolving to their `zelda2_` archive. An id that
  already resolved must not change which archive it gets;
* a miss on the MM prefix is the ONLY thing that consults the shared prefix, so the fallback cannot
  shadow an existing mapping;
* a miss on both is still a miss — the fallback must not invent a model or a default path;
* the 76 names with no archive under either prefix stay unresolved rather than being fuzzy-matched.
  Six of them (`geldb`~`gelb`, `gi_rupy`~`gi_ruppy`, `gi_shield_2`/`gi_shield_3`~`gi_shield_02`,
  `gi_golonmask`~`gi_goronmask`, `gi_bottle_22`~`gi_bottle_21`) are near-misses of real stems, and
  matching them by edit distance would bind objects to the WRONG archive, which is worse than falling
  back to the N64 actor.
"""

from __future__ import annotations

import os
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

CATALOG = REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_model_catalog.cpp"
OBJECT_NAMES_INC = REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_object_names.inc"
MM_PREFIX = "/actors/zelda2_"
SHARED_PREFIX = "/actors/zelda_"
ARCHIVE_SUFFIX = ".gar.lzs"

# Names present under BOTH prefixes in the MM3D ROM, measured. The fallback must not touch these.
AMBIGUOUS = {"gi_ocarina", "mag", "mir_ray", "ny", "sb"}
# Real stems within edit distance of an object-table name that resolves to neither prefix. Binding
# these by fuzzy match would attach the wrong archive, so they are named to keep that from tempting
# anyone later.
NEAR_MISS_STEMS = {
    "geldb": "gelb",
    "gi_rupy": "gi_ruppy",
    "gi_golonmask": "gi_goronmask",
    "gi_shield_2": "gi_shield_02",
    "gi_shield_3": "gi_shield_02",
    "gi_bottle_22": "gi_bottle_21",
}


def _rom_stems() -> tuple[set[str], set[str]]:
    from ctr_romfs import CtrRom

    for key in ("ZELDA3D_MM3D_ROM", "ZELDA3D_MM_ROM"):
        if os.environ.get(key):
            break
    else:
        raise unittest.SkipTest("no MM3D ROM in the environment")
    rom = CtrRom(os.environ[key])
    mm: set[str] = set()
    shared: set[str] = set()
    for entry in rom.iter_files():
        path = getattr(entry, "path", None) or getattr(entry, "name", "")
        if not (isinstance(path, str) and path.startswith("/actors/") and path.endswith(ARCHIVE_SUFFIX)):
            continue
        stem = path[len("/actors/") : -len(ARCHIVE_SUFFIX)]
        if stem.startswith("zelda2_"):
            mm.add(stem[len("zelda2_") :])
        elif stem.startswith("zelda_"):
            shared.add(stem[len("zelda_") :])
    return mm, shared


def _table_names() -> set[str]:
    return set(re.findall(r'"([a-z0-9_]+)"', OBJECT_NAMES_INC.read_text()))


def resolve(name: str, mm: set[str], shared: set[str]) -> str | None:
    """The fallback's decision rule, mirroring `ResolveObjectModel`."""
    if name in mm:
        return MM_PREFIX + name + ARCHIVE_SUFFIX
    if name in shared:
        return SHARED_PREFIX + name + ARCHIVE_SUFFIX
    return None


class TheSourceImplementsTheRuleItDocuments(unittest.TestCase):
    """The rule is asserted against the shipping source, not a paraphrase of it.

    A test that re-implements the resolver proves nothing about the one that runs; this checks the
    actual ordering in the actual file, so reordering the two probes fails here.
    """

    def setUp(self) -> None:
        self.text = CATALOG.read_text()

    def test_the_mm_prefix_is_probed_first(self) -> None:
        mm_at = self.text.index('std::string("/actors/zelda2_")')
        shared_at = self.text.index('std::string("/actors/zelda_")')
        self.assertLess(mm_at, shared_at, "the MM prefix must be probed first")

    def test_the_shared_prefix_is_only_reached_after_an_mm_miss(self) -> None:
        self.assertIn("if (!ProbeModel(path, \"\", skinned, boneCount)) {", self.text)
        # The fallback assignment must sit INSIDE that miss branch, not after the block.
        block = self.text[self.text.index("if (!ProbeModel(path") :]
        self.assertLess(block.index('"/actors/zelda_"'), block.index("g_objectToModel[objectId] = -1;"))

    def test_a_double_miss_is_still_a_miss(self) -> None:
        """Three -1 caches: no name, no archive under either prefix, and the skinned opt-out.

        Asserted as three rather than a bare count so a new early-out cannot pass by coincidence --
        the skinned one is at the `skinnedEnabled` gate and is unrelated to this change.
        """
        self.assertEqual(self.text.count("g_objectToModel[objectId] = -1;"), 3)
        self.assertIn("skip obj=0x%03X", self.text, "the third is the skinned opt-out, not a probe miss")

    def test_the_log_names_the_archive_path(self) -> None:
        """Two archives can share a short name under different prefixes, so the path is logged."""
        self.assertIn("path.c_str()", self.text)


class NoAlreadyResolvedIdChangesArchive(unittest.TestCase):
    def setUp(self) -> None:
        self.mm, self.shared = _rom_stems()
        self.names = _table_names()

    def test_the_ambiguous_names_still_resolve_to_the_mm_archive(self) -> None:
        """The five both-prefix names must keep their zelda2_ archive, not flip to the shared one."""
        for name in sorted(AMBIGUOUS):
            with self.subTest(name=name):
                self.assertIn(name, self.mm, f"{name} must exist under the MM prefix in the ROM")
                self.assertEqual(resolve(name, self.mm, self.shared), MM_PREFIX + name + ARCHIVE_SUFFIX)

    def test_the_fallback_only_adds_ids_that_previously_missed(self) -> None:
        before = {n for n in self.names if n in self.mm}
        after = {n for n in self.names if resolve(n, self.mm, self.shared) is not None}
        self.assertTrue(before <= after, "the fallback must never drop an id that already resolved")
        self.assertFalse(before - after)

    def test_the_fallback_is_a_real_gain(self) -> None:
        before = len(self.names & self.mm)
        after = len([n for n in self.names if resolve(n, self.mm, self.shared) is not None])
        self.assertEqual(after - before, 60, "measured against the MM3D ROM")


class UnresolvableNamesStayUnresolved(unittest.TestCase):
    def setUp(self) -> None:
        self.mm, self.shared = _rom_stems()
        self.names = _table_names()

    def test_near_miss_names_resolve_to_nothing(self) -> None:
        """Fuzzy-matching these would attach the WRONG archive, which is worse than the N64 actor."""
        for name, stem in sorted(NEAR_MISS_STEMS.items()):
            with self.subTest(name=name):
                self.assertIn(stem, self.mm | self.shared, "the near-miss stem must really exist")
                self.assertIsNone(resolve(name, self.mm, self.shared))

    def test_a_name_under_neither_prefix_resolves_to_nothing(self) -> None:
        self.assertIsNone(resolve("definitely_not_an_mm3d_archive", self.mm, self.shared))

    def test_the_unresolvable_count_is_76(self) -> None:
        missing = [n for n in self.names if resolve(n, self.mm, self.shared) is None]
        self.assertEqual(len(missing), 76)


if __name__ == "__main__":
    unittest.main()
