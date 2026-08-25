#!/usr/bin/env python3
"""Drive and validate the deterministic 12-scene MM3D animation phase tour."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import fifo_rpc
import mm_phase_artifacts
import mm_phase_catalog
import mm_phase_orchestration
import mm_phase_report
import mm_phase_session
import mm_runtime_errors


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--list-scenes", action="store_true", help="print the deterministic scene set"
    )
    parser.add_argument(
        "--scenes", help="comma-separated subset of the named deterministic scenes"
    )
    parser.add_argument(
        "--dwell",
        type=float,
        default=8.0,
        help="seconds sampled per scene (default: 8)",
    )
    parser.add_argument(
        "--load-timeout",
        type=float,
        default=45.0,
        help="seconds allowed for each exact scene transition (default: 45)",
    )
    parser.add_argument(
        "--command-timeout",
        type=float,
        default=15.0,
        help="seconds allowed for one correlated MM command (default: 15)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=mm_phase_artifacts.DEFAULT_OUTPUT,
        help="artifact directory (default: scratch/mm_phase_tour)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.list_scenes:
        for scene in mm_phase_catalog.SCENES:
            print(
                f"{scene.name:<24} entrance={hex(scene.entrance):>6} "
                f"scene_id={scene.scene_id}"
            )
        return 0
    if args.dwell <= 0 or args.load_timeout <= 0 or args.command_timeout <= 0:
        print("FAIL: dwell and timeouts must be positive", file=sys.stderr)
        return 2
    try:
        scenes = mm_phase_catalog.select_scenes(args.scenes)
        report = mm_phase_orchestration.run_tour(
            scenes, args.dwell, args.load_timeout, args.command_timeout, args.output
        )
    except (
        fifo_rpc.FifoRpcError,
        mm_phase_artifacts.PhaseArtifactError,
        mm_phase_report.PhaseReportError,
        mm_phase_session.PhaseSessionError,
        mm_runtime_errors.RuntimeBusy,
        mm_runtime_errors.RuntimeErrorBase,
        mm_phase_catalog.SceneSelectionError,
        subprocess.TimeoutExpired,
    ) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(
        f"PASS: {len(scenes)} deterministic scenes, {report.pair_count} live "
        f"(model,clip) pairs, {len(report.moved_pairs)} moved, 0 unmapped, "
        f"0 static sufficiently-sampled clips; morph samples={report.morph_samples}"
    )
    print(f"artifacts: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
