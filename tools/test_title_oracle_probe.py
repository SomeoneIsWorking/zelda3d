from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import title_oracle_probe


class FakeHarness:
    def __init__(self, responses: dict[str, str]):
        self.responses = responses
        self.commands: list[str] = []

    def send(self, command: str) -> str:
        self.commands.append(command)
        return self.responses[command]


class FakeCache:
    def __init__(self, artifact: Path | None):
        self.artifact = artifact

    def get_artifact(self, _name: str, _args: dict[str, int]) -> Path | None:
        return self.artifact


class CheckpointCache:
    def __init__(self, artifacts: dict[int, Path]):
        self.artifacts = artifacts

    def get_artifact(self, _name: str, args: dict[str, int]) -> Path | None:
        return self.artifacts.get(args["oracle_frame"])


class TitleOracleProbeTests(unittest.TestCase):
    def test_artifact_identity_includes_exact_cursor_frame_and_draw(self) -> None:
        self.assertEqual(
            title_oracle_probe.artifact_args(1093, draw=17),
            {
                "capture_version": 2,
                "title_cs": 1093,
                "oracle_frame": 2010,
                "software_renderer": 1,
                "draw": 17,
            },
        )

    def test_dual_texture_draw_filter_preserves_full_identity_line(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "vsuni.log"
            dual = "draw n=7 idx=1 tex0=1234/8x8/f0 texEn=1/1/0 tev1..5=abc:8:1\n"
            path.write_text(
                "draw n=6 idx=1 tex0=1234/8x8/f0 texEn=1/0/0 tev1..5=abc:8:1\n" + dual
            )
            self.assertEqual(
                title_oracle_probe.dual_texture_draws(path), [(7, dual.rstrip())]
            )

    def test_uniform_cache_hit_never_spawns_oracle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "cached.log"
            artifact.write_text("")
            with mock.patch.object(
                title_oracle_probe, "spawn", side_effect=AssertionError("spawned")
            ):
                path, hit = title_oracle_probe.capture_uniforms(
                    FakeCache(artifact), 464
                )
            self.assertEqual(path, artifact)
            self.assertTrue(hit)

    def test_oracle_steps_are_chunked_and_checked(self) -> None:
        harness = FakeHarness(
            {
                "run 25": "ok run 25",
                "run 1": "ok run 1",
            }
        )
        title_oracle_probe.step_oracle(harness, 51)
        self.assertEqual(harness.commands, ["run 25", "run 25", "run 1"])

    def test_latest_checkpoint_is_reusable_and_leaves_post_load_warmup(self) -> None:
        checkpoint_1600 = Path("checkpoint-1600.state")
        checkpoint_2000 = Path("checkpoint-2000.state")
        cache = CheckpointCache({1600: checkpoint_1600, 2000: checkpoint_2000})
        self.assertEqual(
            title_oracle_probe.latest_checkpoint(cache, 2008),
            (2000, checkpoint_2000),
        )
        self.assertEqual(
            title_oracle_probe.latest_checkpoint(cache, 2002),
            (1600, checkpoint_1600),
        )


if __name__ == "__main__":
    unittest.main()
