#!/usr/bin/env python3
"""CLI for the exact-owned headless Majora runtime."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from fifo_rpc import FifoRpcClient
from mm_runtime_errors import RuntimeBusy, RuntimeErrorBase, RuntimeOwnershipError
from mm_runtime_lease import RuntimeLease
from mm_runtime_lifecycle import MMRuntime

USAGE = (
    "mm_game.py {start [entrance]|restart [entrance]|stop|status|shot <name>|log [-f]}"
)


def _control(runtime: MMRuntime) -> FifoRpcClient:
    return FifoRpcClient(runtime.paths.repl_fifo, timeout=5.0)


def _start_locked(
    runtime: MMRuntime, lease: RuntimeLease, entrance: str | None
) -> None:
    instance = runtime.start(lease, entrance)
    try:
        runtime.wait_for_gameplay(lease, lambda: _control(runtime).request("posinfo"))
    except BaseException:
        runtime.stop(lease)
        raise
    print(
        f"mm pid={instance.game.pid} disp={runtime.paths.display} entrance={entrance or 'default'}"
    )
    print(f"input_fifo={runtime.paths.input_fifo} repl_fifo={runtime.paths.repl_fifo}")
    print("gameplay reached")


def start(runtime: MMRuntime, entrance: str | None) -> int:
    with runtime.lease(blocking=False) as lease:
        _start_locked(runtime, lease, entrance)
    return 0


def restart(runtime: MMRuntime, entrance: str | None) -> int:
    with runtime.lease(blocking=False) as lease:
        state = runtime.status(lease)
        if state.game_alive or state.xvfb_alive or state.foreign_games:
            raise RuntimeBusy("refusing to rebuild while an MM runtime exists")
        jobs = os.environ.get("ZELDA3D_BUILD_JOBS", "4")
        subprocess.run(
            [
                "cmake",
                "--build",
                str(runtime.paths.repo / "Shipwright" / "build-cmake"),
                "--target",
                "zelda3d_app",
                f"-j{jobs}",
            ],
            check=True,
        )
        _start_locked(runtime, lease, entrance)
    return 0


def stop(runtime: MMRuntime) -> int:
    with runtime.lease(blocking=False) as lease:
        status = runtime.status(lease)
        if status.instance is None:
            if status.foreign_games:
                pids = ", ".join(str(process.pid) for process in status.foreign_games)
                raise RuntimeOwnershipError(
                    f"refusing to stop unowned MM process(es): {pids}"
                )
            print("stopped (no owned instance)")
            return 0
        runtime.stop(lease)
    print("stopped")
    return 0


def status(runtime: MMRuntime) -> int:
    with runtime.lease(blocking=False) as lease:
        state = runtime.status(lease)
    count = int(state.game_alive) + len(state.foreign_games)
    print(f"zelda3d mm instances: {count}")
    if state.game_alive and state.instance is not None:
        health = "healthy" if state.xvfb_alive else "Xvfb-dead"
        print(f"{state.instance.game.pid} owned {health}")
    for process in state.foreign_games:
        print(f"{process.pid} unowned")
    return 0 if state.healthy else 1


def shot(runtime: MMRuntime, name: str) -> int:
    if not name or Path(name).name != name:
        raise ValueError("shot name must be one filename component")
    with runtime.lease(blocking=False) as lease:
        if not runtime.status(lease).healthy:
            raise RuntimeErrorBase("cannot capture: owned MM runtime is not healthy")
        output = runtime.paths.repo / "scratch" / "screenshots" / f"{name}.png"
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["import", "-window", "root", str(output)],
            check=True,
            env={**os.environ, "DISPLAY": runtime.paths.display},
        )
    print(f"shot -> {name}.png")
    return 0


def show_log(runtime: MMRuntime, arguments: list[str]) -> int:
    allowed = [] if not arguments else arguments
    if any(argument not in {"-f"} for argument in allowed):
        raise ValueError("log accepts only optional -f")
    return subprocess.run(
        ["tail", *allowed, str(runtime.paths.log)], check=False
    ).returncode


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if not arguments:
        raise ValueError(USAGE)
    command, *rest = arguments
    runtime = MMRuntime()
    if command in {"start", "restart"}:
        if len(rest) > 1:
            raise ValueError(USAGE)
        entrance = rest[0] if rest else None
        return (
            start(runtime, entrance)
            if command == "start"
            else restart(runtime, entrance)
        )
    if command == "stop" and not rest:
        return stop(runtime)
    if command == "status" and not rest:
        return status(runtime)
    if command == "shot" and len(rest) == 1:
        return shot(runtime, rest[0])
    if command == "log":
        return show_log(runtime, rest)
    raise ValueError(USAGE)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        RuntimeBusy,
        RuntimeErrorBase,
        subprocess.CalledProcessError,
        ValueError,
    ) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
