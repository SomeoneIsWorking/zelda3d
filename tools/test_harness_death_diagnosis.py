#!/usr/bin/env python3
"""Pin the harness-death diagnosis, so a product crash cannot hide as a transport error.

A closed stdout pipe is what a *crashed* harness looks like from the outside. The
harness writes a fatal-signal backtrace to its stderr log, and that backtrace is
the only place the frame that actually died is recorded. When the report said just
"harness closed stdout unexpectedly", every title/parity capture that hit a real
product crash looked like a flaky tool.

These tests pin the three shapes: a fatal-signal log, a log with no marker, and
no log at all. The negative control matters because a diagnosis that always
returns the same reassuring sentence is worse than none.
"""

from __future__ import annotations

import contextlib
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import harness_transport


@contextlib.contextmanager
def _log(text: str):
    """Point HARNESS_STDERR at a temp log holding `text` for the block."""
    handle = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    handle.write(text)
    handle.close()
    try:
        with mock.patch.dict(os.environ, {"HARNESS_STDERR": handle.name}):
            yield handle.name
    finally:
        os.unlink(handle.name)


def _closed_stdout_message() -> str:
    """Reproduce the exact message `_readline` raises when the stdout pipe closes."""
    class _ClosedStdout:
        """A pipe that is readable and immediately at EOF, like a crashed child."""

        def fileno(self) -> int:
            return 0

        def read(self, _size: int) -> bytes:
            return b""

    harness = harness_transport.Harness.__new__(harness_transport.Harness)
    harness._buf = b""
    harness.proc = type("Proc", (), {"stdout": _ClosedStdout()})()
    with mock.patch("select.select", return_value=([0], [], [])), \
         mock.patch("os.read", return_value=b""):
        try:
            harness._readline(timeout=0.01)
        except RuntimeError as error:
            return str(error)
    raise AssertionError("a closed stdout pipe must raise")


class HarnessDeathDiagnosisTests(unittest.TestCase):
    def tearDown(self) -> None:
        harness_transport.LOG_TAIL_BYTES = 256 * 1024

    def test_fatal_signal_log_names_the_log_and_the_top_frames(self) -> None:
        text = ("[zelda3d] FATAL signal — async-signal-safe backtrace:\n"
                "/usr/lib64/libvulkan_lvp.so(+0xd2101)\n"
                "/usr/lib64/libSDL3.so.0(+0x22979b)\n"
                "libultraship.so(Fast::Zelda3DRenderer::getUnifiedPipeline+0x79B)\n")
        with _log(text) as path:
            diagnosis = harness_transport._log_diagnosis()
        self.assertIn("fatal signal", diagnosis)
        self.assertIn(path, diagnosis)
        self.assertIn("libvulkan_lvp.so", diagnosis)
        self.assertIn("getUnifiedPipeline", diagnosis)

    def test_a_log_without_the_marker_still_points_at_the_log(self) -> None:
        with _log("[harness-vk] Vulkan frontend ready\nScene: SCENE_TITLE\n") as path:
            diagnosis = harness_transport._log_diagnosis()
        self.assertIn("no fatal-signal marker", diagnosis)
        self.assertIn(path, diagnosis)

    def test_no_log_configured_yields_no_claim(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(harness_transport._log_diagnosis(), "")

    def test_a_missing_log_file_is_not_an_error(self) -> None:
        with mock.patch.dict(os.environ, {"HARNESS_STDERR": "/nonexistent/harness.log"}):
            self.assertEqual(harness_transport._log_diagnosis(), "")

    def test_only_the_tail_is_read(self) -> None:
        # A log far larger than the window must still be diagnosed, and the window
        # must stay small enough that a huge log cannot be slurped on every call.
        original = harness_transport.LOG_TAIL_BYTES
        harness_transport.LOG_TAIL_BYTES = 4096
        try:
            text = "filler line\n" * 20000 + "[zelda3d] FATAL signal:\n/usr/lib/lvp.so(+0x1)\n"
            with _log(text):
                self.assertIn("fatal signal", harness_transport._log_diagnosis())
        finally:
            harness_transport.LOG_TAIL_BYTES = original
        self.assertLessEqual(original, 1024 * 1024)

    def test_transport_error_carries_the_diagnosis(self) -> None:
        with mock.patch.object(harness_transport, "_log_diagnosis",
                               return_value="harness logged a fatal signal"):
            message = _closed_stdout_message()
        self.assertIn("harness closed stdout unexpectedly", message)
        self.assertIn("logged a fatal signal", message)

    def test_transport_error_without_a_log_stays_bare(self) -> None:
        with mock.patch.object(harness_transport, "_log_diagnosis", return_value=""):
            self.assertEqual(_closed_stdout_message(), "harness closed stdout unexpectedly")


if __name__ == "__main__":
    unittest.main()
