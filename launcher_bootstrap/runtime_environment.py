"""Environment defaults and desktop discovery for the shipping launcher."""

from __future__ import annotations

import os
import sys
from collections.abc import Mapping
from pathlib import Path

VALUE_DEFAULTS = {
    "ZELDA3D_ENTRANCE": "219",
    "ZELDA3D_AUTO": "1",
    "ZELDA3D_N64ANIM": "1",
    "ZELDA3D_VULKAN": "1",
}
EMPTY_DEFAULTS = {
    "ZELDA3D_WARP": "",
    "ZELDA3D_COLDBOOT": "",
    "ZELDA3D_TIME": "",
    "ZELDA3D_GL_STATECHECK": "",
}


def _first_xauthority(user_id: int) -> str | None:
    directory = Path("/run/user") / str(user_id)
    return next(
        (str(path) for path in sorted(directory.glob("xauth_*")) if path.is_file()),
        None,
    )


def prepare_runtime_environment(
    source: Mapping[str, str],
    *,
    platform: str = sys.platform,
    user_id: int | None = None,
) -> dict[str, str]:
    """Return the product environment without mutating the caller's mapping."""
    environment = dict(source)
    environment["SOH3D"] = "1"
    for name, value in VALUE_DEFAULTS.items():
        if not environment.get(name):
            environment[name] = value
    for name, value in EMPTY_DEFAULTS.items():
        environment.setdefault(name, value)

    if platform.startswith("linux") and not environment.get("DISPLAY"):
        environment["DISPLAY"] = ":0"
        if not environment.get("XAUTHORITY"):
            authority = _first_xauthority(os.getuid() if user_id is None else user_id)
            if authority is not None:
                environment["XAUTHORITY"] = authority
    return environment
