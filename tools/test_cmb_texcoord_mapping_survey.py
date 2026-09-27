#!/usr/bin/env python3
"""Pin the coordinator-mapping/lighting corpus survey's counting and scope.

`tools/cmb_texcoord_mapping_survey.py` answers the question that decided whether
the host's texture-coordinate mapping needed a lit-ness gate: which retail
materials declare mapping method 3 or 4, and are they lit. Its result is
recorded in `oot3d-decomp/docs/cmb_texcoord_mapping.md` §8, so the counting has
to be exact: a method-4 material that a combiner never samples cannot change a
pixel, and counting those anyway would overstate the reach.

The tests build synthetic materials and check the two things that can go wrong
quietly — the mapping/lighting bytes read from the wrong offset, and a consumed
unit being counted as a declared-only one.
"""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path
from unittest import mock

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import cmb_texcoord_mapping_survey as mapping
from cmb_corpus import CORPORA, iter_corpus, iter_mm3d_cmbs, iter_oot_cmbs
from tev_corpus_survey import (MaterialRecord, material_chunk_pointer, parse_material_records,
                               slots_used)

COORD_OFFSET = 0x58
COORD_STRIDE = 0x18
MATERIAL_STRIDE = 0x15C
COMB_BASE = 0x124 + 2 * 0  # stage index table sits right after the material structs


class _Stage:
    """A combiner stage. `INTERPOLATE` (0x8575) is the three-slot op; MODULATE uses two."""

    def __init__(self, sources, op: int = 0x8575):
        self.rgb_op = op
        self.a_op = 0x2100
        self.rgb_src = list(sources) + [0x8577] * (3 - len(sources))
        self.a_src = [0x8577, 0x8577, 0x8577]


def _cmb(materials):
    """Build a one-combiner-table CMB carrying `materials` material records."""
    count = len(materials)
    data = bytearray(0x1000)
    data[:4] = b"cmb "
    data[0x08:0x0C] = (6).to_bytes(4, "little")  # version 6 -> 0x15C stride
    mats_at = 0x40
    data[0x28:0x2C] = mats_at.to_bytes(4, "little")
    data[mats_at : mats_at + 4] = b"mats"
    data[mats_at + 8 : mats_at + 12] = count.to_bytes(4, "little")
    comb_base = mats_at + 0x0C + count * MATERIAL_STRIDE
    for index, spec in enumerate(materials):
        base = mats_at + 0x0C + index * MATERIAL_STRIDE
        data[base] = spec["fragment_lighting"]
        data[base + 1] = spec["vertex_lighting"]
        for unit, method in enumerate(spec["methods"]):
            data[base + COORD_OFFSET + COORD_STRIDE * unit + 2] = method
        data[base + 0x120 : base + 0x124] = (1).to_bytes(4, "little")  # one stage
        data[base + 0x124 : base + 0x126] = (0).to_bytes(2, "little")  # combiner index 0
        # Combiner entry 0: three RGB sources, slot 0..2 at +0x0C.
        for slot, source in enumerate(spec["sources"]):
            struct.pack_into("<H", data, comb_base + 0x0C + 2 * slot, source)
    return bytes(data)


def _record(**kwargs):
    defaults = dict(index=0, tex=[-1] * 3, coord_mapping=[1, 1, 1], coord_source=[0, 0, 0],
                    stages=[], fragment_lighting=False, vertex_lighting=False)
    defaults.update(kwargs)
    return MaterialRecord(**defaults)


def _mm_cmb(materials):
    """A Majora's Mask CMB: version 10, with a `qtrs` chunk pointer at 0x28."""
    payload = bytearray(_cmb(materials))
    struct.pack_into("<I", payload, 0x08, 10)  # MM3D version
    struct.pack_into("<I", payload, 0x28, 0x80)  # `qtrs` chunk now occupies 0x28
    payload[0x80:0x84] = b"qtrs"
    struct.pack_into("<I", payload, 0x2C, 0x40)  # ...so `mats` moved to 0x2C
    return bytes(payload)


class TexcoordMappingSurveyTests(unittest.TestCase):
    @staticmethod
    def _corpus(*items):
        """A stub for `iter_corpus(game)`: the survey calls the result with no arguments."""
        return lambda: iter(items)

    def test_consumed_units_reads_every_combiner_slot(self) -> None:
        # Pinned because the slot count is op-dependent: a fixture that assumed three
        # sources for MODULATE would have counted two and reported a unit as unused.
        self.assertEqual(slots_used(0x2100), 2)
        self.assertEqual(slots_used(0x8575), 3)
        self.assertEqual(mapping.consumed_units([_Stage([0x84C0, 0x84C2, 0x84C3])]), {0, 2, 3})
        self.assertEqual(mapping.consumed_units([_Stage([0x84C0, 0x84C1, 0x8577])]), {0, 1})
        self.assertEqual(mapping.consumed_units([_Stage([0x84C0, 0x84C2, 0x8577])]), {0, 2})
        self.assertEqual(mapping.consumed_units([_Stage([0x8577, 0x8577, 0x8577])]), set())
        # The alpha chain counts too: MODULATE's two alpha slots are real sources.
        self.assertEqual(
            mapping.consumed_units([_Stage([0x8577, 0x8577, 0x8577], op=0x2100)]), set())

    def test_only_methods_three_and_four_are_reported(self) -> None:
        with mock.patch.object(mapping, "iter_corpus", return_value=self._corpus(("m.cmb", _cmb([
            {"fragment_lighting": True, "vertex_lighting": True,
             "methods": [3, 4, 1], "sources": [0x84C0, 0x8577, 0x8577]},
        ])))), mock.patch.object(mapping, "parse_material_records", side_effect=lambda b: [
            _record(index=0, coord_mapping=[3, 4, 1], fragment_lighting=True,
                    vertex_lighting=True, stages=[_Stage([0x84C0, 0x84C1, 0x8577])]),
        ]):
            table = mapping.survey()
        self.assertEqual(sorted(table), [
            "tex0 CameraSphereEnvMap [consumed]",
            "tex1 ProjectionMap [consumed]",
        ])

    def test_a_unit_no_combiner_samples_is_reported_as_declared_only(self) -> None:
        # tex1 declares method 4 but the chain only samples tex0, so it cannot change a pixel.
        with mock.patch.object(mapping, "iter_corpus", return_value=self._corpus(("m.cmb", _cmb([
            {"fragment_lighting": False, "vertex_lighting": True,
             "methods": [1, 4, 1], "sources": [0x84C0, 0x8577, 0x8577]},
        ])))), mock.patch.object(mapping, "parse_material_records", side_effect=lambda b: [
            _record(index=0, coord_mapping=[1, 4, 1], vertex_lighting=True,
                    stages=[_Stage([0x84C0, 0x8577, 0x8577])]),
        ]):
            table = mapping.survey()
        self.assertEqual(sorted(table), ["tex1 ProjectionMap [declared-only]"])
        self.assertEqual(table["tex1 ProjectionMap [declared-only]"]["lit"], 1)

    def test_lighting_split_is_reported_per_method(self) -> None:
        specs = [
            (True, True, [1, 4, 1]),   # both
            (False, True, [1, 4, 1]),  # vertex only
            (True, False, [1, 4, 1]),  # fragment only
            (False, False, [1, 4, 1]),  # unlit
        ]
        records = [
            _record(index=i, coord_mapping=m, fragment_lighting=f, vertex_lighting=v,
                    stages=[_Stage([0x84C0, 0x84C1, 0x8577])])
            for i, (f, v, m) in enumerate(specs)
        ]
        with mock.patch.object(mapping, "iter_corpus", return_value=self._corpus(("m.cmb", _cmb([
            {"fragment_lighting": f, "vertex_lighting": v, "methods": m,
             "sources": [0x84C0, 0x84C1, 0x8577]}
            for f, v, m in specs
        ])))), mock.patch.object(mapping, "parse_material_records",
                                  side_effect=lambda b: records):
            table = mapping.survey()
        counts = table["tex1 ProjectionMap [consumed]"]
        self.assertEqual(counts["lit"], 3)
        self.assertEqual(counts["unlit"], 1)
        self.assertEqual(counts["both"], 1)
        self.assertEqual(counts["vertex-only"], 1)
        self.assertEqual(counts["fragment-only"], 1)

    def test_method_byte_is_read_from_the_coordinator_entry(self) -> None:
        # The real parse walk, not a mock: put a known method on coordinator 1 and check
        # it comes back from the coordinator offset rather than anywhere else.
        payload = _cmb([{"fragment_lighting": False, "vertex_lighting": True,
                         "methods": [1, 3, 1], "sources": [0x84C0, 0x84C1, 0x8577]}])
        record = next(iter(parse_material_records(payload)))
        self.assertEqual(record.coord_mapping, [1, 3, 1])
        self.assertTrue(record.vertex_lighting)
        self.assertFalse(record.fragment_lighting)
        self.assertTrue(record.lit)

    def test_both_games_are_reachable_corpora(self) -> None:
        # One survey, two populations: which container each game uses is the corpus
        # owner's decision, not something each survey re-derives.
        self.assertEqual(sorted(CORPORA), ["mm", "oot"])
        self.assertIs(iter_corpus("oot"), iter_oot_cmbs)
        self.assertIs(iter_corpus("mm"), iter_mm3d_cmbs)
        with self.assertRaises(ValueError):
            iter_corpus("mm3d")

    def test_majoras_mask_qtrs_insertion_is_honoured(self) -> None:
        # MM3D (version >= 7) inserts a `qtrs` pointer at 0x28, so `mats` is at 0x2C.
        # Reading 0x28 unconditionally pointed at `qtrs` and every MM3D material parsed as
        # zero materials — a silent zero, not an error. Both halves are pinned here.
        payload = _mm_cmb([{"fragment_lighting": True, "vertex_lighting": False,
                            "methods": [1, 4, 1], "sources": [0x84C0, 0x84C1, 0x8577]}])
        self.assertEqual(material_chunk_pointer(payload), 0x40)
        records = list(parse_material_records(payload))
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].coord_mapping, [1, 4, 1])
        self.assertTrue(records[0].fragment_lighting)
        # The OoT3D layout must still read at its own offset, unchanged.
        oot = _cmb([{"fragment_lighting": True, "vertex_lighting": True,
                     "methods": [3, 1, 1], "sources": [0x84C0, 0x8577, 0x8577]}])
        self.assertEqual(material_chunk_pointer(oot), 0x40)
        self.assertEqual(list(parse_material_records(oot))[0].coord_mapping, [3, 1, 1])

    def test_a_non_cmb_reports_no_chunk_rather_than_a_wild_offset(self) -> None:
        self.assertIsNone(material_chunk_pointer(b"not a cmb at all" + bytes(0x100)))
        # A truncated/garbage payload must not be probed at a random offset either.
        self.assertIsNone(material_chunk_pointer(b"cmb " + bytes(0x40)))

    def test_an_empty_corpus_is_refused_not_reported_as_an_answer(self) -> None:
        # "This game uses no mapped coordinators" is a claim; a broken container or a
        # wrong layout read produces the same empty table. The survey must refuse it.
        with mock.patch.object(mapping, "iter_corpus",
                               return_value=self._corpus(("only.cmb", b"garbage"))):
            with self.assertRaises(RuntimeError):
                mapping.survey(game="mm")


if __name__ == "__main__":
    unittest.main()
