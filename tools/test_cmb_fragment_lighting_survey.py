#!/usr/bin/env python3
from __future__ import annotations

import unittest
from types import SimpleNamespace
from unittest import mock

import cmb_fragment_lighting_survey as survey


def _cmb_material(enabled: bool = True) -> tuple[bytearray, int]:
    data = bytearray(0x240)
    data[:4] = b"cmb "
    data[0x08:0x0C] = (6).to_bytes(4, "little")
    data[0x28:0x2C] = (0x40).to_bytes(4, "little")
    data[0x40:0x44] = b"mats"
    data[0x48:0x4C] = (1).to_bytes(4, "little")
    material = 0x4C
    data[material] = int(enabled)
    data[material + 0xA0 : material + 0xB4] = bytes(range(1, 21))
    for index, value in enumerate((0x62C884C0, 0, 0x62B0, 0x62C0, 0, 0x62A0FF01, 0x3F800000)):
        start = material + 0xCC + 0x10 + index * 4
        data[start : start + 4] = value.to_bytes(4, "little")
    return data, material


def _stage(
    rgb_sources: list[int],
    alpha_sources: list[int],
    rgb_op: int = 0x2100,
    alpha_op: int = 0x2100,
) -> SimpleNamespace:
    return SimpleNamespace(
        rgb_op=rgb_op,
        a_op=alpha_op,
        rgb_src=rgb_sources,
        a_src=alpha_sources,
        sig=lambda: "synthetic",
    )


class FragmentLightingSurveyTests(unittest.TestCase):
    def test_joins_enabled_flag_material_colors_and_active_tev_sources(self) -> None:
        data, _ = _cmb_material()
        stages = [_stage([survey.FRAGMENT_PRIMARY, 0x84C0, 0], [0x8577, 0x84C0, 0])]
        parsed = [(0, [], [], [], stages)]

        with mock.patch.object(survey, "parse_mats", return_value=parsed):
            records = survey.scan_materials("light.cmb", bytes(data))

        self.assertEqual(len(records), 1)
        self.assertTrue(records[0].enabled)
        self.assertEqual(records[0].emission, (1, 2, 3, 4))
        self.assertEqual(records[0].ambient, (5, 6, 7, 8))
        self.assertEqual(records[0].diffuse, (9, 10, 11, 12))
        self.assertEqual(records[0].specular0, (13, 14, 15, 16))
        self.assertEqual(records[0].specular1, (17, 18, 19, 20))
        self.assertEqual(
            records[0].descriptor_words,
            (0x62C884C0, 0, 0x62B0, 0x62C0, 0, 0x62A0FF01, 0x3F800000),
        )
        self.assertEqual(records[0].primary_uses, 1)

    def test_ignores_fragment_source_in_unused_replace_slots(self) -> None:
        data, _ = _cmb_material()
        stages = [
            _stage(
                [0x84C0, survey.FRAGMENT_PRIMARY, survey.FRAGMENT_SECONDARY],
                [0x8577, survey.FRAGMENT_PRIMARY, survey.FRAGMENT_SECONDARY],
                rgb_op=0x1E01,
                alpha_op=0x1E01,
            )
        ]
        with mock.patch.object(survey, "parse_mats", return_value=[(0, [], [], [], stages)]):
            records = survey.scan_materials("unused.cmb", bytes(data))

        self.assertEqual(records[0].primary_uses, 0)
        self.assertEqual(records[0].secondary_uses, 0)

    def test_reports_authored_fragment_source_even_when_enable_flag_is_clear(self) -> None:
        data, _ = _cmb_material(enabled=False)
        stages = [_stage([survey.FRAGMENT_SECONDARY, 0x84C0, 0], [0x8577, 0x84C0, 0])]
        with mock.patch.object(survey, "parse_mats", return_value=[(0, [], [], [], stages)]):
            records = survey.scan_materials("mismatch.cmb", bytes(data))

        self.assertEqual(len(records), 1)
        self.assertFalse(records[0].enabled)
        self.assertEqual(records[0].secondary_uses, 1)


QTRS_CHUNK = 0x40
MATS_CHUNK = 0x60


def _mm3d_material(enabled: bool = True) -> tuple[bytearray, int]:
    """A version-7 CMB laid out the way MM3D stores it.

    0x28 points at the `qtrs` chunk and 0x2C at the `mats` chunk, because version >= 7 inserts the
    `qtrs` pointer right after the skeleton and shifts every later chunk pointer by +4. The material
    records then start at mats_offset + 0x0C.
    """
    data = bytearray(0x240)
    data[:4] = b"cmb "
    data[0x08:0x0C] = (7).to_bytes(4, "little")
    data[0x28:0x2C] = QTRS_CHUNK.to_bytes(4, "little")
    data[0x2C:0x30] = MATS_CHUNK.to_bytes(4, "little")
    data[QTRS_CHUNK : QTRS_CHUNK + 4] = b"qtrs"
    data[MATS_CHUNK : MATS_CHUNK + 4] = b"mats"
    data[MATS_CHUNK + 8 : MATS_CHUNK + 12] = (1).to_bytes(4, "little")
    material = MATS_CHUNK + 0x0C
    data[material] = int(enabled)
    data[material + 0xA0 : material + 0xB4] = bytes(range(1, 21))
    return data, material


class Mm3dMaterialChunkTests(unittest.TestCase):
    """The MM3D layout is a different container layout, not a different game.

    Reading the material chunk pointer at 0x28 unconditionally points at MM3D's `qtrs` chunk, so
    every MM3D file parses as ZERO materials. That failure is silent -- no error, just an empty
    survey that reads like "no material uses fragment lighting" -- which is exactly how MM3D's
    lighting dependency stayed invisible. These cases pin the version gate.
    """

    def _stages(self):
        return [_stage([survey.FRAGMENT_PRIMARY, 0x84C0, 0], [0x8577, 0x84C0, 0])]

    def test_version7_finds_its_material_chunk_past_the_qtrs_pointer(self) -> None:
        data, _ = _mm3d_material()
        with mock.patch.object(survey, "parse_mats", return_value=[(0, [], [], [], self._stages())]):
            records = survey.scan_materials("mm3d.cmb", bytes(data))
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].primary_uses, 1)

    def test_version6_still_uses_the_unshifted_pointer(self) -> None:
        data, _ = _cmb_material()
        with mock.patch.object(survey, "parse_mats", return_value=[(0, [], [], [], self._stages())]):
            records = survey.scan_materials("oot3d.cmb", bytes(data))
        self.assertEqual(len(records), 1)

    def test_a_version7_file_whose_mats_pointer_is_absent_reports_nothing(self) -> None:
        data, _ = _mm3d_material()
        data[0x2C:0x30] = (0).to_bytes(4, "little")
        with mock.patch.object(survey, "parse_mats", return_value=[(0, [], [], [], self._stages())]):
            self.assertEqual(survey.scan_materials("broken.cmb", bytes(data)), [])

    def test_non_cmb_input_is_rejected(self) -> None:
        self.assertEqual(survey.scan_materials("x", b"NOTACMB" + bytes(0x40)), [])


def _one_material_corpus(offset_pairs: dict[int, int]) -> list[tuple[str, bytes]]:
    """A one-material version-7 corpus whose material record holds `offset -> u32` values."""
    data, _ = _mm3d_material()
    material = MATS_CHUNK + 0x0C
    for offset, value in offset_pairs.items():
        data[material + offset : material + offset + 4] = value.to_bytes(4, "little")
    return [("synthetic.cmb", bytes(data))]


class DescriptorEnumSweepTests(unittest.TestCase):
    """The enum sweep is what licenses the cross-game structural claim, so it needs its own controls.

    A sweep that returned "distinctive" for any input would let the MM3D conclusion through on
    nothing, so both the positive and the negative shape are pinned.
    """

    def test_finds_the_enum_at_exactly_one_offset(self) -> None:
        corpus = _one_material_corpus({0xDC: survey.FRAGMENT_LIGHT_AMB_COLOR, 0xE0: 0})
        hits = [(offset, count) for offset, count in survey.descriptor_enum_sweep(corpus) if count]
        self.assertEqual([offset for offset, _count in hits], [0xDC])

    def test_reports_not_distinctive_when_the_enum_repeats(self) -> None:
        """The same enum at two offsets must NOT read as distinctive, or the claim is vacuous."""
        corpus = _one_material_corpus(
            {0xDC: survey.FRAGMENT_LIGHT_AMB_COLOR, 0xE8: survey.FRAGMENT_LIGHT_AMB_COLOR}
        )
        hits = [(offset, count) for offset, count in survey.descriptor_enum_sweep(corpus) if count]
        self.assertGreaterEqual(len(hits), 2)

    def test_the_measured_block_is_excluded_from_its_own_control(self) -> None:
        """+0xA0..+0xB0 is the block under test; sampling it as a 'control' would be circular."""
        offsets = [offset for offset, _value in survey.offset_control_sweep(_one_material_corpus({}))]
        self.assertFalse([o for o in offsets if 0xA0 <= o <= 0xB0])

    def test_control_sweep_reads_more_offsets_than_the_record_padding(self) -> None:
        offsets = [offset for offset, _value in survey.offset_control_sweep(_one_material_corpus({}))]
        self.assertGreater(len(offsets), 10)


if __name__ == "__main__":
    unittest.main()
