"""Process transport and response framing for the embedded harness REPL."""

from __future__ import annotations

import os
import re
import select
import subprocess
import time
from collections.abc import Callable, Mapping
from contextlib import ExitStack
from typing import Self

from harness_paths import REPO_ROOT

BOOT_TIMEOUT_SECONDS = 900.0

ReadLine = Callable[[float], str | None]

_OK_END_COMMANDS = {
    "actors",
    "az_fog",
    "az_linkjoints",
    "diag",
    "hits",
    "pchits",
    "snapshot",
    "titleactors",
    "watches",
}

_BARE_OK_COMMANDS = {
    "help",
    "input",
    "loadstate",
    "quit",
    "savestate",
    "w8",
    "w16",
    "w32",
}
_HEX_OK_WIDTHS = {"r8": 2, "r16": 4, "r32": 8, "scene": 4}


def _uses_ok_end(command: str) -> bool:
    words = command.split()
    return bool(words) and (
        words[0] in _OK_END_COMMANDS or words[:2] == ["force", "titletime_read"]
    )


def _is_ok_terminal(command: str, line: str) -> bool:
    """Return whether *line* is the acknowledgement for *command*.

    The harness can emit game diagnostics and a prior long-running command's
    acknowledgement before the current reply.  Its protocol has no request
    IDs, so use the stable command label (or the few documented bare/typed
    reply shapes) to keep those lines from shifting the transaction stream.
    """
    words = command.split()
    if not words:
        return False
    name = words[0]
    if name in _BARE_OK_COMMANDS:
        return line == "ok"
    if width := _HEX_OK_WIDTHS.get(name):
        return re.fullmatch(rf"ok 0x[0-9a-fA-F]{{{width}}}", line) is not None
    if name == "playstate":
        return (
            re.fullmatch(r"ok 0x[0-9a-fA-F]{8} mode=(?:play|title)", line) is not None
        )
    if name == "gameplay":
        return line in {"ok yes", "ok no"}
    if name == "mem":
        if len(words) < 3:
            return False
        try:
            byte_count = int(words[2], 0)
        except ValueError:
            return False
        if byte_count < 0:
            return False
        expected = "ok" if byte_count == 0 else rf"ok [0-9a-fA-F]{{{2 * byte_count}}}"
        return re.fullmatch(expected, line) is not None
    if name == "soh_ctlflags":
        return line.startswith("ok stateFlags1=")
    if name == "soh_wallinfo":
        return line.startswith("ok bgFlags=")
    if name in {"compare", "sweep"} and len(words) >= 2:
        expected = f"ok {name} {words[1]}"
        return line == expected or line.startswith(f"{expected} ")
    if name == "force" and len(words) >= 2:
        expected_words = (
            words[:3] if words[1] == "bossfd_fault" and len(words) >= 3 else words[:2]
        )
        expected = f"ok {' '.join(expected_words)}"
        return line == expected or line.startswith(f"{expected} ")
    return line == f"ok {name}" or line.startswith(f"ok {name} ")


def _read_streaming_response(
    read_line: ReadLine,
    command: str,
    per_line_timeout: float,
    peek_timeout: float,
) -> list[str]:
    """Collect one harness response using its three supported frame shapes.

    Replies are either one ``ok`` line, a command-defined stream ending in
    ``ok end``, or a labeled header followed by a named ``ok``/``err``
    terminator. Command shape is authoritative: timing cannot distinguish a
    stream from a one-line reply followed by asynchronous diagnostic output.
    """
    del peek_timeout
    uses_ok_end = _uses_ok_end(command)
    lines: list[str] = []

    while True:
        line = read_line(per_line_timeout)
        if line is None:
            last = lines[-1] if lines else "<none>"
            raise TimeoutError(
                f"send_multiline({command!r}): no line for {per_line_timeout}s; "
                f"got {len(lines)} lines so far (last: {last!r})"
            )
        line = line.rstrip()
        lines.append(line)
        if line.startswith("err "):
            return lines
        if uses_ok_end and line == "ok end":
            return lines
        if not uses_ok_end and _is_ok_terminal(command, line):
            return lines


class Harness:
    """Own one harness subprocess and its line-oriented REPL transport."""

    def __init__(self, cmd: list[str], environment: Mapping[str, str] | None = None):
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
                env=environment,
            )
        self._buf = b""
        # The launcher may configure and build the harness before it execs the
        # binary.  Do not close that build at the ordinary command timeout:
        # an interrupted Ninja run leaves its dependency log incomplete and
        # turns the next launch into another full rebuild.
        line = self._readline(timeout=BOOT_TIMEOUT_SECONDS)
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

    def send(self, command: str, *, per_line_timeout: float = 60.0) -> str:
        """Send one command and return its terminal response line.

        The emulated game may print diagnostics while a command is running.
        Reading only the first line leaves the actual acknowledgement queued,
        which shifts every later command/reply pair.  Use the same framing as
        ``send_multiline`` and discard only the already-consumed diagnostics.
        """
        stdin = self.proc.stdin
        if stdin is None:
            raise RuntimeError("harness stdin pipe is unavailable")
        stdin.write((command.rstrip() + "\n").encode())
        stdin.flush()
        return _read_streaming_response(
            self._readline, command, per_line_timeout=per_line_timeout, peek_timeout=0.0
        )[-1]

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
