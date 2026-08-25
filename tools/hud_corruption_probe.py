"""Shared live-session operations for HUD corruption diagnostics."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
GAME = REPO / "tools/zelda3d_game.py"
REPL = REPO / "tools/zelda3d_repl.py"
SCREENSHOTS = REPO / "scratch/screenshots"


def game(command: str, entrance: str | None = None, daytime: str | None = None) -> bool:
    arguments = [sys.executable, str(GAME), command]
    if entrance is not None:
        arguments.append(entrance)
    if daytime is not None:
        arguments.append(daytime)
    completed = subprocess.run(
        arguments,
        cwd=REPO,
        env={
            **os.environ,
            "ZELDA3D_HEADLESS": "1",
            "ZELDA3D_AUTO": "1",
            "ZELDA3D_LINK": "1",
        },
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return completed.returncode == 0


def repl(command: str, *, quiet: bool = True) -> str:
    completed = subprocess.run(
        [sys.executable, str(REPL), "cmd", command],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=False,
    )
    if not quiet and completed.stdout:
        print(completed.stdout, end="")
    return completed.stdout


def wait_for_scene(attempts: int = 40) -> bool:
    return any("scene" in repl("posinfo") for _ in range(attempts))


def frame_hud(*, settle_frames: int) -> None:
    for _ in range(6):
        repl("btnhold 0x8000 2")
        repl("btnhold 0 0")
    repl("tp -66 -79 939")
    repl("cam -66 60 939 -66 900 1200")
    for _ in range(settle_frames):
        repl("posinfo")


def screenshot(name: str) -> Path:
    subprocess.run(
        [sys.executable, str(REPL), "shot", name],
        cwd=REPO,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return SCREENSHOTS / f"{name}.png"


def hud_mse(reference: Path, candidate: Path) -> float:
    hearts = (40, 20, 560, 140)
    rupee = (20, 900, 360, 1006)

    def crops(path: Path) -> tuple[np.ndarray, np.ndarray]:
        image = Image.open(path).convert("RGB")
        if image.size != (1920, 1006):
            image = image.resize((1920, 1006))
        return (
            np.asarray(image.crop(hearts), dtype=np.float32),
            np.asarray(image.crop(rupee), dtype=np.float32),
        )

    first = crops(reference)
    second = crops(candidate)
    return float(
        ((first[0] - second[0]) ** 2).mean()
        + ((first[1] - second[1]) ** 2).mean()
    )
