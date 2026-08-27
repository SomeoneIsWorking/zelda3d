"""Exact-owned live MM session used by the phase-tour orchestrator."""

from __future__ import annotations

import re
import time

from fifo_rpc import FifoRpcClient
from mm_phase_catalog import Scene
from mm_process import same_process
from mm_runtime_lease import RuntimeLease
from mm_runtime_lifecycle import MMRuntime


class PhaseSessionError(RuntimeError):
    """The owned MM session could not complete an exact lifecycle operation."""


class LiveMM:
    def __init__(
        self,
        runtime: MMRuntime,
        lease: RuntimeLease,
        command_timeout: float,
        *,
        client: FifoRpcClient | None = None,
    ):
        self.runtime = runtime
        self.lease = lease
        self.client = client or FifoRpcClient(
            runtime.paths.repl_fifo, timeout=command_timeout
        )
        self.started = False

    def require_idle(self) -> None:
        status = self.runtime.status(self.lease)
        if not (status.game_alive or status.xvfb_alive or status.foreign_games):
            return
        pids = [process.pid for process in status.foreign_games]
        if status.game_alive and status.instance is not None:
            pids.append(status.instance.game.pid)
        if status.xvfb_alive and status.instance is not None:
            pids.append(status.instance.xvfb.pid)
        raise PhaseSessionError(
            f"an MM runtime already exists (pid(s): {', '.join(map(str, pids))})"
        )

    def start(self, entrance: int) -> str:
        instance = self.runtime.start(
            self.lease,
            hex(entrance),
            extra_env={
                "ZELDA3D_MM_PHASE_REPORT": "1",
                "ZELDA3D_MM_SKINNED": "1",
            },
        )
        self.started = True
        position = self.runtime.wait_for_gameplay(
            self.lease,
            lambda: self.client.request("posinfo"),
            timeout=45.0,
        )
        return f"mm pid={instance.game.pid} gameplay reached: {position}"

    def query(self, command: str) -> str:
        return self.client.request(command)

    def wait_for_scene(self, scene: Scene, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        last = "(no reply)"
        while time.monotonic() < deadline:
            last = self.query("posinfo")
            match = re.search(r"\bscene=(-?\d+)\b", last)
            if (
                match is not None
                and int(match.group(1)) == scene.scene_id
                and "pos=(" in last
            ):
                return last
            time.sleep(0.1)
        raise PhaseSessionError(
            f"{scene.name} entrance {hex(scene.entrance)} never reached expected "
            f"scene {scene.scene_id}; last reply: {last}"
        )

    def warp(self, scene: Scene) -> str:
        reply = self.query(f"warp {hex(scene.entrance)}")
        if not reply.startswith("ok warp"):
            raise PhaseSessionError(f"warp to {scene.name} was refused: {reply}")
        return reply

    def quit(self) -> str:
        instance = self.runtime.read_manifest()
        if instance is None or not same_process(instance.game):
            raise PhaseSessionError(
                "owned MM process disappeared before orderly teardown"
            )
        reply = self.query("quitteardown")
        if not self.runtime.wait_for_owned_exit(instance.game, 20.0):
            raise PhaseSessionError(
                f"MM accepted quitteardown but pid {instance.game.pid} did not exit"
            )
        self.runtime.finish_orderly_exit(self.lease)
        self.started = False
        return reply

    def cleanup(self) -> None:
        if self.started:
            self.runtime.stop(self.lease)
            self.started = False
