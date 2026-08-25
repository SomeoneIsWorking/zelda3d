#!/usr/bin/env python3
"""Correlated request/reply transport for tagged Zelda3D FIFO endpoints."""

from __future__ import annotations

import errno
import fcntl
import os
import re
import secrets
import time
from dataclasses import dataclass, field
from pathlib import Path


FRAME_RE = re.compile(r"^@([0-9a-f]{16}) ([DE])(?: (.*))?$")
MAX_ATOMIC_REQUEST = 4096


class FifoRpcError(RuntimeError):
    pass


class FifoRpcTimeout(FifoRpcError, TimeoutError):
    pass


class FifoRpcUnavailable(FifoRpcError):
    pass


@dataclass
class TaggedReplyCollector:
    request_id: str
    payload: list[str] = field(default_factory=list)
    complete: bool = False

    def feed(self, line: str) -> None:
        match = FRAME_RE.fullmatch(line.rstrip("\r\n"))
        if match is None or match.group(1) != self.request_id:
            return
        kind = match.group(2)
        value = match.group(3) or ""
        if kind == "D":
            if self.complete:
                raise FifoRpcError(f"payload arrived after terminator for request {self.request_id}")
            self.payload.append(value)
            return
        try:
            expected_count = int(value)
        except ValueError as exc:
            raise FifoRpcError(f"invalid reply count for request {self.request_id}: {value!r}") from exc
        if expected_count != len(self.payload):
            raise FifoRpcError(
                f"reply count mismatch for request {self.request_id}: "
                f"terminator says {expected_count}, received {len(self.payload)}"
            )
        self.complete = True

    def text(self) -> str:
        if not self.complete:
            raise FifoRpcError(f"request {self.request_id} has no terminator")
        return "\n".join(self.payload)


class FifoRpcClient:
    def __init__(self, fifo: Path | str, *, timeout: float = 5.0):
        self.fifo = Path(fifo)
        self.output = Path(f"{self.fifo}.out")
        self.lock = Path(f"{self.fifo}.rpc.lock")
        self.timeout = timeout

    def request(self, command: str) -> str:
        if "\n" in command or "\r" in command:
            raise ValueError("FIFO RPC command must be one line")
        request_id = secrets.token_hex(8)
        encoded = f"@{request_id} {command}\n".encode()
        if len(encoded) > MAX_ATOMIC_REQUEST:
            raise ValueError(f"FIFO RPC request exceeds {MAX_ATOMIC_REQUEST} bytes")

        self.lock.parent.mkdir(parents=True, exist_ok=True)
        with self.lock.open("a+", encoding="utf-8") as lock_file:
            fcntl.flock(lock_file, fcntl.LOCK_EX)
            cursor = _OutputCursor.at_end(self.output)
            self._write_request(encoded)
            return self._read_reply(request_id, cursor)

    def _write_request(self, encoded: bytes) -> None:
        try:
            descriptor = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        except FileNotFoundError as exc:
            raise FifoRpcUnavailable(f"FIFO {self.fifo} is missing; is MM running?") from exc
        except OSError as exc:
            if exc.errno == errno.ENXIO:
                raise FifoRpcUnavailable(f"FIFO {self.fifo} has no reader; is MM running?") from exc
            raise
        try:
            written = os.write(descriptor, encoded)
        finally:
            os.close(descriptor)
        if written != len(encoded):
            raise FifoRpcError(f"short FIFO write: {written} of {len(encoded)} bytes")

    def _read_reply(self, request_id: str, cursor: _OutputCursor) -> str:
        collector = TaggedReplyCollector(request_id)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            for line in cursor.read_new_lines():
                if cursor.disrupted:
                    raise FifoRpcError(f"reply stream changed during request {request_id}")
                collector.feed(line)
                if collector.complete:
                    return collector.text()
            time.sleep(0.01)
        raise FifoRpcTimeout(f"timed out waiting for correlated reply {request_id} from {self.fifo}")


@dataclass
class _OutputCursor:
    path: Path
    inode: int | None
    offset: int
    partial: str = ""
    disrupted: bool = False

    @classmethod
    def at_end(cls, path: Path) -> _OutputCursor:
        try:
            stat = path.stat()
        except FileNotFoundError:
            return cls(path, None, 0)
        return cls(path, stat.st_ino, stat.st_size)

    def read_new_lines(self) -> tuple[str, ...]:
        try:
            stat = self.path.stat()
        except FileNotFoundError:
            self.inode = None
            self.offset = 0
            self.partial = ""
            return ()
        if self.inode != stat.st_ino or stat.st_size < self.offset:
            self.disrupted = self.inode is not None
            self.inode = stat.st_ino
            self.offset = 0
            self.partial = ""
        if stat.st_size == self.offset:
            return ()
        with self.path.open("r", encoding="utf-8", errors="replace") as output:
            output.seek(self.offset)
            chunk = output.read()
            self.offset = output.tell()
        combined = self.partial + chunk
        lines = combined.splitlines(keepends=True)
        if lines and not lines[-1].endswith(("\n", "\r")):
            self.partial = lines.pop()
        else:
            self.partial = ""
        return tuple(line.rstrip("\r\n") for line in lines)
