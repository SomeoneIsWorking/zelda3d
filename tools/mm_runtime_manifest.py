"""Exact process manifest and owned runtime-artifact lifecycle."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from mm_process import ProcessIdentity
from mm_runtime_errors import RuntimeOwnershipError
from mm_runtime_paths import RuntimePaths, require_scratch_file


@dataclass(frozen=True)
class RuntimeInstance:
    game: ProcessIdentity
    xvfb: ProcessIdentity
    binary: str
    display: str

    def to_json(self) -> dict[str, object]:
        return {
            "version": 1,
            "binary": self.binary,
            "display": self.display,
            "game": self.game.to_json(),
            "xvfb": self.xvfb.to_json(),
        }

    @classmethod
    def from_json(cls, value: dict[str, object]) -> RuntimeInstance:
        if value.get("version") != 1:
            raise ValueError("unsupported MM runtime manifest version")
        return cls(
            game=ProcessIdentity.from_json(value["game"]),  # type: ignore[arg-type]
            xvfb=ProcessIdentity.from_json(value["xvfb"]),  # type: ignore[arg-type]
            binary=str(value["binary"]),
            display=str(value["display"]),
        )


class RuntimeManifest:
    def __init__(self, paths: RuntimePaths):
        self.paths = paths

    def read(self) -> RuntimeInstance | None:
        try:
            value = json.loads(self.paths.manifest.read_text(encoding="utf-8"))
            return RuntimeInstance.from_json(value)
        except FileNotFoundError:
            return None
        except (json.JSONDecodeError, TypeError, ValueError) as exc:
            raise RuntimeOwnershipError(
                f"invalid MM runtime manifest {self.paths.manifest}: {exc}"
            ) from exc

    def write(self, instance: RuntimeInstance) -> None:
        temporary = self.paths.manifest.with_suffix(".json.new")
        temporary.write_text(
            json.dumps(instance.to_json(), indent=2) + "\n", encoding="utf-8"
        )
        temporary.replace(self.paths.manifest)
        self.paths.pid_file.write_text(f"{instance.game.pid}\n", encoding="utf-8")

    def cleanup(self) -> None:
        for path in self._owned_artifacts():
            try:
                require_scratch_file(path).unlink()
            except FileNotFoundError:
                pass

    def _owned_artifacts(self) -> tuple[Path, ...]:
        return (
            self.paths.input_fifo,
            Path(f"{self.paths.input_fifo}.out"),
            self.paths.repl_fifo,
            Path(f"{self.paths.repl_fifo}.out"),
            self.paths.pid_file,
            self.paths.manifest,
            self.paths.manifest.with_suffix(".json.new"),
        )
