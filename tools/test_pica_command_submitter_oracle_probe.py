from __future__ import annotations

import unittest

from pica_command_submitter_oracle_probe import (
    match_submit_record,
    parse_submit_records,
)


class ParseSubmitRecordsTests(unittest.TestCase):
    def test_reads_submitter_fields(self) -> None:
        line = (
            "CMDSUBMIT source=MMIO pc=0x00123456 lr=0x00654321 listVa=0x00000000 "
            "listPa=0x20480000 size=640 mmio=0x1ef00018 r0=0x00000001 r1=0x00000002 "
            "r2=0x00000003 r3=0x00000004 sp=0x0ffff000"
        )
        self.assertEqual(
            parse_submit_records([line]),
            [
                {
                    "source": "MMIO",
                    "pc": 0x00123456,
                    "lr": 0x00654321,
                    "virtual_address": 0,
                    "physical_address": 0x20480000,
                    "size": 640,
                    "mmio_address": 0x1EF00018,
                    "r0": 1,
                    "r1": 2,
                    "r2": 3,
                    "r3": 4,
                    "sp": 0x0FFFF000,
                }
            ],
        )

    def test_matches_same_repeated_submission(self) -> None:
        record = {"physical_address": 0x20480000, "pc": 0x12345678, "size": 640}
        self.assertEqual(match_submit_record([record, record], 0x20480000, 640), record)

    def test_rejects_conflicting_submissions(self) -> None:
        records = [
            {"physical_address": 0x20480000, "pc": 0x12345678, "size": 640},
            {"physical_address": 0x20480000, "pc": 0x87654321, "size": 640},
        ]
        with self.assertRaisesRegex(RuntimeError, "2 distinct records"):
            match_submit_record(records, 0x20480000, 640)

    def test_rejects_missing_list(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "no 640-byte record"):
            match_submit_record([], 0x20480000, 640)

    def test_splits_literal_newline_from_older_cached_logs(self) -> None:
        line = (
            "# header\\nCMDSUBMIT source=GSP pc=0x00123456 lr=0x00654321 listVa=0x14480000 "
            "listPa=0x20480000 size=640 mmio=0x00000000 r0=0x00000001 r1=0x00000002 "
            "r2=0x00000003 r3=0x00000004 sp=0x0ffff000\\n"
        )
        self.assertEqual(parse_submit_records([line])[0]["physical_address"], 0x20480000)
