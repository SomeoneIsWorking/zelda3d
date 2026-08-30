#!/usr/bin/env python3
"""Falsifiers for the offline CmbVShader PRIMARY corpus instrument."""

from __future__ import annotations

import unittest
from types import SimpleNamespace
from unittest import mock

import cmb_primary_corpus_survey as survey
from cmb import MODE_ARRAY, MODE_CONSTANT


def _fake_cmb(color_mode: int) -> SimpleNamespace:
    color = SimpleNamespace(mode=color_mode)
    return SimpleNamespace(
        version=6,
        mats_ptr=0,
        meshes=[SimpleNamespace(sepd_index=0, material_index=0, mesh_id=7)],
        sepds=[SimpleNamespace(attrs={"color": color})],
        vatr={"color": (0, 0)},
    )


class PrimaryCorpusSurveyTests(unittest.TestCase):
    def test_reports_unlit_no_color_nonwhite_diffuse(self) -> None:
        data = bytearray(0x200)
        material = 0x0C
        data[material + 1] = 0  # IsVertexLighting
        data[material + 0xA8 : material + 0xAC] = bytes((255, 140, 0, 255))

        with mock.patch.object(survey, "Cmb", return_value=_fake_cmb(MODE_ARRAY)):
            found = survey.candidates("candle.cmb", bytes(data))

        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].diffuse, (255, 140, 0, 255))

    def test_rejects_vertex_lit_material(self) -> None:
        data = bytearray(0x200)
        material = 0x0C
        data[material + 1] = 1  # IsVertexLighting; byte 0 is IsFragmentLighting
        data[material + 0xA8 : material + 0xAC] = bytes((255, 140, 0, 255))

        with mock.patch.object(survey, "Cmb", return_value=_fake_cmb(MODE_ARRAY)):
            self.assertEqual(survey.candidates("lit.cmb", bytes(data)), [])

    def test_rejects_present_constant_color_attribute(self) -> None:
        data = bytearray(0x200)
        material = 0x0C
        data[material + 0xA8 : material + 0xAC] = bytes((255, 140, 0, 255))

        with mock.patch.object(survey, "Cmb", return_value=_fake_cmb(MODE_CONSTANT)):
            self.assertEqual(survey.candidates("colored.cmb", bytes(data)), [])


if __name__ == "__main__":
    unittest.main()
