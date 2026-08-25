#!/usr/bin/env python3
"""Exact Linux process identity and signaling for the Majora runtime owner."""

from __future__ import annotations

import os
import signal
import time
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass(frozen=True)
class ProcessIdentity:
    pid: int
    start_ticks: int
    executable: str
    argv: tuple[str, ...]

    def to_json(self) -> dict[str, object]:
        value = asdict(self)
        value["argv"] = list(self.argv)
        return value

    @classmethod
    def from_json(cls, value: dict[str, object]) -> ProcessIdentity:
        return cls(
            pid=int(value["pid"]),
            start_ticks=int(value["start_ticks"]),
            executable=str(value["executable"]),
            argv=tuple(str(arg) for arg in value["argv"]),  # type: ignore[arg-type]
        )


def inspect_process(pid: int) -> ProcessIdentity | None:
    proc = Path("/proc") / str(pid)
    try:
        stat = (proc / "stat").read_text(encoding="utf-8")
        closing_paren = stat.rfind(")")
        start_ticks = int(stat[closing_paren + 2 :].split()[19])
        executable = os.readlink(proc / "exe")
        if executable.endswith(" (deleted)"):
            executable = executable[: -len(" (deleted)")]
        raw_argv = (proc / "cmdline").read_bytes().split(b"\0")
        argv = tuple(os.fsdecode(arg) for arg in raw_argv if arg)
    except (FileNotFoundError, PermissionError, ProcessLookupError, ValueError):
        return None
    return ProcessIdentity(pid, start_ticks, executable, argv)


def same_process(expected: ProcessIdentity) -> bool:
    actual = inspect_process(expected.pid)
    return actual is not None and (
        actual.start_ticks == expected.start_ticks
        and actual.executable == expected.executable
        and actual.argv == expected.argv
    )


def find_game_processes(binary: Path, game_id: str) -> tuple[ProcessIdentity, ...]:
    wanted = str(binary.resolve())
    found: list[ProcessIdentity] = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        identity = inspect_process(int(proc.name))
        if identity is None or identity.executable != wanted or len(identity.argv) < 2:
            continue
        if identity.argv[1] == game_id:
            found.append(identity)
    return tuple(sorted(found, key=lambda item: item.pid))


def terminate_exact(
    identity: ProcessIdentity,
    *,
    graceful_timeout: float = 5.0,
    force_timeout: float = 2.0,
) -> bool:
    """Terminate only the recorded PID generation; return whether it exited."""
    if not same_process(identity):
        return True
    try:
        os.kill(identity.pid, signal.SIGTERM)
    except ProcessLookupError:
        return True
    if wait_for_exit(identity, graceful_timeout):
        return True
    if not same_process(identity):
        return True
    try:
        os.kill(identity.pid, signal.SIGKILL)
    except ProcessLookupError:
        return True
    return wait_for_exit(identity, force_timeout)


def wait_for_exit(identity: ProcessIdentity, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while same_process(identity):
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.05)
    return True
