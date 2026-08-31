"""Focused unit tests for harness REPL response framing."""

from __future__ import annotations

import sys
import unittest
from collections import deque
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from harness_transport import _read_streaming_response


class ResponseReader:
    def __init__(self, lines: list[str | None]):
        self.lines = deque(lines)
        self.timeouts: list[float] = []

    def __call__(self, timeout: float) -> str | None:
        self.timeouts.append(timeout)
        return self.lines.popleft()


class StreamingResponseTests(unittest.TestCase):
    def test_bare_ok_is_terminal(self) -> None:
        reader = ResponseReader(["ok"])

        result = _read_streaming_response(reader, "input 0x100", 30.0, 0.2)

        self.assertEqual(result, ["ok"])
        self.assertEqual(reader.timeouts, [30.0])

    def test_single_line_ok_is_terminal(self) -> None:
        reader = ResponseReader(["ok 0x00000123"])

        result = _read_streaming_response(reader, "r32 0x1000", 30.0, 0.2)

        self.assertEqual(result, ["ok 0x00000123"])
        self.assertEqual(reader.timeouts, [30.0])

    def test_lighting_capture_ack_keeps_draw_and_path(self) -> None:
        reader = ResponseReader(["ok lighting_capture 4 scratch/lighting.json"])

        result = _read_streaming_response(
            reader, "lighting_capture 4 scratch/lighting.json", 30.0, 0.2
        )

        self.assertEqual(result, ["ok lighting_capture 4 scratch/lighting.json"])
        self.assertEqual(reader.timeouts, [30.0])

    def test_single_line_reply_does_not_peek_into_async_output(self) -> None:
        reader = ResponseReader(["ok soh_input 0x8000 stick=(0,0)", "async diagnostic"])

        result = _read_streaming_response(reader, "soh_input 0x8000 0 0", 30.0, 0.2)

        self.assertEqual(result, ["ok soh_input 0x8000 stick=(0,0)"])
        self.assertEqual(list(reader.lines), ["async diagnostic"])

    def test_async_output_before_one_line_reply_is_consumed(self) -> None:
        reader = ResponseReader(["game diagnostic", "ok run 30"])

        result = _read_streaming_response(reader, "run 30", 30.0, 0.2)

        self.assertEqual(result, ["game diagnostic", "ok run 30"])
        self.assertEqual(reader.timeouts, [30.0, 30.0])

    def test_prior_named_ack_is_not_current_named_ack(self) -> None:
        reader = ResponseReader(["ok run 60", "ok soh_input 0x8000 stick=(0,0)"])

        result = _read_streaming_response(reader, "soh_input 0x8000 0 0", 30.0, 0.2)

        self.assertEqual(result, ["ok run 60", "ok soh_input 0x8000 stick=(0,0)"])

    def test_prior_named_ack_is_not_bare_ack(self) -> None:
        reader = ResponseReader(["ok run 30", "ok"])

        result = _read_streaming_response(reader, "input 0", 30.0, 0.2)

        self.assertEqual(result, ["ok run 30", "ok"])

    def test_prior_named_ack_is_not_hex_ack(self) -> None:
        reader = ResponseReader(["ok force bossfd2_ground", "ok 0x3f800000"])

        result = _read_streaming_response(reader, "r32 0x0990c768", 30.0, 0.2)

        self.assertEqual(result, ["ok force bossfd2_ground", "ok 0x3f800000"])

    def test_hex_reply_width_must_match_command(self) -> None:
        reader = ResponseReader(["ok 0x1234", "ok 0x00001234"])

        result = _read_streaming_response(reader, "r32 0x0990c768", 30.0, 0.2)

        self.assertEqual(result, ["ok 0x1234", "ok 0x00001234"])

    def test_playstate_requires_its_mode_label(self) -> None:
        reader = ResponseReader(["ok 0x09900000", "ok 0x09900000 mode=play"])

        result = _read_streaming_response(reader, "playstate", 30.0, 0.2)

        self.assertEqual(result, ["ok 0x09900000", "ok 0x09900000 mode=play"])

    def test_force_subcommands_do_not_cross_match(self) -> None:
        reader = ResponseReader(
            [
                "ok force bossfd2_ground oracle=0x0990c440",
                "ok force camera eye=(1,2,3)",
            ]
        )

        result = _read_streaming_response(
            reader, "force camera 1 2 3 4 5 6 45", 30.0, 0.2
        )

        self.assertEqual(
            result,
            [
                "ok force bossfd2_ground oracle=0x0990c440",
                "ok force camera eye=(1,2,3)",
            ],
        )

    def test_zero_length_memory_reply_is_bare_ok(self) -> None:
        reader = ResponseReader(["ok"])

        result = _read_streaming_response(reader, "mem 0x1000 0", 30.0, 0.2)

        self.assertEqual(result, ["ok"])

    def test_counted_stream_ends_at_ok_end(self) -> None:
        reader = ResponseReader(["ok actors 1", "actor 0", "ok end"])

        result = _read_streaming_response(reader, "actors", 30.0, 0.2)

        self.assertEqual(result, ["ok actors 1", "actor 0", "ok end"])

    def test_labeled_stream_ends_at_named_ok(self) -> None:
        reader = ResponseReader(["compare bossfd:", "samples=150", "ok compare bossfd"])

        result = _read_streaming_response(reader, "compare bossfd", 30.0, 0.2)

        self.assertEqual(
            result, ["compare bossfd:", "samples=150", "ok compare bossfd"]
        )

    def test_labeled_stream_preserves_error_terminator(self) -> None:
        reader = ResponseReader(
            ["compare bossfd:", "err compare bossfd verdict=MISSING"]
        )

        result = _read_streaming_response(reader, "compare bossfd", 30.0, 0.2)

        self.assertEqual(
            result, ["compare bossfd:", "err compare bossfd verdict=MISSING"]
        )

    def test_first_line_error_is_already_terminal(self) -> None:
        reader = ResponseReader(["err unknown command"])

        result = _read_streaming_response(reader, "compare nope", 30.0, 0.2)

        self.assertEqual(result, ["err unknown command"])
        self.assertEqual(reader.timeouts, [30.0])

    def test_no_first_line_is_an_error(self) -> None:
        reader = ResponseReader([None])

        with self.assertRaisesRegex(TimeoutError, "got 0 lines"):
            _read_streaming_response(reader, "scene", 30.0, 0.2)

    def test_missing_stream_terminator_is_an_error(self) -> None:
        reader = ResponseReader(["ok actors 1", "actor 0", None])

        with self.assertRaisesRegex(TimeoutError, "got 2 lines"):
            _read_streaming_response(reader, "actors", 30.0, 0.2)


if __name__ == "__main__":
    unittest.main()
