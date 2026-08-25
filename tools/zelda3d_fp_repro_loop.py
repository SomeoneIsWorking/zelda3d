#!/usr/bin/env python3
"""Repeat the real-GPU first-person early-load crash reproduction."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

from mm_process import find_game_processes, terminate_exact

REPO = Path(__file__).resolve().parent.parent
GPU_LAUNCH = REPO / "tools/zelda3d_gpu_launch.py"
SIGNATURE = re.compile(
    r"SOH3D MTX-NULL|SOH3D ARENA-CORRUPT|guMtxF2L|Matrix_ToMtx"
)


def _stop_existing(binary: Path) -> None:
    for process in find_game_processes(binary, "oot"):
        if not terminate_exact(process):
            raise RuntimeError(f"OoT process {process.pid} did not exit")


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if len(args) > 3:
        raise ValueError(
            "usage: zelda3d_fp_repro_loop.py [entrance] [attempts] [seconds_per_boot]"
        )
    entrance = args[0] if args else "389"
    attempts = int(args[1]) if len(args) > 1 else 20
    seconds = float(args[2]) if len(args) > 2 else 12.0
    binary = Path(
        os.environ.get(
            "ZELDA3D_SOH",
            str(REPO / "Shipwright/build-cmake/zelda3d/zelda3d"),
        )
    ).resolve()
    run_dir = REPO / "scratch/logs/fp_repro" / datetime.now().strftime(
        "run_%Y%m%d_%H%M%S"
    )
    run_dir.mkdir(parents=True, exist_ok=False)
    print(
        f"fp_repro_loop: entrance={entrance} attempts={attempts} "
        f"secs={seconds:g} -> {run_dir}"
    )
    environment = {**os.environ, "ZELDA3D_FP_REPRO": "1"}
    for attempt in range(1, attempts + 1):
        log_path = run_dir / f"attempt_{attempt:02d}.log"
        _stop_existing(binary)
        with log_path.open("wb") as log:
            try:
                completed = subprocess.run(
                    [sys.executable, str(GPU_LAUNCH), entrance],
                    cwd=REPO,
                    env=environment,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=seconds,
                    check=False,
                )
                return_code = completed.returncode
            except subprocess.TimeoutExpired:
                return_code = 124
        _stop_existing(binary)
        text = log_path.read_text(errors="replace")
        sky = text.count("SKYBUG")
        engaged = text.count("FP_REPRO: first-person ENGAGED")
        started = text.count("FP_REPRO: start injecting")
        if SIGNATURE.search(text):
            print(
                f"attempt {attempt}: *** #16 CRASH SIGNATURE FOUND *** "
                f"(skybug={sky} fpStart={started} fpEngaged={engaged} "
                f"rc={return_code}) -> {log_path}"
            )
            (run_dir / "HIT").write_text(f"{log_path}\n", encoding="utf-8")
            evidence = re.compile(r"SOH3D MTX-NULL|SOH3D ARENA-CORRUPT|guMtxF2L|Matrix_ToMtx|Signal:|RDI:|RSI:|Scene:")
            for index, line in enumerate(text.splitlines(), start=1):
                if evidence.search(line):
                    print(f"{index}:{line}")
            return 0
        print(
            f"attempt {attempt}: no #16 crash "
            f"(skybug={sky} fpStart={started} fpEngaged={engaged} rc={return_code})"
        )
    print(f"fp_repro_loop: no #16 crash in {attempts} attempts. Logs in {run_dir}")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
