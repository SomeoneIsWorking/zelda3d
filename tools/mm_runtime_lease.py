"""Global exclusion lease for all Majora runtime lifecycle operations."""

from __future__ import annotations

import fcntl
from contextlib import AbstractContextManager
from typing import IO, Self

from mm_runtime_errors import RuntimeBusy, RuntimeOwnershipError
from mm_runtime_paths import RuntimePaths, validate_runtime_paths


class RuntimeLease(AbstractContextManager["RuntimeLease"]):
    def __init__(self, paths: RuntimePaths, *, blocking: bool = True):
        validate_runtime_paths(paths)
        self.paths = paths
        self.blocking = blocking
        self._file: IO[str] | None = None

    def __enter__(self) -> Self:
        self.paths.runtime_dir.mkdir(parents=True, exist_ok=True)
        self.paths.lock.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.paths.lock.open("a+", encoding="utf-8")
        operation = fcntl.LOCK_EX | (0 if self.blocking else fcntl.LOCK_NB)
        try:
            fcntl.flock(self._file, operation)
        except BlockingIOError as exc:
            self._file.close()
            self._file = None
            raise RuntimeBusy(
                f"another MM lifecycle operation holds {self.paths.lock}"
            ) from exc
        return self

    def __exit__(self, *_args: object) -> None:
        if self._file is not None:
            fcntl.flock(self._file, fcntl.LOCK_UN)
            self._file.close()
            self._file = None

    def assert_held(self) -> None:
        if self._file is None:
            raise RuntimeOwnershipError(
                "MM runtime operation requires a held lifecycle lease"
            )


class HeldLeaseProof:
    """Internal proof for a lifecycle operation that already holds the global lease."""

    @staticmethod
    def assert_held() -> None:
        return
