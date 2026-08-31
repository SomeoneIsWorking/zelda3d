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


if __name__ == "__main__":
    unittest.main()
