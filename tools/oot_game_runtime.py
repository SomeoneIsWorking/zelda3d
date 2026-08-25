"""Runtime ownership for automated Ocarina of Time Zelda3D sessions."""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path

from mm_process import ProcessIdentity, find_game_processes, terminate_exact
from oot_headless_display import configure_headless_display
from oot_runtime_environment import resolved_game_environment
from oot_runtime_paths import OotRuntimePaths, SCRATCH


class OotRuntime:
    def __init__(self, paths: OotRuntimePaths | None = None):
        self.paths = paths or OotRuntimePaths.from_environment()

    def game_processes(self) -> tuple[ProcessIdentity, ...]:
        return find_game_processes(self.paths.binary, "oot")

    def stop(self) -> None:
        targets = self._stop_targets()
        failures = [str(proc.pid) for proc in targets if not terminate_exact(proc)]
        if failures:
            raise RuntimeError("OoT process(es) did not exit: " + ", ".join(failures))
        self._cleanup_control_files()

    def status(self) -> int:
        processes = self.game_processes()
        if not processes:
            print(
                "zelda3d oot: none running "
                f"(searched exe={self.paths.binary}[ (deleted)] argv[1]=oot)"
            )
            return 1
        print(f"zelda3d oot running ({len(processes)}):")
        for process in processes:
            print(f"  pid {process.pid}")
        if len(processes) > 1:
            print("WARN: more than one instance!", file=os.sys.stderr)
            return 2
        return 0

    def start(self, entrance: str, daytime: str) -> None:
        self.stop()
        environment = resolved_game_environment(
            self.paths, entrance=entrance, daytime=daytime
        )
        configure_headless_display(self.paths, environment)
        self.paths.log.parent.mkdir(parents=True, exist_ok=True)
        print(f"starting: entrance={entrance} time={daytime} -> {self.paths.log}")
        with self.paths.log.open("wb") as log:
            process = subprocess.Popen(
                ["stdbuf", "-oL", "-eL", str(self.paths.binary), "oot"],
                cwd=self.paths.game_dir,
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        self.paths.pid_file.write_text(f"{process.pid}\n", encoding="utf-8")
        self._wait_until_ready(process)

    def restart(self, entrance: str, daytime: str) -> None:
        subprocess.run(
            [
                "cmake",
                "--build",
                str(self.paths.repo / "Shipwright/build-cmake"),
                "--target",
                "zelda3d_app",
                f"-j{os.cpu_count() or 1}",
            ],
            check=True,
        )
        self.start(entrance, daytime)

    def _stop_targets(self) -> tuple[ProcessIdentity, ...]:
        processes = self.game_processes()
        if self.paths.instance:
            try:
                owned_pid = int(self.paths.pid_file.read_text(encoding="utf-8").strip())
            except (FileNotFoundError, ValueError):
                return ()
            return tuple(proc for proc in processes if proc.pid == owned_pid)
        spared = self._parallel_pids()
        return tuple(proc for proc in processes if proc.pid not in spared)

    @staticmethod
    def _parallel_pids() -> set[int]:
        pids: set[int] = set()
        for path in SCRATCH.glob("zelda3d.[0-9]*.pid"):
            try:
                pids.add(int(path.read_text(encoding="utf-8").strip()))
            except (OSError, ValueError):
                continue
        return pids

    def _cleanup_control_files(self) -> None:
        for path in (
            self.paths.repl_fifo,
            Path(f"{self.paths.repl_fifo}.out"),
            self.paths.pid_file,
        ):
            path.unlink(missing_ok=True)

    def _wait_until_ready(self, process: subprocess.Popen[bytes]) -> None:
        output_fifo = Path(f"{self.paths.repl_fifo}.out")
        for _ in range(80):
            if output_fifo.exists():
                suffix = f" inst {self.paths.instance} on {self.paths.display}" if self.paths.instance else ""
                print(f"ready (pid {process.pid}{suffix})")
                return
            if process.poll() is not None:
                raise RuntimeError(
                    f"failed to boot (rc={process.returncode}); see {self.paths.log}"
                )
            time.sleep(0.5)
        raise RuntimeError(
            f"booted but REPL not ready after 40s (pid {process.pid})"
        )
