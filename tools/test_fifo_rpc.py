#!/usr/bin/env python3
"""Protocol falsifiers for correlated Zelda3D FIFO requests."""

from __future__ import annotations

import os
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from fifo_rpc import FifoRpcClient, FifoRpcError, TaggedReplyCollector, _OutputCursor


class ReplyCollectorTests(unittest.TestCase):
    def test_ignores_interleaved_ids_and_validates_multiline_count(self) -> None:
        collector = TaggedReplyCollector("0123456789abcdef")
        collector.feed("startup greeting")
        collector.feed("@ffffffffffffffff D unrelated")
        collector.feed("@0123456789abcdef D first")
        collector.feed("@0123456789abcdef D second")
        collector.feed("@ffffffffffffffff E 1")
        collector.feed("@0123456789abcdef E 2")
        self.assertEqual(collector.text(), "first\nsecond")

    def test_accepts_explicit_zero_line_reply(self) -> None:
        collector = TaggedReplyCollector("0123456789abcdef")
        collector.feed("@0123456789abcdef E 0")
        self.assertEqual(collector.text(), "")

    def test_rejects_count_mismatch(self) -> None:
        collector = TaggedReplyCollector("0123456789abcdef")
        collector.feed("@0123456789abcdef D only")
        with self.assertRaisesRegex(FifoRpcError, "count mismatch"):
            collector.feed("@0123456789abcdef E 2")


class OutputCursorTests(unittest.TestCase):
    def test_detects_output_replacement_and_shrink(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            output = Path(directory) / "reply.out"
            output.write_text("old\n", encoding="utf-8")
            cursor = _OutputCursor.at_end(output)
            replacement = Path(directory) / "replacement"
            replacement.write_text("new\n", encoding="utf-8")
            replacement.replace(output)
            self.assertEqual(cursor.read_new_lines(), ("new",))
            self.assertTrue(cursor.disrupted)
            cursor.disrupted = False
            output.write_text("x\n", encoding="utf-8")
            self.assertEqual(cursor.read_new_lines(), ("x",))
            self.assertTrue(cursor.disrupted)


class EndToEndFifoTests(unittest.TestCase):
    def test_waits_for_delayed_correlated_multiline_reply(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            fifo = Path(directory) / "rpc.fifo"
            os.mkfifo(fifo)
            output = Path(f"{fifo}.out")
            output.write_text("legacy greeting\n", encoding="utf-8")
            ready = threading.Event()

            def server() -> None:
                descriptor = os.open(fifo, os.O_RDWR)
                ready.set()
                with os.fdopen(descriptor, "r", encoding="utf-8") as request_file:
                    request = request_file.readline().strip()
                request_id = request[1:17]
                time.sleep(0.5)
                with output.open("a", encoding="utf-8") as reply_file:
                    reply_file.write("@ffffffffffffffff D unrelated\n")
                    reply_file.write(f"@{request_id} D line one\n")
                    reply_file.write(f"@{request_id} D line two\n")
                    reply_file.write(f"@{request_id} E 2\n")

            thread = threading.Thread(target=server)
            thread.start()
            self.assertTrue(ready.wait(timeout=1.0))
            reply = FifoRpcClient(fifo, timeout=2.0).request("actors 0")
            thread.join()
            self.assertEqual(reply, "line one\nline two")


if __name__ == "__main__":
    unittest.main()
