#!/usr/bin/env python3
"""CLI for automated, headless-by-default Ocarina of Time sessions."""

from __future__ import annotations

import os
import subprocess
import sys

from oot_game_runtime import OotRuntime

USAGE = "zelda3d_game.py {start|restart|stop|status|log} [entrance] [time]"


class UsageError(ValueError):
    """The command line does not match the public manager interface."""


def _start_arguments(arguments: list[str]) -> tuple[str, str]:
    if len(arguments) > 2:
        raise UsageError(USAGE)
    entrance = arguments[0] if arguments else os.environ.get("ZELDA3D_ENTRANCE", "238")
    daytime = (
        arguments[1]
        if len(arguments) == 2
        else os.environ.get("ZELDA3D_TIME", "0x6000")
    )
    return entrance, daytime


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if not args:
        raise UsageError(USAGE)
    command, *rest = args
    runtime = OotRuntime()
    if command == "start":
        runtime.start(*_start_arguments(rest))
        return 0
    if command == "restart":
        runtime.restart(*_start_arguments(rest))
        return 0
    if command == "stop" and not rest:
        runtime.stop()
        return 0
    if command == "status" and not rest:
        return runtime.status()
    if command == "log":
        if any(argument not in {"-f"} for argument in rest):
            raise UsageError("log accepts only optional -f")
        return subprocess.run(
            ["tail", "-n", "40", *rest, str(runtime.paths.log)], check=False
        ).returncode
    raise UsageError(USAGE)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except UsageError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
