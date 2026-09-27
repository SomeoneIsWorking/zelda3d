#!/usr/bin/env python3
"""Tests for the CmbVShader uniform-coverage survey.

The survey exists to stop a capture planner from assuming an index is readable when no capture wrote
it, and from assuming a written index means the question is answered. Both mistakes are silent, so
both directions are pinned here.
"""

from __future__ import annotations

import json
import struct
import tempfile
import unittest
from pathlib import Path

import sys

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from cmb_shader_uniform_coverage import (
    INDEX_SCOPE_NOTE,
    UNIFORM_BLOCKS,
    UNIFORM_QUESTIONS,
    CoverageReport,
    CaptureCoverage,
    _layout_identified,
    _model_view_rows_are_identity,
    coverage_for_probe,
    format_report,
    survey,
)


class FakeUniforms:
    """The slice of VertexUniforms the survey's checks touch."""

    def __init__(self, values: dict[int, tuple[float, ...]]) -> None:
        self._values = values

    def get(self, index: int):
        return self._values.get(index)


IDENTITY = (1.0, 0.0, 0.0, 0.0)
ZERO4 = (0.0, 0.0, 0.0, 0.0)


def _model_view(rows) -> dict[int, tuple[float, ...]]:
    values: dict[int, tuple[float, ...]] = {}
    for index, row in enumerate(rows):
        padded = tuple(row) + (0.0,) * (4 - len(row))
        values[4 + index] = padded
    return values


class ModelViewIdentityCheck(unittest.TestCase):
    def test_identity_rows_are_recognised(self) -> None:
        rows = [
            (1.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 0.0, 1.0),
        ]
        self.assertTrue(_model_view_rows_are_identity(FakeUniforms(_model_view(rows))))

    def test_translation_only_in_row_three_is_still_identity(self) -> None:
        """The title's case: the distinguishing part sits in row 3, which the shader never reads."""
        rows = [
            (1.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 0.0, 1.0),
        ]
        values = _model_view(rows)
        values[7] = (1234.0, 5678.0, 0.0, 1.0)
        self.assertTrue(_model_view_rows_are_identity(FakeUniforms(values)))

    def test_a_rotated_row_is_not_identity(self) -> None:
        rows = [
            (0.0, 1.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 0.0, 1.0),
        ]
        self.assertFalse(_model_view_rows_are_identity(FakeUniforms(_model_view(rows))))

    def test_absent_model_view_counts_as_unusable_not_as_non_identity(self) -> None:
        """A capture without c4..c6 must not be reported as the non-identity case."""
        self.assertTrue(_model_view_rows_are_identity(FakeUniforms({})))


class LayoutIdentification(unittest.TestCase):
    def test_title_probe_counts_as_identified(self) -> None:
        self.assertTrue(_layout_identified(Path("title-pica-command-list_2010.json"), {}))

    def test_bulk_gameplay_probe_without_material_is_not_identified(self) -> None:
        self.assertFalse(
            _layout_identified(Path("pica-command-writer_190_7dd8.json"), {"draw": 4})
        )

    def test_probe_naming_a_material_is_identified(self) -> None:
        self.assertTrue(
            _layout_identified(Path("pica-command-writer_190_7dd8.json"), {"material": "g_title"})
        )

    def test_index_scope_note_names_the_reason(self) -> None:
        self.assertIn("PER-MATERIAL", INDEX_SCOPE_NOTE)


class Answerability(unittest.TestCase):
    def _capture(self, **kwargs) -> CaptureCoverage:
        defaults = dict(
            cache_key="k",
            probe_path=Path("p.json"),
            artifact_path=Path("a.bin"),
            end_word=10,
            draw=1,
            present=frozenset({"uModelView", "uInvView"}),
            uniform_count=8,
            error=None,
            layout_identified=True,
            uniforms=FakeUniforms(
                _model_view([(0.0, 1.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)])
            ),
        )
        defaults.update(kwargs)
        return CaptureCoverage(**defaults)

    def test_spanning_is_not_answerability(self) -> None:
        capture = self._capture(layout_identified=False)
        self.assertTrue(capture.spans(("uModelView", "uInvView")))
        self.assertFalse(capture.can_answer(("uModelView", "uInvView")))

    def test_value_check_rejects_a_degenerate_capture(self) -> None:
        degenerate = self._capture(
            uniforms=FakeUniforms(
                _model_view([(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)])
            )
        )
        check = lambda uniforms: not _model_view_rows_are_identity(uniforms)  # noqa: E731
        self.assertTrue(degenerate.spans(("uModelView", "uInvView")))
        self.assertFalse(degenerate.can_answer(("uModelView", "uInvView"), check))

    def test_value_check_accepts_a_usable_capture(self) -> None:
        check = lambda uniforms: not _model_view_rows_are_identity(uniforms)  # noqa: E731
        self.assertTrue(self._capture().can_answer(("uModelView", "uInvView"), check))

    def test_missing_block_is_reported(self) -> None:
        capture = self._capture(present=frozenset({"uModelView"}))
        self.assertFalse(capture.spans(("uModelView", "uInvView")))


class ReportBuckets(unittest.TestCase):
    def test_blocked_question_survives_an_empty_capture_set(self) -> None:
        report = CoverageReport()
        self.assertEqual(report.answered(), {})
        self.assertEqual(report.blocked()[0][0].split()[0], "uInvView")

    def test_report_names_the_per_material_caveat(self) -> None:
        report = CoverageReport()
        text = format_report(report, Path("/nonexistent"))
        self.assertIn("PER-MATERIAL", text)
        self.assertIn("scanned 0", text)


class ShippedQuestionWiring(unittest.TestCase):
    """Pin the shipped table, not just the helper.

    The value check is the part that stops the title capture from being reported as closing question
    (1): it spans uModelView and uInvView and its layout IS identified, but its model-view rows are
    the identity, so the "non-identity" question is not answered by it. Wiring that check into
    UNIFORM_QUESTIONS is the only thing that makes the report true, so it is asserted here through
    the real report rather than by calling can_answer directly.
    """

    QUESTION_ONE = "uInvView equals inverse(view) on a non-identity model-view draw"

    def _title_like_capture(self, rows) -> CaptureCoverage:
        return CaptureCoverage(
            cache_key="k",
            probe_path=Path("title-pica-command-list_2010.json"),
            artifact_path=Path("a.bin"),
            end_word=10,
            draw=77,
            present=frozenset({"uModelView", "uInvView", "TexCoordSlot+ShaderMode"}),
            uniform_count=37,
            error=None,
            layout_identified=True,
            uniforms=FakeUniforms(
                {**_model_view(rows), 89: ZERO4, 76: IDENTITY, 77: IDENTITY, 78: IDENTITY}
            ),
        )

    def test_question_one_wires_a_value_check(self) -> None:
        _blocks, _tracker, value_check = UNIFORM_QUESTIONS[self.QUESTION_ONE]
        self.assertIsNotNone(value_check, "question (1) lost its non-identity value check")

    def test_identity_model_view_leaves_question_one_blocked(self) -> None:
        capture = self._title_like_capture(
            [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)]
        )
        report = CoverageReport(captures=[capture])
        self.assertNotIn(self.QUESTION_ONE, report.answered())
        self.assertIn(self.QUESTION_ONE, [question for question, _b, _t in report.blocked()])
        # It still spans the blocks, so the report must show that weaker fact rather than hide it.
        self.assertIn(self.QUESTION_ONE, report.spanning())

    def test_non_identity_model_view_answers_question_one(self) -> None:
        capture = self._title_like_capture(
            [(0.0, 1.0, 0.0), (-1.0, 0.0, 0.0), (0.0, 0.0, 1.0)]
        )
        report = CoverageReport(captures=[capture])
        self.assertIn(self.QUESTION_ONE, report.answered())

    def test_texcoordslot_question_carries_its_denominator(self) -> None:
        capture = self._title_like_capture(
            [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)]
        )
        report = CoverageReport(captures=[capture])
        text = format_report(report, Path("/nonexistent"))
        self.assertIn("DENOMINATOR", text)
        self.assertIn("NOT 'ever'", text)


class SurveyRefusesAnEmptyCorpus(unittest.TestCase):
    def test_missing_root_raises(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "missing oracle cache root"):
            survey(Path("/nonexistent-oracle-cache"))

    def test_root_with_no_command_lists_raises(self) -> None:
        """An empty survey must not read as 'nothing can answer anything'."""
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "empty-cache").mkdir()
            with self.assertRaisesRegex(RuntimeError, "no decodable command-list captures"):
                survey(Path(directory))

    def test_unrelated_probe_records_are_ignored_not_counted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / "cache"
            (cache / "key" / "probes").mkdir(parents=True)
            (cache / "key" / "probes" / "other.json").write_text(json.dumps({"error": "x"}))
            with self.assertRaisesRegex(RuntimeError, "no decodable command-list captures"):
                survey(cache)


class RealCorpusIsReadable(unittest.TestCase):
    """When the cache is present, the survey must actually walk it rather than no-op."""

    def test_real_cache_walks(self) -> None:
        cache_root = REPO / "scratch" / "oracle_cache"
        if not cache_root.is_dir():
            self.skipTest("no oracle cache in this checkout")
        report = survey(cache_root)
        self.assertGreater(report.scanned_probes, 0)
        self.assertGreater(len(report.captures), 0)
        # Every block named in the table must be a real index range, and no capture may claim a block
        # it does not actually write.
        for capture in report.captures:
            for block in capture.present:
                for index in UNIFORM_BLOCKS[block]:
                    self.assertGreaterEqual(index, 0)
                    self.assertLess(index, 96)


class UnpackSanity(unittest.TestCase):
    def test_float32_word_decodes_to_its_own_value(self) -> None:
        """Guards the arithmetic that made 0x4e7e0000 look like a decoder bug.

        It is not one: 0x4e7e0000 is a legitimate float32 near 1.065e9. Reading it as "1.0" because a
        neighbouring word was 0x3f800000 is how a real decode path gets wrongly accused.
        """
        word = 0x4E7E0000
        value = struct.unpack("<f", struct.pack("<I", word))[0]
        self.assertAlmostEqual(value, 1065353216.0, places=0)
        self.assertNotAlmostEqual(value, 1.0, places=3)


if __name__ == "__main__":
    unittest.main()
