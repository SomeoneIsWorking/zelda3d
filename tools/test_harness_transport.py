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
    def test_single_line_ok_ends_after_peek_timeout(self) -> None:
        reader = ResponseReader(["ok 123", None])

        result = _read_streaming_response(reader, "r32 0x1000", 30.0, 0.2)

        self.assertEqual(result, ["ok 123"])
        self.assertEqual(reader.timeouts, [30.0, 0.2])

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

    def test_missing_stream_terminator_is_an_error(self) -> None:
        reader = ResponseReader(["ok actors 1", "actor 0", None])

        with self.assertRaisesRegex(TimeoutError, "got 2 lines"):
            _read_streaming_response(reader, "actors", 30.0, 0.2)


if __name__ == "__main__":
    unittest.main()
