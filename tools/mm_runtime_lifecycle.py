"""Exact child-process lifecycle orchestration for the Majora runtime."""

from __future__ import annotations

import subprocess
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from fifo_rpc import FifoRpcTimeout, FifoRpcUnavailable
from mm_process import (
    ProcessIdentity,
    find_game_processes,
    inspect_process,
    same_process,
    terminate_exact,
    wait_for_exit,
)
from mm_runtime_errors import RuntimeBusy, RuntimeErrorBase, RuntimeOwnershipError
from mm_runtime_launch import RuntimeLaunchProvisioner
from mm_runtime_lease import HeldLeaseProof, RuntimeLease
from mm_runtime_manifest import RuntimeInstance, RuntimeManifest
from mm_runtime_paths import RuntimePaths, validate_runtime_paths


@dataclass(frozen=True)
class RuntimeStatus:
    instance: RuntimeInstance | None
    game_alive: bool
    xvfb_alive: bool
    foreign_games: tuple[ProcessIdentity, ...]

    @property
    def healthy(self) -> bool:
        return (
            self.instance is not None
            and self.game_alive
            and self.xvfb_alive
            and not self.foreign_games
        )


class MMRuntime:
    def __init__(self, paths: RuntimePaths | None = None):
        self.paths = paths or RuntimePaths.from_environment()
        validate_runtime_paths(self.paths)
        self._manifest = RuntimeManifest(self.paths)
        self._launch = RuntimeLaunchProvisioner(self.paths)
        self._spawned: dict[int, subprocess.Popen[bytes]] = {}

    def lease(self, *, blocking: bool = True) -> RuntimeLease:
        return RuntimeLease(self.paths, blocking=blocking)

    def read_manifest(self) -> RuntimeInstance | None:
        return self._manifest.read()

    def status(self, lease: RuntimeLease) -> RuntimeStatus:
        lease.assert_held()
        instance = self.read_manifest()
        game_alive = instance is not None and self._process_alive(instance.game)
        xvfb_alive = instance is not None and self._process_alive(instance.xvfb)
        owned_pid = instance.game.pid if game_alive and instance is not None else None
        foreign = tuple(
            process
            for process in find_game_processes(self.paths.binary, "mm")
            if process.pid != owned_pid
        )
        return RuntimeStatus(instance, game_alive, xvfb_alive, foreign)

    def start(
        self,
        lease: RuntimeLease,
        entrance: str | None,
        *,
        extra_env: dict[str, str] | None = None,
    ) -> RuntimeInstance:
        lease.assert_held()
        self._prepare_start()
        env = self._launch.environment(entrance, extra_env)
        display_socket = self._launch.display_socket()
        if display_socket.exists():
            raise RuntimeBusy(
                f"display {self.paths.display} is already owned ({display_socket} exists)"
            )

        self.paths.runtime_dir.mkdir(parents=True, exist_ok=True)
        self.paths.log.write_bytes(b"")
        xvfb_command = [
            "Xvfb",
            self.paths.display,
            "-screen",
            "0",
            "1280x960x24",
        ]
        with self.paths.xvfb_log.open("wb") as xvfb_log:
            xvfb_process = subprocess.Popen(
                xvfb_command,
                stdout=xvfb_log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        xvfb = self._capture_started_process(xvfb_process, "Xvfb", tuple(xvfb_command))
        try:
            self._wait_for_display(xvfb, display_socket)
            game_command = [str(self.paths.binary), "mm"]
            with self.paths.log.open("wb") as game_log:
                game_process = subprocess.Popen(
                    game_command,
                    cwd=self.paths.game_dir,
                    env=env,
                    stdout=game_log,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
            game = self._capture_started_process(
                game_process, "zelda3d mm", tuple(game_command)
            )
        except BaseException:
            self._terminate_spawned(xvfb_process)
            raise

        instance = RuntimeInstance(
            game, xvfb, str(self.paths.binary), self.paths.display
        )
        self._spawned = {game.pid: game_process, xvfb.pid: xvfb_process}
        try:
            self._manifest.write(instance)
        except BaseException:
            self._terminate_spawned(game_process)
            self._terminate_spawned(xvfb_process)
            self._spawned.clear()
            raise
        return instance

    def stop(self, lease: RuntimeLease) -> None:
        lease.assert_held()
        instance = self.read_manifest()
        if instance is None:
            return
        failures: list[str] = []
        if not self._stop_owned_process(instance.game):
            failures.append(f"game pid {instance.game.pid}")
        if not self._stop_owned_process(instance.xvfb):
            failures.append(f"Xvfb pid {instance.xvfb.pid}")
        if failures:
            raise RuntimeOwnershipError(
                "owned process did not exit: " + ", ".join(failures)
            )
        self._manifest.cleanup()

    def finish_orderly_exit(self, lease: RuntimeLease) -> None:
        """Remove ownership state after the game has exited through its REPL."""
        lease.assert_held()
        instance = self.read_manifest()
        if instance is None:
            return
        if self._process_alive(instance.game):
            raise RuntimeOwnershipError(f"game pid {instance.game.pid} is still alive")
        if not self._stop_owned_process(instance.xvfb):
            raise RuntimeOwnershipError(
                f"owned Xvfb pid {instance.xvfb.pid} did not exit"
            )
        self._manifest.cleanup()

    def wait_for_gameplay(
        self,
        lease: RuntimeLease,
        query: Callable[[], str],
        *,
        timeout: float = 40.0,
    ) -> str:
        lease.assert_held()
        instance = self.read_manifest()
        if instance is None:
            raise RuntimeOwnershipError("MM runtime has no owned instance")
        deadline = time.monotonic() + timeout
        last = "(no reply)"
        while time.monotonic() < deadline:
            if not self._process_alive(instance.game):
                raise RuntimeErrorBase(
                    f"MM exited before gameplay; see {self.paths.log}"
                )
            try:
                last = query()
            except (FifoRpcTimeout, FifoRpcUnavailable) as exc:
                last = f"(control transport not ready: {exc})"
            if "pos=(" in last:
                return last
            time.sleep(0.1)
        raise RuntimeErrorBase(
            f"gameplay not reached within {timeout:.0f}s; last reply: {last}"
        )

    def wait_for_owned_exit(self, identity: ProcessIdentity, timeout: float) -> bool:
        process = self._spawned.get(identity.pid)
        if process is None:
            return wait_for_exit(identity, timeout)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return False
        self._spawned.pop(identity.pid, None)
        return True

    def _prepare_start(self) -> None:
        self._launch.validate_prerequisites()
        status = self.status(HeldLeaseProof())
        if status.game_alive or status.xvfb_alive:
            raise RuntimeBusy(
                "owned MM runtime already exists; stop it explicitly before starting"
            )
        if status.foreign_games:
            pids = ", ".join(str(process.pid) for process in status.foreign_games)
            raise RuntimeBusy(f"unowned zelda3d mm process(es) already exist: {pids}")
        self._manifest.cleanup()

    def _capture_started_process(
        self,
        process: subprocess.Popen[bytes],
        label: str,
        expected_argv: tuple[str, ...],
    ) -> ProcessIdentity:
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            identity = inspect_process(process.pid)
            if identity is not None and identity.argv == expected_argv:
                return identity
            if process.poll() is not None:
                break
            time.sleep(0.01)
        raise RuntimeErrorBase(f"{label} failed to start")

    @staticmethod
    def _terminate_spawned(process: subprocess.Popen[bytes]) -> None:
        if process.poll() is not None:
            return
        process.terminate()
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2.0)

    def _stop_owned_process(self, identity: ProcessIdentity) -> bool:
        process = self._spawned.pop(identity.pid, None)
        if process is None:
            return terminate_exact(identity)
        self._terminate_spawned(process)
        return process.poll() is not None

    def _process_alive(self, identity: ProcessIdentity) -> bool:
        process = self._spawned.get(identity.pid)
        if process is not None and process.poll() is not None:
            self._spawned.pop(identity.pid, None)
            return False
        return same_process(identity)

    def _wait_for_display(self, xvfb: ProcessIdentity, socket: Path) -> None:
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            if not same_process(xvfb):
                raise RuntimeErrorBase(
                    f"Xvfb exited before display {self.paths.display} became ready"
                )
            if socket.exists():
                return
            time.sleep(0.05)
        raise RuntimeErrorBase(f"Xvfb display socket did not appear: {socket}")
