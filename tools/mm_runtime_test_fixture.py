"""Focused RuntimePaths fixture shared by Majora runtime unit tests."""

from pathlib import Path

from mm_runtime_paths import GLOBAL_RUNTIME_LOCK, RuntimePaths


def runtime_paths(directory: Path) -> RuntimePaths:
    runtime_dir = directory / "runtime"
    return RuntimePaths(
        repo=directory,
        runtime_dir=runtime_dir,
        binary=Path("/proc/self/exe").resolve(),
        game_dir=directory,
        display=":199",
        input_fifo=runtime_dir / "input.fifo",
        repl_fifo=runtime_dir / "repl.fifo",
        log=runtime_dir / "run.log",
        xvfb_log=runtime_dir / "xvfb.log",
        manifest=runtime_dir / "runtime.json",
        pid_file=directory / "pid",
        lock=GLOBAL_RUNTIME_LOCK,
    )
