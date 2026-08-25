"""Path and scratch-scope policy for automated OoT Zelda3D sessions."""

from __future__ import annotations

import os
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRATCH = REPO / "scratch"


@dataclass(frozen=True)
class OotRuntimePaths:
    repo: Path
    binary: Path
    game_dir: Path
    instance: str
    display: str
    log: Path
    repl_fifo: Path
    pid_file: Path
    xvfb_log: Path

    @classmethod
    def from_environment(
        cls, environment: Mapping[str, str] | None = None
    ) -> "OotRuntimePaths":
        env = os.environ if environment is None else environment
        binary = Path(
            env.get(
                "ZELDA3D_SOH",
                str(REPO / "Shipwright/build-cmake/zelda3d/zelda3d"),
            )
        ).resolve()
        instance = env.get("ZELDA3D_INSTANCE", "")
        if instance:
            try:
                instance_number = int(instance)
            except ValueError as exc:
                raise ValueError("ZELDA3D_INSTANCE must be a positive integer") from exc
            if instance_number <= 0 or instance_number >= 99:
                raise ValueError("ZELDA3D_INSTANCE must be between 1 and 98")
            suffix = f".{instance}"
            default_display = f":{99 - instance_number}"
        else:
            suffix = ""
            default_display = ":99"
        paths = cls(
            repo=REPO,
            binary=binary,
            game_dir=binary.parent.parent / "soh",
            instance=instance,
            display=env.get("ZELDA3D_HEADLESS_DISPLAY", default_display),
            log=SCRATCH / "logs" / f"run{suffix}.log",
            repl_fifo=Path(
                env.get("ZELDA3D_REPL", str(SCRATCH / f"zelda3d{suffix}.ctl"))
            ).resolve(),
            pid_file=SCRATCH / f"zelda3d{suffix}.pid",
            xvfb_log=SCRATCH / "logs" / "xvfb.log",
        )
        paths.validate_scratch_artifacts()
        return paths

    def validate_scratch_artifacts(self) -> None:
        scratch = SCRATCH.resolve()
        for path in (self.log, self.repl_fifo, self.pid_file, self.xvfb_log):
            if not path.resolve(strict=False).is_relative_to(scratch):
                raise ValueError(f"OoT runtime artifact must stay under scratch: {path}")
