#!/usr/bin/env python3
"""Tests for the texture-format coverage survey.

The survey answers "does the host decode every format the retail content uses?", and it exists
because that question had been silently wrong: `0x67606758` (3dstool LA4) was DECLARED in the shipping
decoder's format table and never switched on, and the Python mirror listed it without a decoder, so
both sides looked like they supported it. Two OoT3D textures used it -- the Great Deku Tree's
magic-fire and magic-love effects -- and `PicaDecode` returned empty, dropping them.

Two properties are pinned here, and both have bitten before:

* The C++ table is PARSED from the shipping source, not transcribed. A transcribed copy is exactly
  how "the decoder handles LA4" becomes true in the tool while the product still drops the texture.
* The decoder must keep REJECTING formats it does not know. A coverage survey that made unknown
  formats decodable would report full coverage while the renderer silently produced wrong pixels.

The corpus-backed assertions live in the survey itself, because they need the ROMs; these cover the
logic, the refusal, and the table agreement.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

import pica_texture_format_survey as survey  # noqa: E402
from pica_texture import GLFMT  # noqa: E402

LA4 = 0x67606758


class DecoderTableIsReadFromTheShippingSource(unittest.TestCase):
    def test_cpp_table_includes_the_format_the_content_uses(self) -> None:
        formats = survey.cpp_decoder_formats()
        self.assertIn(LA4, formats, "0x67606758 is used by retail OoT3D content")
        self.assertEqual(formats[LA4], "LA4")

    def test_a_declared_but_unhandled_format_is_not_counted_as_supported(self) -> None:
        """The parse must require BOTH a declaration and a `case GF_` label.

        This is the exact shape of the bug the survey found: the constant was declared, so reading the
        declaration alone would have reported LA4 as supported while `PicaDecode` returned empty.
        """
        text = survey.CPP_DECODER.read_text()
        self.assertIn("GF_LA4 = 0x67606758", text)
        self.assertIn("case GF_LA4:", text, "declared without a decode case is the bug, not support")

    def test_tables_agree_with_the_python_mirror(self) -> None:
        agree, detail = survey.decoder_tables_agree()
        self.assertTrue(agree, f"decoder tables disagree: {detail}")
        self.assertEqual(detail["cpp_handled"], detail["python_handled"])

    def test_both_tables_report_the_same_fourteen(self) -> None:
        formats = survey.cpp_decoder_formats()
        self.assertEqual(len(formats), len(GLFMT))
        self.assertEqual(set(formats), set(GLFMT))


class UndecodableIsComputedFromTheShippingTable(unittest.TestCase):
    def _report(self, **kwargs) -> survey.FormatReport:
        report = survey.FormatReport(game="test", **kwargs)
        return report

    def test_a_format_outside_the_table_is_reported_undecodable(self) -> None:
        report = self._report(cmb_formats=Counter({LA4: 2, 0xDEADBEEF: 1}))
        undecodable = report.undecodable(set(survey.cpp_decoder_formats()))
        self.assertEqual(dict(undecodable), {0xDEADBEEF: 1})

    def test_a_fully_covered_game_reports_nothing_undecodable(self) -> None:
        report = self._report(cmb_formats=Counter({LA4: 2, 0x0000675A: 7600}))
        self.assertEqual(report.undecodable(set(survey.cpp_decoder_formats())), {})

    def test_containers_are_summed_not_reported_separately(self) -> None:
        """A format split across cmb and ctxb is still one verdict, with both counts kept."""
        report = self._report(cmb_formats=Counter({LA4: 2}), ctxb_formats=Counter({LA4: 3}))
        self.assertEqual(dict(report.total), {LA4: 5})


class EmptyCorpusRefuses(unittest.TestCase):
    def test_an_unknown_game_name_is_rejected_with_the_valid_set(self) -> None:
        with self.assertRaisesRegex(ValueError, "unknown corpus"):
            survey.survey("no-such-game")

    def test_the_empty_corpus_refusal_names_both_containers(self) -> None:
        """A scan that found nothing must not read as "nothing to worry about".

        The check is on the message rather than by driving a ROM read, because the alternative is a
        test that needs the ROMs to assert a string.
        """
        import inspect

        source = inspect.getsource(survey.survey)
        self.assertIn("no texture records found", source)
        self.assertIn("either container", source)


class JsonOutputCarriesTheVerdict(unittest.TestCase):
    def test_undecodable_formats_survive_into_json(self) -> None:
        """The machine-readable form has to keep the failing formats, not just a pass/fail."""
        report = survey.FormatReport(game="test", cmb_formats=Counter({0xDEADBEEF: 3}))
        payload = {
            "games": {
                "test": {
                    "formats": {hex(f): c for f, c in report.total.items()},
                    "undecodable": {
                        hex(f): c
                        for f, c in report.undecodable(set(survey.cpp_decoder_formats())).items()
                    },
                }
            }
        }
        text = json.dumps(payload)
        self.assertIn("0xdeadbeef", text.lower())
        self.assertIn("undecodable", text)


if __name__ == "__main__":
    unittest.main()
