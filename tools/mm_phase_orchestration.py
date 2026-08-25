"""Tour sequencing over the shipping MM runtime, report, and artifact owners."""

from __future__ import annotations

import time
from pathlib import Path

from mm_phase_artifacts import PhaseArtifactStore
from mm_phase_catalog import Scene
from mm_phase_report import PhaseReport, validate_phase_log
from mm_phase_session import LiveMM
from mm_runtime_lifecycle import MMRuntime


def run_tour(
    scenes: tuple[Scene, ...],
    dwell: float,
    load_timeout: float,
    command_timeout: float,
    output: Path,
) -> PhaseReport:
    artifacts = PhaseArtifactStore.under_scratch(output)
    baseline = artifacts.load_phase_mode_baseline()
    transcript: list[dict[str, object]] = []
    report: PhaseReport | None = None
    error: str | None = None
    log_text = ""
    runtime = MMRuntime()

    with runtime.lease(blocking=False) as lease:
        live = LiveMM(runtime, lease, command_timeout)
        try:
            live.require_idle()
            start_reply = live.start(scenes[0].entrance)
            transcript.append(
                {"scene": scenes[0].name, "command": "start", "reply": start_reply}
            )
            for index, scene in enumerate(scenes):
                if index > 0:
                    transcript.append(
                        {
                            "scene": scene.name,
                            "command": "warp",
                            "reply": live.warp(scene),
                        }
                    )
                position = live.wait_for_scene(scene, load_timeout)
                actors = live.query("actors 0")
                transcript.append(
                    {
                        "scene": scene.name,
                        "entrance": hex(scene.entrance),
                        "expected_scene_id": scene.scene_id,
                        "posinfo": position,
                        "actors": actors,
                    }
                )
                print(
                    f"[{index + 1:02d}/{len(scenes):02d}] {scene.name:<24} "
                    f"entrance={hex(scene.entrance)} scene={scene.scene_id}"
                )
                time.sleep(dwell)
            transcript.append({"command": "quitteardown", "reply": live.quit()})
            log_text = runtime.paths.log.read_text(encoding="utf-8", errors="replace")
            report = validate_phase_log(log_text, phase_mode_baseline=baseline)
            return report
        except BaseException as exc:
            error = str(exc)
            live.cleanup()
            raise
        finally:
            if not log_text and runtime.paths.log.exists():
                log_text = runtime.paths.log.read_text(
                    encoding="utf-8", errors="replace"
                )
            artifacts.write(log_text, transcript, report, error, runtime.paths.log)
