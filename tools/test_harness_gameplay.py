"""Focused unit tests for gameplay boot and warp policy."""

from __future__ import annotations

import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from harness_gameplay import boot_to_gameplay, set_time_of_day

SCRATCH_TESTS = Path(__file__).resolve().parents[1] / "scratch" / "tests"
SCRATCH_TESTS.mkdir(parents=True, exist_ok=True)


class FakeHarness:
    def __init__(self, responses: dict[str, list[str] | str]):
        self.responses = {
            command: list(value) if isinstance(value, list) else [value]
            for command, value in responses.items()
        }
        self.commands: list[str] = []
        self.timeouts: list[tuple[str, float]] = []

    def send(self, command: str, *, per_line_timeout: float = 60.0) -> str:
        self.commands.append(command)
        self.timeouts.append((command, per_line_timeout))
        values = self.responses.get(command, ["ok"])
        if len(values) > 1:
            return values.pop(0)
        return values[0]


class BootToGameplayTests(unittest.TestCase):
    def test_cached_state_must_reach_gameplay_before_warp(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            state = Path(directory) / "gameplay.state"
            state.write_bytes(b"state")
            harness = FakeHarness({"gameplay": "ok gameplay no"})

            with redirect_stderr(StringIO()):
                result = boot_to_gameplay(harness, entrance=0xEE, gameplay_state=state)

        self.assertFalse(result)
        self.assertNotIn("warp 0xee", harness.commands)

    def test_cached_state_warps_and_settles_in_fixed_chunks(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            state = Path(directory) / "gameplay.state"
            state.write_bytes(b"state")
            harness = FakeHarness({"gameplay": ["ok gameplay yes", "ok gameplay yes"]})

            result = boot_to_gameplay(
                harness,
                entrance=0xEE,
                settle_frames=180,
                gameplay_state=state,
            )

        self.assertTrue(result)
        self.assertEqual(harness.commands.count("run 60"), 4)
        self.assertIn("warp 0xee", harness.commands)

    def test_cold_boot_captures_state_once_gameplay_is_real(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            state = Path(directory) / "nested" / "gameplay.state"
            harness = FakeHarness({"gameplay": "ok gameplay yes"})

            with redirect_stderr(StringIO()):
                result = boot_to_gameplay(harness, gameplay_state=state)

        self.assertTrue(result)
        self.assertIn("run 300", harness.commands)
        self.assertIn(("run 300", 180.0), harness.timeouts)
        self.assertIn(f"savestate {state}", harness.commands)

    def test_failed_warp_is_reported_without_settling(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            state = Path(directory) / "gameplay.state"
            state.write_bytes(b"state")
            harness = FakeHarness(
                {
                    "gameplay": "ok gameplay yes",
                    "warp 0x305": "err warp rejected",
                }
            )

            with redirect_stderr(StringIO()):
                result = boot_to_gameplay(harness, entrance=0x305, gameplay_state=state)

        self.assertFalse(result)
        self.assertEqual(harness.commands.count("run 60"), 1)

    def test_settle_frames_preserves_non_multiple_of_sixty(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            state = Path(directory) / "gameplay.state"
            state.write_bytes(b"state")
            harness = FakeHarness({"gameplay": ["ok gameplay yes", "ok gameplay yes"]})

            self.assertTrue(
                boot_to_gameplay(
                    harness,
                    entrance=0xEE,
                    settle_frames=61,
                    gameplay_state=state,
                )
            )

        self.assertEqual(harness.commands[-4:], ["warp 0xee", "run 60", "run 1", "gameplay"])

    def test_zero_settle_frames_does_not_advance_after_warp(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            state = Path(directory) / "gameplay.state"
            state.write_bytes(b"state")
            harness = FakeHarness({"gameplay": ["ok gameplay yes", "ok gameplay yes"]})

            self.assertTrue(
                boot_to_gameplay(
                    harness,
                    entrance=0xEE,
                    settle_frames=0,
                    gameplay_state=state,
                )
            )

        self.assertEqual(harness.commands[-2:], ["warp 0xee", "gameplay"])

    def test_time_of_day_drift_is_circular_at_u16_wrap(self) -> None:
        harness = FakeHarness(
            {
                "r16 0x00587964": "ok 0x0002",
                "r16 0x00588f00": "ok 0x0003",
            }
        )

        set_time_of_day(harness, 0xFFFE, settle=0, tolerance=5)

        self.assertNotIn("run 0", harness.commands)

    def test_time_of_day_rejects_out_of_range_clock(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsigned 16-bit"):
            set_time_of_day(FakeHarness({}), 0x10000)


if __name__ == "__main__":
    unittest.main()
