from __future__ import annotations

import unittest

from pica_command_provenance_oracle_probe import parse_provenance, provenance_records


class ParseProvenanceTests(unittest.TestCase):
    def test_reads_requested_draw(self) -> None:
        line = "draw n=4 idx=0 picaLit=1 cmdList=18000000/17/512 tex0=12345678/32x64/f13"
        self.assertEqual(
            parse_provenance([line], 4),
            {
                "draw": 4,
                "command_list_address": 0x18000000,
                "command_list_word_index": 17,
                "command_list_word_count": 512,
            },
        )

    def test_rejects_missing_draw(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "no command-list provenance"):
            parse_provenance(["draw n=3 idx=0 picaLit=1 cmdList=18000000/17/512"], 4)

    def test_rejects_duplicate_draw(self) -> None:
        line = "draw n=4 idx=0 picaLit=1 cmdList=18000000/17/512"
        with self.assertRaisesRegex(RuntimeError, "2 command-list records"):
            parse_provenance([line, line], 4)

    def test_returns_multiple_records_for_multiframe_validation(self) -> None:
        lines = [
            "draw n=4 idx=0 picaLit=1 cmdList=18000000/17/512",
            "draw n=4 idx=0 picaLit=1 cmdList=18001000/19/512",
        ]
        self.assertEqual(
            [record["command_list_address"] for record in provenance_records(lines, 4)],
            [0x18000000, 0x18001000],
        )
