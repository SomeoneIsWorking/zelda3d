from __future__ import annotations

import unittest

from pica_command_submitter_oracle_probe import (
    match_submit_record,
    parse_bulk_records,
    parse_bulk_selftest_records,
    parse_pointer_records,
    parse_submit_records,
)


class ParseSubmitRecordsTests(unittest.TestCase):
    def test_reads_submitter_fields(self) -> None:
        line = (
            "CMDSUBMIT source=MMIO pc=0x00123456 lr=0x00654321 listVa=0x00000000 "
            "listPa=0x20480000 size=640 mmio=0x1ef00018 r0=0x00000001 r1=0x00000002 "
            "r2=0x00000003 r3=0x00000004 sp=0x0ffff000 s0=0x00000005 s1=0x00000006 "
            "s2=0x00000007 s3=0x00000008 s4=0x00000009 s5=0x0000000a s6=0x0000000b s7=0x0000000c "
            "s8=0x0000000d s9=0x0000000e s10=0x0000000f s11=0x00000010 s12=0x00000011 s13=0x00000012 "
            "s14=0x00000013 s15=0x00000014 s16=0x00000015"
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
                    "s0": 5,
                    "s1": 6,
                    "s2": 7,
                    "s3": 8,
                    "s4": 9,
                    "s5": 10,
                    "s6": 11,
                    "s7": 12,
                    "s8": 13,
                    "s9": 14,
                    "s10": 15,
                    "s11": 16,
                    "s12": 17,
                    "s13": 18,
                    "s14": 19,
                    "s15": 20,
                    "s16": 21,
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
            "r2=0x00000003 r3=0x00000004 sp=0x0ffff000 s0=0x00000005 s1=0x00000006 "
            "s2=0x00000007 s3=0x00000008 s4=0x00000009 s5=0x0000000a s6=0x0000000b s7=0x0000000c "
            "s8=0x0000000d s9=0x0000000e s10=0x0000000f s11=0x00000010 s12=0x00000011 s13=0x00000012 "
            "s14=0x00000013 s15=0x00000014 s16=0x00000015\\n"
        )
        self.assertEqual(parse_submit_records([line])[0]["physical_address"], 0x20480000)

    def test_reads_only_exact_pointer_acquisitions(self) -> None:
        lines = [
            (
                "PTR pc=0x00123456 lr=0x00654321 va=0x1458fa80 r0=0x00000001 r1=0x00000002 "
                "r2=0x00000003 r3=0x00000004 sp=0x0ffff000"
            ),
            (
                "PTR pc=0x00876543 lr=0x00123456 va=0x1458fa84 r0=0x00000000 r1=0x00000000 "
                "r2=0x00000000 r3=0x00000000 sp=0x0ffff000"
            ),
        ]
        self.assertEqual(
            parse_pointer_records(lines, 0x1458FA80),
            [
                {
                    "pc": 0x00123456,
                    "lr": 0x00654321,
                    "virtual_address": 0x1458FA80,
                    "r0": 1,
                    "r1": 2,
                    "r2": 3,
                    "r3": 4,
                    "sp": 0x0FFFF000,
                }
            ],
        )

    def test_reads_bulk_write_that_overlaps_command_list(self) -> None:
        lines = [
            (
                "MB pc=0x00123456 lr=0x00654321 va=0x1458fa00 sz=512 r0=0x00000001 r1=0x00000002 "
                "r2=0x00000003 r3=0x00000004 sp=0x0ffff000"
            ),
            (
                "MB pc=0x00876543 lr=0x00123456 va=0x14590000 sz=32 r0=0x00000000 r1=0x00000000 "
                "r2=0x00000000 r3=0x00000000 sp=0x0ffff000"
            ),
        ]
        records = parse_bulk_records(lines, 0x1458FA80, 0x1458FB80)
        self.assertEqual([record["pc"] for record in records], [0x00123456])

    def test_accepts_new_bulk_selftest_record_only(self) -> None:
        before = ["unrelated log line"]
        after = before + [
            (
                "MB pc=0x00123456 lr=0x00654321 va=0x1458fa80 sz=16 r0=0x00000001 r1=0x00000002 "
                "r2=0x00000003 r3=0x00000004 sp=0x0ffff000"
            )
        ]
        records = parse_bulk_selftest_records(before, after, 0x1458FA80)
        self.assertEqual([record["size"] for record in records], [16])

    def test_rejects_bulk_selftest_without_new_matching_record(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "self-test produced no matching record"):
            parse_bulk_selftest_records([], [], 0x1458FA80)
