"""Tests for the MM3D per-scene lighting generator.

The two things that must not rot here are both *negatives* that were previously recorded as facts:

1. **The layouts must stay distinguishable, two-sided.** MM3D's env record is at `ptr+0x28` with
   stride `0x20`; OoT3D's is at `ptr+0x10` with stride `0x1C`. A consumer or generator hard-coded to
   the other game's offsets reads MM3D's `fogColor` as its second light colour and produces values
   that are wrong but plausible. A one-sided check would not catch that, so both directions are
   asserted: the MM layout must score ~0 on OoT3D data, and the OoT3D layout ~0 on MM3D data, while
   each scores high on its own.

2. **LzS inflation must stay load-bearing.** "MM3D has no scene lighting" was a recorded conclusion
   and it was wrong, for exactly one reason: 182 of MM3D's 424 scene ZSIs are LzS-compressed and were
   parsed as plain bytes. Compressed bytes do not produce an error when fed to a command-stream
   parser -- they produce plausible garbage, which is what made the negative so convincing. The
   control that shows it: parsing them as plain yields 256 distinct "ctypes" across the whole
   0x00-0xFF range, 234 of them outside OoT3D's 22-value command set.

The most valuable test in this file needs no ROM at all: MM's own N64 `EnvLightSettings` in
`2ship/include/z64environment.h` fixes the *internal spacing* of the six colour triples, and the
recovered 3DS record must match it under a single uniform shift. That is an authority independent of
this project's own measurement, so it cannot be satisfied by a self-consistent wrong offset.
"""

from __future__ import annotations

import os
import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

import gen_mm3d_scene_lighting as gen  # noqa: E402
from ctr_romfs import CtrRom  # noqa: E402
from mm_animmap_archive import lzs_decompress, lzs_is_compressed  # noqa: E402

MM_ROM = os.environ.get("ZELDA3D_MM3D_ROM")
OOT_ROM = os.environ.get("ZELDA3D_OOT3D_ROM")

# MM's N64 EnvLightSettings, in 2ship/include/z64environment.h: six u8[3] triples, in this order.
N64_ENV_TRIPLES = ("ambientColor", "light1Dir", "light1Color",
                   "light2Dir", "light2Color", "fogColor")
#: those triples' byte offsets, as MM's own N64 struct defines them
N64_OFFSETS = [0x00, 0x03, 0x06, 0x09, 0x0C, 0x0F]


def _zsis(rom_path: str, pattern: str) -> list[str]:
    rom = CtrRom(rom_path)
    try:
        out = []
        for entry in rom.iter_files():
            p = getattr(entry, "path", str(entry))
            if re.match(pattern, p):
                out.append(p)
        return sorted(out)
    finally:
        rom.fp.close()


def _reads(rom_path: str, pattern: str, layout: gen.Layout, inflate: bool = True) -> int:
    """How many of the matched ZSIs yield a plausible env palette under `layout`."""
    rom = CtrRom(rom_path)
    try:
        hits = 0
        for entry in rom.iter_files():
            p = getattr(entry, "path", str(entry))
            if not re.match(pattern, p):
                continue
            raw = rom.read(rom.get(p))
            if not inflate and lzs_is_compressed(raw):
                continue
            if gen.plausible(gen.parse_env(raw, layout)):
                hits += 1
        return hits
    finally:
        rom.fp.close()


class TheLayoutsStayDistinguishableTwoSided(unittest.TestCase):
    """ROM-gated: the discrimination is between two real games' real data, so it needs both ROMs."""

    @classmethod
    def setUpClass(cls) -> None:
        if not (MM_ROM and OOT_ROM and Path(MM_ROM).is_file() and Path(OOT_ROM).is_file()):
            raise unittest.SkipTest("needs both ZELDA3D_MM3D_ROM and ZELDA3D_OOT3D_ROM")

    def test_mm3d_data_reads_under_the_mm_layout(self) -> None:
        hits = _reads(MM_ROM, r"/scenes/.+_info\.zsi$", gen.MM3D)
        self.assertGreater(hits, 90, f"MM3D env region collapsed to {hits} hits")

    def test_mm3d_data_scores_nothing_under_the_oot_layout(self) -> None:
        """The wrong-game direction. A generator hard-coded to OoT's offsets lands here and looks fine."""
        self.assertEqual(_reads(MM_ROM, r"/scenes/.+_info\.zsi$", gen.OOT3D), 0)

    def test_oot3d_data_reads_under_the_oot_layout(self) -> None:
        hits = _reads(OOT_ROM, r"/scene/.+_info\.zsi$", gen.OOT3D)
        self.assertGreater(hits, 90, f"OoT3D env region collapsed to {hits} hits")

    def test_oot3d_data_scores_nothing_under_the_mm_layout(self) -> None:
        """The other wrong-game direction, and the control for the two above."""
        self.assertEqual(_reads(OOT_ROM, r"/scene/.+_info\.zsi$", gen.MM3D), 0)

    def test_fog_color_sits_where_the_other_games_offset_is_not(self) -> None:
        """MM's fogColor is +0x1A. OoT's colour-block base is +0x0A, so +0x0A reads MM's fogColor
        as its SECOND light colour -- a wrong value with the right shape, which is the worst kind."""
        self.assertEqual(gen.MM3D.fogcol - gen.MM3D.ambient, 0x0F)
        self.assertEqual(gen.OOT3D.fogcol - gen.OOT3D.ambient, 0x0F)
        self.assertNotEqual(gen.MM3D.fogcol, gen.OOT3D.fogcol)


class TheFieldMapAgreesWithMmOwnN64Struct(unittest.TestCase):
    """ROM-free: MM's own header pins the internal spacing, independent of our measurement."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (REPO / "2ship" / "include" / "z64environment.h").read_text()

    def test_the_struct_is_present_and_sized_as_assumed(self) -> None:
        block = self.header.split("} EnvLightSettings")[0]
        self.assertIn("} EnvLightSettings", self.header)
        offsets = {m.group(2): int(m.group(1), 16)
                   for m in re.finditer(r"/\*\s*0x([0-9A-Fa-f]{2})\s*\*/\s*"
                                        r"(?:u8|s8)\s+([A-Za-z0-9_]+)\[3\]", block)}
        # Re-derive from the comments rather than trusting a list, so a header edit shows up here.
        self.assertEqual([offsets.get(f) for f in N64_ENV_TRIPLES], N64_OFFSETS,
                         f"MM's N64 EnvLightSettings layout moved: {offsets}")

    def test_the_3ds_record_is_that_struct_under_one_uniform_shift(self) -> None:
        """Every triple displaced by the same amount -- which is what makes the map trustworthy."""
        mm_offsets = [gen.MM3D.ambient, gen.MM3D.dir0, gen.MM3D.col0,
                      gen.MM3D.dir1, gen.MM3D.col1, gen.MM3D.fogcol]
        shifts = {m - n for m, n in zip(mm_offsets, N64_OFFSETS)}
        self.assertEqual(len(shifts), 1,
                         f"MM's record is not the N64 struct under a single shift: {mm_offsets}")
        self.assertEqual(shifts.pop(), gen.MM3D.ambient)

    def test_oot3d_has_the_same_internal_spacing_at_a_different_base(self) -> None:
        """OoT3D's block is the same N64 struct at +0x0A; MM3D's is at +0x0B. The spacing is shared
        and only the base differs -- which is exactly why an offset copied between the games lands
        one byte off and reads a colour as the wrong colour."""
        oot_offsets = [gen.OOT3D.ambient, gen.OOT3D.dir0, gen.OOT3D.col0,
                       gen.OOT3D.dir1, gen.OOT3D.col1, gen.OOT3D.fogcol]
        shifts = {o - a for o, a in zip(oot_offsets, N64_OFFSETS)}
        self.assertEqual(len(shifts), 1, f"OoT3D's block is not the N64 struct at one base: {oot_offsets}")
        self.assertEqual(shifts.pop(), 0x0A)
        self.assertEqual(gen.MM3D.ambient - gen.OOT3D.ambient, 1,
                         "MM3D's colour block is one byte past OoT3D's; a copied offset reads "
                         "MM3D's fogColor as its second light colour")


class LzsInflationIsLoadBearing(unittest.TestCase):
    """The regression guard for the wrong negative this table replaces."""

    @classmethod
    def setUpClass(cls) -> None:
        if not (MM_ROM and Path(MM_ROM).is_file()):
            raise unittest.SkipTest("needs ZELDA3D_MM3D_ROM")
        cls.paths = _zsis(MM_ROM, r"/scenes/.+_info\.zsi$")
        cls.compressed = [p for p in cls.paths
                          if lzs_is_compressed(_read(MM_ROM, p))]

    def test_a_large_minority_of_mm3d_zsis_are_compressed(self) -> None:
        self.assertGreater(len(self.compressed), 100,
                           "MM3D's ZSIs stopped being compressed; this whole file is moot")
        self.assertLess(len(self.compressed), len(self.paths),
                        "if ALL were compressed, the plain-parse control below would be vacuous")

    def test_parsing_compressed_zsis_as_plain_finds_far_fewer(self) -> None:
        inflated = _reads(MM_ROM, r"/scenes/.+_info\.zsi$", gen.MM3D, inflate=True)
        plain_only = _reads(MM_ROM, r"/scenes/.+_info\.zsi$", gen.MM3D, inflate=False)
        self.assertGreater(inflated, plain_only * 5,
                           f"inflation only moved {plain_only} -> {inflated}; it is the whole "
                           "difference between the recorded negative and the recovery")

    def test_the_plain_parse_yields_ctypes_outside_the_real_command_set(self) -> None:
        """The control that proves the old 5-of-424 number was noise and not a real measurement.

        Real scene commands come from a small fixed set. Fed compressed bytes, a parser finds
        "commands" spread across the whole byte space -- uniform noise, which is the signature of
        data mistaken for a command stream.
        """
        ctypes: set[int] = set()
        for path in self.compressed:
            blob = _read(MM_ROM, path)  # deliberately NOT inflated
            off = 0x10
            while off + 8 <= len(blob):
                head = struct.unpack_from(">I", blob, off)[0]
                ctype = (head >> 24) & 0xFF
                ctypes.add(ctype)
                off += 8
                if ctype == gen.END_COMMAND:
                    break
        self.assertGreater(len(ctypes), 22,
                           "the compressed files parsed as a small clean command set, which would "
                           "mean this test no longer demonstrates what it claims")


def _read(rom_path: str, path: str) -> bytes:
    rom = CtrRom(rom_path)
    try:
        return rom.read(rom.get(path))
    finally:
        rom.fp.close()


class TheCommandStreamParser(unittest.TestCase):
    def test_finds_the_env_command_and_stops_at_the_terminator(self) -> None:
        blob = bytearray(0x40)
        blob[0:4] = b"ZSI\x01"
        struct.pack_into(">I", blob, 0x10, (gen.ENV_COMMAND << 24) | (3 << 16))
        struct.pack_into("<I", blob, 0x14, 0x200)
        struct.pack_into(">I", blob, 0x18, (gen.END_COMMAND << 24))
        found = gen.find_env_command(bytes(blob))
        self.assertEqual(found, (3, 0x200))

    def test_no_env_command_returns_none(self) -> None:
        blob = bytearray(0x40)
        struct.pack_into(">I", blob, 0x10, (0x11 << 24))
        struct.pack_into(">I", blob, 0x18, (gen.END_COMMAND << 24))
        self.assertIsNone(gen.find_env_command(bytes(blob)))

    def test_uncompressed_bytes_pass_through_unchanged(self) -> None:
        payload = b"ZSI\x01not compressed at all"
        self.assertIs(gen.maybe_inflate(payload), payload)

    def test_an_lzs_buffer_is_inflated(self) -> None:
        """Round-tripping needs a real LzS buffer, so the check is that the helper is wired to the
        project's own decompressor rather than to a hand-rolled one -- a hand-rolled 3DS LZ decoder
        is exactly the kind of thing that yields plausible garbage."""
        self.assertIs(gen.lzs_decompress, lzs_decompress)
        self.assertIs(gen.lzs_is_compressed, lzs_is_compressed)


class ThePlausibilityGateIsAShapeCheck(unittest.TestCase):
    def _slot(self, **over) -> dict:
        base = {"amb": [1, 1, 1], "l0dir": [0, 0, 0], "l0col": [1, 1, 1], "l1dir": [0, 0, 0],
                "l1col": [1, 1, 1], "fogcol": [1, 1, 1], "fognear": 800,
                "fogfar": 2400.0, "zfar": 12000.0}
        base.update(over)
        return base

    def test_sane_values_pass(self) -> None:
        self.assertTrue(gen.plausible([self._slot()]))

    def test_empty_fails(self) -> None:
        self.assertFalse(gen.plausible([]))

    def test_an_absurd_distance_fails(self) -> None:
        self.assertFalse(gen.plausible([self._slot(zfar=1e9)]))
        self.assertFalse(gen.plausible([self._slot(fogfar=0.0)]))
        self.assertFalse(gen.plausible([self._slot(fognear=0xFFFF)]))

    def test_the_gate_does_not_inspect_colour(self) -> None:
        """It is a distance check. Letting it judge colour would make it a tuned threshold."""
        self.assertTrue(gen.plausible([self._slot(amb=[255, 255, 255], fogcol=[0, 0, 0])]))


class TheCommittedTableIsPresentAndConsistent(unittest.TestCase):
    def test_the_generated_include_exists(self) -> None:
        self.assertTrue(Path(gen.OUT).is_file(),
                        f"missing {gen.OUT}; run tools/gen_mm3d_scene_lighting.py")

    def test_its_row_count_matches_mms_scene_table(self) -> None:
        text = Path(gen.OUT).read_text()
        table = text.split("kMm3dSceneLighting[] = {")[1]
        rows = [l for l in table.splitlines() if l.strip().startswith("/*")]
        expected = len(gen.gms.n64_scenes())
        self.assertEqual(len(rows), expected,
                         "a row per scene-table entry; a mismatch shifts every later scene's palette")

    def test_it_states_the_layout_it_was_generated_from(self) -> None:
        """A generated table that does not say which game's layout it used is how a reader
        hard-codes the wrong offsets."""
        header = Path(gen.OUT).read_text().split("\n\n")[0]
        self.assertIn(f"ptr+0x{gen.MM3D.record_base:02X}", header)
        self.assertIn(f"stride 0x{gen.MM3D.stride:02X}", header)

    def test_most_scenes_have_a_palette(self) -> None:
        table = Path(gen.OUT).read_text().split("kMm3dSceneLighting[] = {")[1]
        filled = len(re.findall(r"\{\s*[1-9]\d*\s*,\s*kMm3dSlots_", table))
        self.assertGreater(filled, 90, f"only {filled} scenes have a palette")


if __name__ == "__main__":
    unittest.main()
