from __future__ import annotations

import unittest

from oracle_pcwatch_probe import capture_frame, parse_pc_hits, probe_args


class OraclePcwatchProbeTests(unittest.TestCase):
    def test_probe_arguments_include_the_full_fixture_identity(self) -> None:
        self.assertEqual(
            probe_args(0x4093F8, "pica-command-append", 0xCD, 0x6000, 180),
            {
                "capture_version": 1,
                "function": 0x4093F8,
                "label": "pica-command-append",
                "entrance": 0xCD,
                "daytime": 0x6000,
                "settle_frames": 180,
                "time_settle_frames": 8,
                "trace_run_frames": 2,
                "texture_pack": 0,
            },
        )
        self.assertEqual(capture_frame(180), 190)

    def test_parse_pc_hits_rejects_empty_or_incomplete_responses(self) -> None:
        self.assertEqual(parse_pc_hits(["ok pchits 1", "pc=0x004093f8", "ok end"]), ["pc=0x004093f8"])
        with self.assertRaisesRegex(RuntimeError, "not reached"):
            parse_pc_hits(["ok pchits 0", "ok end"])
        with self.assertRaisesRegex(RuntimeError, "malformed"):
            parse_pc_hits(["ok pchits 1", "pc=0x004093f8"])


if __name__ == "__main__":
    unittest.main()
