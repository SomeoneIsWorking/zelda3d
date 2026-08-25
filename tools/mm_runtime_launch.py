"""Host prerequisites and launch environment for the Majora runtime."""

from __future__ import annotations

import os
import re
import shutil
from pathlib import Path

from mm_runtime_errors import RuntimeErrorBase
from mm_runtime_paths import RuntimePaths
from repo_environment import apply_repo_environment


class RuntimeLaunchProvisioner:
    def __init__(self, paths: RuntimePaths):
        self.paths = paths

    def validate_prerequisites(self) -> None:
        require_program("Xvfb")
        if not self.paths.binary.is_file() or not os.access(self.paths.binary, os.X_OK):
            raise RuntimeErrorBase(
                f"MM launcher missing or not executable: {self.paths.binary}"
            )
        if not self.paths.game_dir.is_dir():
            raise RuntimeErrorBase(f"MM game directory missing: {self.paths.game_dir}")

    def environment(
        self,
        entrance: str | None,
        extra: dict[str, str] | None,
        *,
        environ: dict[str, str] | None = None,
    ) -> dict[str, str]:
        env = dict(os.environ if environ is None else environ)
        apply_repo_environment(self.paths.repo, env)
        if extra:
            env.update(extra)
        env.pop("WAYLAND_DISPLAY", None)
        env.update(
            {
                "DISPLAY": self.paths.display,
                "SDL_VIDEODRIVER": "x11",
                "SDL_AUDIODRIVER": "dummy",
                "LIBGL_ALWAYS_SOFTWARE": "1",
                "GALLIUM_DRIVER": "llvmpipe",
                "ZELDA3D_MM_WARP": "1",
                "SHIP_SCRIPTED_FIFO": str(self.paths.input_fifo),
                "ZELDA3D_MM_REPL": str(self.paths.repl_fifo),
            }
        )
        if entrance:
            int(entrance, 0)
            env["ZELDA3D_MM_ENTRANCE"] = entrance
        return env

    def display_socket(self) -> Path:
        match = re.fullmatch(r":(\d+)", self.paths.display)
        if match is None:
            raise RuntimeErrorBase(
                f"unsupported private display syntax: {self.paths.display}"
            )
        return Path("/tmp/.X11-unix") / f"X{match.group(1)}"


def require_program(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RuntimeErrorBase(f"required program is missing: {name}")
    return path
