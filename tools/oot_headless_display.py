"""Xvfb provisioning for automated OoT sessions."""

from __future__ import annotations

import subprocess
import sys
import time
from collections.abc import Mapping

from oot_runtime_paths import OotRuntimePaths


def configure_headless_display(
    paths: OotRuntimePaths, environment: dict[str, str]
) -> None:
    if environment.get("ZELDA3D_HEADLESS", "1") != "1":
        return
    display_environment = {**environment, "DISPLAY": paths.display}
    if not _display_is_ready(display_environment):
        print(f"headless: starting Xvfb on {paths.display}", file=sys.stderr)
        paths.xvfb_log.parent.mkdir(parents=True, exist_ok=True)
        with paths.xvfb_log.open("wb") as log:
            subprocess.Popen(
                ["Xvfb", paths.display, "-screen", "0", "1920x1080x24"],
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        _wait_for_display(paths, display_environment)
    environment.update(
        {
            "DISPLAY": paths.display,
            "XAUTHORITY": "/dev/null",
            "SDL_VIDEODRIVER": "x11",
            "SDL_AUDIODRIVER": "dummy",
        }
    )
    environment.pop("WAYLAND_DISPLAY", None)
    print(
        f"headless: on {paths.display} (Xvfb, SDL x11, audio=dummy)",
        file=sys.stderr,
    )


def _display_is_ready(environment: Mapping[str, str]) -> bool:
    return (
        subprocess.run(
            ["xdpyinfo"],
            env=dict(environment),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode
        == 0
    )


def _wait_for_display(
    paths: OotRuntimePaths, environment: Mapping[str, str]
) -> None:
    for _ in range(20):
        if _display_is_ready(environment):
            return
        time.sleep(0.5)
    raise RuntimeError(
        f"Xvfb failed on {paths.display} (see {paths.xvfb_log.relative_to(paths.repo)})"
    )
