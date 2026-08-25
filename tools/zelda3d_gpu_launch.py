#!/usr/bin/env python3
"""Run the OoT Zelda3D core on the user's real GPU for driver diagnostics."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from mm_process import find_game_processes, terminate_exact
from rom_provision import provision_n64_extraction_rom, resolve_rom_environment

REPO = Path(__file__).resolve().parent.parent
DEFAULT_BINARY = REPO / "Shipwright/build-cmake/zelda3d/zelda3d"


def _latest_xauthority() -> str | None:
    candidates = sorted(
        (Path(f"/run/user/{os.getuid()}")).glob("xauth_*"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    return str(candidates[0]) if candidates else None


def _clear_control_files(fifo: Path) -> None:
    scratch = (REPO / "scratch").resolve()
    for path in (fifo, Path(f"{fifo}.out")):
        resolved = path.resolve(strict=False)
        if not resolved.is_relative_to(scratch):
            raise RuntimeError(f"refusing control-file cleanup outside scratch: {path}")
        resolved.unlink(missing_ok=True)


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if len(args) > 1:
        raise ValueError("usage: zelda3d_gpu_launch.py [entrance]")
    entrance = args[0] if args else os.environ.get("ZELDA3D_ENTRANCE", "219")
    binary = Path(os.environ.get("ZELDA3D_SOH", str(DEFAULT_BINARY))).resolve()
    game_dir = binary.parent.parent / "soh"

    for process in find_game_processes(binary, "oot"):
        if not terminate_exact(process):
            raise RuntimeError(f"OoT process {process.pid} did not exit")

    environment = resolve_rom_environment(REPO, os.environ)
    provision_n64_extraction_rom(game_dir, environment)
    if not environment.get("ZELDA3D_OOT3D_ROM"):
        raise RuntimeError(
            "no OoT3D .3ds found — set ZELDA3D_OOT3D_ROM, add ./.env, "
            f"or drop a *.3ds in {REPO}"
        )
    environment.setdefault("DISPLAY", ":0")
    if not environment.get("XAUTHORITY"):
        xauthority = _latest_xauthority()
        if xauthority:
            environment["XAUTHORITY"] = xauthority
    environment.update(
        {
            "SOH3D": "1",
            "ZELDA3D_WARP": "1",
            "ZELDA3D_ENTRANCE": entrance,
            "ZELDA3D_REPL": environment.get(
                "ZELDA3D_REPL", str(REPO / "scratch/zelda3d.ctl")
            ),
            "ZELDA3D_TIME": environment.get("ZELDA3D_TIME", "0x8000"),
        }
    )
    fifo = Path(environment["ZELDA3D_REPL"])
    _clear_control_files(fifo)
    print(
        "zelda3d_gpu_launch: "
        f"DISPLAY={environment['DISPLAY']} "
        f"XAUTHORITY={environment.get('XAUTHORITY', '<none>')} "
        f"entrance={entrance} time={environment['ZELDA3D_TIME']} fifo={fifo}"
    )
    return subprocess.run(
        ["stdbuf", "-oL", "-eL", str(binary), "oot"],
        cwd=game_dir,
        env=environment,
        check=False,
    ).returncode


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
