"""Path configuration and scratch-scope policy for the Majora runtime."""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from mm_runtime_errors import RuntimeOwnershipError

REPO = Path(__file__).resolve().parent.parent
SCRATCH = REPO / "scratch"
DEFAULT_RUNTIME_DIR = SCRATCH / "logs" / "mm_n2"
GLOBAL_RUNTIME_LOCK = SCRATCH / "locks" / "mm_runtime.lock"


@dataclass(frozen=True)
class RuntimePaths:
    repo: Path
    runtime_dir: Path
    binary: Path
    game_dir: Path
    display: str
    input_fifo: Path
    repl_fifo: Path
    log: Path
    xvfb_log: Path
    manifest: Path
    pid_file: Path
    lock: Path

    @classmethod
    def from_environment(cls, environ: dict[str, str] | None = None) -> RuntimePaths:
        env = os.environ if environ is None else environ
        binary = Path(
            env.get(
                "ZELDA3D_MM",
                str(REPO / "Shipwright" / "build-cmake" / "zelda3d" / "zelda3d"),
            )
        ).resolve()
        runtime_dir = Path(
            env.get("ZELDA3D_MM_RUNTIME_DIR", str(DEFAULT_RUNTIME_DIR))
        ).resolve()
        paths = cls(
            repo=REPO,
            runtime_dir=runtime_dir,
            binary=binary,
            game_dir=binary.parent.parent / "mm",
            display=env.get("ZELDA3D_MM_DISPLAY", ":94"),
            input_fifo=Path(
                env.get("SHIP_SCRIPTED_FIFO", str(runtime_dir / "mm_input.fifo"))
            ).resolve(),
            repl_fifo=Path(
                env.get("ZELDA3D_MM_REPL", str(runtime_dir / "mm_repl.fifo"))
            ).resolve(),
            log=runtime_dir / "run_mm.log",
            xvfb_log=runtime_dir / "xvfb_mm.log",
            manifest=runtime_dir / "runtime.json",
            pid_file=SCRATCH / "mm.pid",
            lock=GLOBAL_RUNTIME_LOCK,
        )
        validate_runtime_paths(paths)
        return paths


def validate_runtime_paths(paths: RuntimePaths) -> None:
    scratch = SCRATCH.resolve()
    artifacts = (
        paths.runtime_dir,
        paths.input_fifo,
        paths.repl_fifo,
        paths.log,
        paths.xvfb_log,
        paths.manifest,
        paths.pid_file,
        paths.lock,
    )
    for path in artifacts:
        if not path.resolve(strict=False).is_relative_to(scratch):
            raise RuntimeOwnershipError(
                f"MM runtime artifact must stay under scratch: {path}"
            )
    if paths.lock.resolve(strict=False) != GLOBAL_RUNTIME_LOCK.resolve(strict=False):
        raise RuntimeOwnershipError(
            f"MM lifecycle lock must use the global path {GLOBAL_RUNTIME_LOCK}: {paths.lock}"
        )


def require_scratch_file(path: Path) -> Path:
    resolved = path.resolve(strict=False)
    if not resolved.is_relative_to(SCRATCH.resolve()):
        raise RuntimeOwnershipError(f"refusing cleanup outside scratch: {resolved}")
    return resolved
