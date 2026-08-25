"""Private Xvfb readiness and environment policy for headless harness runs."""

from __future__ import annotations

import subprocess
import time
from collections.abc import Callable, MutableMapping
from pathlib import Path
from typing import Any


def _display_ready(
    display: str,
    environment: MutableMapping[str, str],
    runner: Callable[..., Any],
) -> bool:
    try:
        runner(
            ["xdpyinfo"],
            check=True,
            env={**environment, "DISPLAY": display},
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return False
    return True


def prepare_headless_display(
    repo: Path,
    environment: MutableMapping[str, str],
    *,
    runner: Callable[..., Any] = subprocess.run,
    process_factory: Callable[..., Any] = subprocess.Popen,
    sleeper: Callable[[float], None] = time.sleep,
) -> None:
    """Ensure the configured private X display exists and select dummy audio."""
    if environment.get("ZELDA3D_HEADLESS", "1") != "1":
        return

    display = environment.get("ZELDA3D_HEADLESS_DISPLAY", ":99")
    if not _display_ready(display, environment, runner):
        log_dir = repo / "scratch" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        with (log_dir / "xvfb_harness.log").open("wb") as log:
            process_factory(
                ["Xvfb", display, "-screen", "0", "1920x1080x24"],
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        for _ in range(20):
            if _display_ready(display, environment, runner):
                break
            sleeper(0.5)
        else:
            raise RuntimeError(f"Xvfb failed to come up on {display}")

    environment.update(
        {
            "DISPLAY": display,
            "XAUTHORITY": "/dev/null",
            "SDL_VIDEODRIVER": "x11",
            "SDL_AUDIODRIVER": "dummy",
            "SOH3D_HARNESS_HEADLESS": "1",
        }
    )
    environment.pop("WAYLAND_DISPLAY", None)
