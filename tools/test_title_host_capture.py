from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import title_host_capture


class FakeHarness:
    def __init__(self, responses: dict[str, str | list[str]]):
        self.responses = responses
        self.commands: list[str] = []

    def send(self, command: str) -> str:
        self.commands.append(command)
        response = self.responses[command]
        if isinstance(response, list):
            return response.pop(0)
        return response


class FakeCache:
    key = "test-key"

    def __init__(self, frames: dict[int, Path]):
        self.frames = frames

    def get_frame(self, frame: int) -> Path | None:
        return self.frames.get(frame)


class TitleHostCaptureTests(unittest.TestCase):
    def test_oracle_frame_uses_recovered_title_clock(self) -> None:
        with patch.dict(
            title_host_capture.oracle_frame_for_title_cs.__globals__,
            {"initial_title_cs": lambda: 88},
        ):
            self.assertEqual(title_host_capture.oracle_frame_for_title_cs(464), 752)
            self.assertEqual(title_host_capture.oracle_frame_for_title_cs(1093), 2010)
            with self.assertRaises(ValueError):
                title_host_capture.oracle_frame_for_title_cs(87)

    def test_boot_selects_renderer_before_title_frames(self) -> None:
        harness = FakeHarness(
            {
                "soh_boot": "ok soh_boot",
                "soh_unified 1": "ok soh_unified 1",
                "soh_step 240": "ok soh_step 240",
                "soh_camera": "ok soh_camera live=1 eye=(0,0,0)",
                "soh_titlecs": "ok soh_titlecs frame=4 end=2400",
            }
        )
        self.assertEqual(title_host_capture.boot_host_title(harness, 1), 4)
        self.assertEqual(
            harness.commands,
            ["soh_boot", "soh_unified 1", "soh_step 240", "soh_camera", "soh_titlecs"],
        )

    def test_advances_naturally_and_reads_half_rate_cursor(self) -> None:
        harness = FakeHarness(
            {
                "soh_step 2": "ok soh_step 2",
                "soh_step 1": ["ok soh_step 1", "ok soh_step 1"],
                "soh_titlecs": [
                    "ok soh_titlecs frame=1092 end=2400",
                    "ok soh_titlecs frame=1092 end=2400",
                    "ok soh_titlecs frame=1093 end=2400",
                ],
            }
        )
        observed = title_host_capture.advance_host_title(harness, 1091, 1093)
        self.assertEqual(observed, 1093)
        self.assertEqual(
            harness.commands,
            [
                "soh_step 2",
                "soh_titlecs",
                "soh_step 1",
                "soh_titlecs",
                "soh_step 1",
                "soh_titlecs",
            ],
        )

    def test_rejects_cursor_overshoot(self) -> None:
        harness = FakeHarness(
            {
                "soh_step 2": "ok soh_step 2",
                "soh_titlecs": "ok soh_titlecs frame=1094 end=2400",
            }
        )
        with self.assertRaisesRegex(RuntimeError, "cursor mismatch"):
            title_host_capture.advance_host_title(harness, 1091, 1093)

    def test_draw_list_uses_second_half_rate_tick_without_changing_cursor(self) -> None:
        harness = FakeHarness(
            {
                "soh_drawlist": "ok soh_drawlist armed",
                "soh_step 1": "ok soh_step 1",
                "soh_titlecs": "ok soh_titlecs frame=1093 end=2400",
            }
        )
        title_host_capture.arm_host_draw_list(harness, 1093)
        self.assertEqual(
            harness.commands,
            ["soh_drawlist", "soh_step 1", "soh_titlecs"],
        )

    def test_cache_miss_refuses_to_launch_oracle_work(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            frame = Path(directory) / "az752.png"
            frame.touch()
            cache = FakeCache({752: frame})
            with patch.dict(
                title_host_capture.oracle_frame_for_title_cs.__globals__,
                {"initial_title_cs": lambda: 88},
            ):
                with self.assertRaisesRegex(RuntimeError, "cs=1093->az=2010"):
                    title_host_capture.require_cached_oracle_frames(cache, [464, 1093])


if __name__ == "__main__":
    unittest.main()
