"""Process transport and response framing for the embedded harness REPL."""

from __future__ import annotations

import os
import select
import subprocess
import time
from collections.abc import Callable
from contextlib import ExitStack
from typing import Self

from harness_paths import REPO_ROOT

ReadLine = Callable[[float], str | None]


def _read_streaming_response(
    read_line: ReadLine,
    command: str,
    per_line_timeout: float,
    peek_timeout: float,
) -> list[str]:
    """Collect one harness response using its three supported frame shapes.

    Replies are either one ``ok`` line, an ``ok`` header followed by ``ok end``,
    or a labeled header followed by a named ``ok``/``err`` terminator.
    """
    first = read_line(per_line_timeout)
    if first is None:
        raise TimeoutError(
            f"send_multiline({command!r}): no first line in {per_line_timeout}s"
        )

    lines = [first.rstrip()]
    if lines[0].startswith("err "):
        return lines
    first_ok = lines[0] == "ok" or lines[0].startswith("ok ")
    if first_ok:
        peek = read_line(peek_timeout)
        if peek is None:
            return lines
        lines.append(peek.rstrip())

    while True:
        line = read_line(per_line_timeout)
        if line is None:
            raise TimeoutError(
                f"send_multiline({command!r}): no line for {per_line_timeout}s; "
                f"got {len(lines)} lines so far (last: {lines[-1]!r})"
            )
        line = line.rstrip()
        lines.append(line)
        if line == "ok end":
            return lines
        if not first_ok and line.startswith(("ok ", "err ")):
            return lines


class Harness:
    """Own one harness subprocess and its line-oriented REPL transport."""

    def __init__(self, cmd: list[str]):
        # Binary, unbuffered I/O keeps select() and the Python buffer in sync.
        with ExitStack() as resources:
            stderr = subprocess.DEVNULL
            if os.environ.get("HARNESS_STDERR"):
                stderr = resources.enter_context(
                    open(os.environ["HARNESS_STDERR"], "wb")
                )
            self.proc = subprocess.Popen(
                cmd,
                cwd=str(REPO_ROOT),
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=stderr,
                bufsize=0,
            )
        self._buf = b""
        line = self._readline()
        if line is None or line.strip() != "boot succeeded":
            self.close()
            raise RuntimeError(f"harness did not boot: got {line!r}")

    def _readline(self, timeout: float = 60.0) -> str | None:
        """Read one newline-terminated response without over-buffering stdout."""
        stdout = self.proc.stdout
        if stdout is None:
            raise RuntimeError("harness stdout pipe is unavailable")
        fd = stdout.fileno()
        deadline = time.monotonic() + timeout
        while b"\n" not in self._buf:
            remaining = max(0.0, deadline - time.monotonic())
            ready, _, _ = select.select([fd], [], [], remaining)
            if not ready:
                return None
            chunk = os.read(fd, 8192)
            if not chunk:
                raise RuntimeError("harness closed stdout unexpectedly")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode("utf-8", errors="replace")

    def send(self, command: str) -> str:
        """Send one command and return its first response line."""
        stdin = self.proc.stdin
        if stdin is None:
            raise RuntimeError("harness stdin pipe is unavailable")
        stdin.write((command.rstrip() + "\n").encode())
        stdin.flush()
        line = self._readline()
        if line is None:
            raise TimeoutError(f"send({command!r}): no response within timeout")
        return line.rstrip()

    def send_multiline(
        self,
        command: str,
        per_line_timeout: float = 30.0,
        peek_timeout: float = 0.2,
    ) -> list[str]:
        """Send a command and collect its complete single- or multi-line response."""
        stdin = self.proc.stdin
        if stdin is None:
            raise RuntimeError("harness stdin pipe is unavailable")
        stdin.write((command.rstrip() + "\n").encode())
        stdin.flush()
        return _read_streaming_response(
            self._readline, command, per_line_timeout, peek_timeout
        )

    def quit(self) -> None:
        """Request clean shutdown and wait for this exact child process."""
        stdin = self.proc.stdin
        if stdin is None:
            raise RuntimeError("harness stdin pipe is unavailable")
        stdin.write(b"quit\n")
        stdin.flush()
        self.proc.wait(timeout=5)

    def close(self, timeout: float = 5.0) -> None:
        """Stop this exact child, escalating from REPL quit to TERM to KILL."""
        if self.proc.poll() is not None:
            return
        try:
            self.quit()
        except (BrokenPipeError, OSError, ValueError, subprocess.TimeoutExpired):
            pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=timeout)

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()
