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

    # The reply shape the real harness prints (environment_probe_commands.cpp HandleZelda3dFog).
    # `on=` is the RESULTING state, which is only meaningful after a frame has run — the latch is
    # read by Zelda3D_Fog3dSet once per rendered frame, so the reply before soh_step still says
    # on=0 for a request to turn it on.
    FOG_ON = "ok soh_fog3d forceOff=0 on=1 a=1.000584 b=7.0041 near=800.0 far=2400.0"
    FOG_OFF = "ok soh_fog3d forceOff=1 on=0 a=0.000000 b=0.0000 near=0.0 far=0.0"

    def test_boot_selects_renderer_before_title_frames(self) -> None:
        harness = FakeHarness(
            {
                "soh_boot": "ok soh_boot",
                "soh_unified 1": "ok soh_unified 1",
                "soh_fog3d 1": "ok soh_fog3d forceOff=0 on=0 a=0.0 b=0.0 near=0.0 far=0.0",
                "soh_step 240": "ok soh_step 240",
                "soh_fog3d": self.FOG_ON,
                "soh_camera": "ok soh_camera live=1 eye=(0,0,0)",
                "soh_titlecs": "ok soh_titlecs frame=4 end=2400",
            }
        )
        self.assertEqual(title_host_capture.boot_host_title(harness, 1), 4)
        self.assertEqual(
            harness.commands,
            [
                "soh_boot",
                "soh_unified 1",
                "soh_fog3d 1",
                "soh_step 240",
                "soh_fog3d",
                "soh_camera",
                "soh_titlecs",
            ],
        )

    def test_a_fog_a_b_that_claims_off_but_reports_on_is_refused(self) -> None:
        """A capture labelled "fog off" is worthless if the fog was on for another reason.

        The fog can be off because it was latched off, or because nothing was fogged (no fogged
        material, a degenerate window, the sky exclusion) — and only the first isolates anything. So
        the resulting state is read back after a frame and a disagreement is an error, not a run.
        This is the check that stops a fog A/B from silently measuring nothing.
        """
        harness = FakeHarness(
            {
                "soh_boot": "ok soh_boot",
                "soh_unified 0": "ok soh_unified 0",
                "soh_fog3d 0": "ok soh_fog3d forceOff=1 on=1 a=1.0 b=7.0 near=800.0 far=2400.0",
                "soh_step 240": "ok soh_step 240",
                "soh_fog3d": self.FOG_ON,  # the latch did not take
                "soh_camera": "ok soh_camera live=1 eye=(0,0,0)",
                "soh_titlecs": "ok soh_titlecs frame=4 end=2400",
            }
        )
        with self.assertRaisesRegex(RuntimeError, "fog latch did not take effect"):
            title_host_capture.boot_host_title(harness, 0, 0)

    def test_fog_off_is_verified_after_the_frame_not_before(self) -> None:
        """Reading the latch reply BEFORE the step would always report on=0 and reject fog=on."""
        harness = FakeHarness(
            {
                "soh_boot": "ok soh_boot",
                "soh_unified 0": "ok soh_unified 0",
                "soh_fog3d 0": "ok soh_fog3d forceOff=1 on=1 a=1.0 b=7.0 near=800.0 far=2400.0",
                "soh_step 240": "ok soh_step 240",
                "soh_fog3d": self.FOG_OFF,
                "soh_camera": "ok soh_camera live=1 eye=(0,0,0)",
                "soh_titlecs": "ok soh_titlecs frame=4 end=2400",
            }
        )
        self.assertEqual(title_host_capture.boot_host_title(harness, 0, 0), 4)
        self.assertLess(
            harness.commands.index("soh_step 240"),
            harness.commands.index("soh_fog3d"),
            "the resulting fog state was read before any frame ran",
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

    def test_draw_list_is_published_after_the_image_not_before(self) -> None:
        """The comparison image must be the requested cursor's frame, not one frame past it.

        One frame of this title is worth ~5.9 mean-abs over 14% of the frame, so arming the draw
        list first silently compared a different frame against the same cached oracle pair.
        """
        responses = {
            "soh_titlecs": "ok soh_titlecs frame=1093 end=2400",
            "soh_snapshot /tmp/x": "ok soh_snapshot 800x480",
            "soh_drawlist": "ok soh_drawlist armed",
            "soh_step 1": "ok soh_step 1",
        }

        class SnapshotHarness(FakeHarness):
            def send(self, command: str) -> str:
                self.commands.append(command)
                return responses.get(command, "ok")

        with patch.object(title_host_capture, "ppm_to_png", lambda path: Path(path)):
            harness = SnapshotHarness({})
            title_host_capture.capture_cursor_image(harness, 1093, Path("/tmp/x"), True)
        self.assertLess(
            harness.commands.index("soh_snapshot /tmp/x"),
            harness.commands.index("soh_drawlist"),
            "draw list was armed before the image, so the image is one frame past the cursor",
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
