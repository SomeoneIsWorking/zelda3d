#!/usr/bin/env python3
"""harness_ctl.py — Python driver for the soh3d_harness REPL.

The soh3d_harness C++ binary owns the OoT3D RE knowledge — offsets for
gPlayState, transitionTrigger, nextEntranceIndex, actor-list layout —
and exposes it as first-class REPL commands (warp, actors, scene, ...
plus low-level r*/w*/mem/run/loadstate/savestate). See
tools/soh3d_harness/main.cpp for the wire protocol.

This script is the convenience layer: process spawn + boot handshake +
interactive REPL + a few multi-step flows (wait for gPlayState to
populate, then warp; pump-and-observe scene changes). New RE mechanisms
belong in the C++ harness, not here — this is for free-form poking.

Usage:
    tools/harness_ctl.py repl                     # interactive shell
    tools/harness_ctl.py send "warp 0x01"         # single command
    tools/harness_ctl.py warp 0x01                # wait for playstate, then warp
    tools/harness_ctl.py peek 0x0050AF34 32       # hex-dump n bytes at VA

    tools/harness_ctl.py --save-state s.state repl
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Optional

REPO_ROOT   = Path(__file__).resolve().parent.parent
HARNESS_SH  = REPO_ROOT / "tools" / "soh3d_harness.sh"
HARNESS_BIN = REPO_ROOT / "Azahar" / "build-libretro" / "bin" / "Release" / "soh3d_harness"


class Harness:
    def __init__(self, cmd: list[str]):
        # bufsize=0 → unbuffered binary I/O so Python's TextIOWrapper doesn't
        # gobble multiple lines into an internal buffer that select() on the
        # raw fd can't see (was breaking every multi-line peek).
        self.proc = subprocess.Popen(
            cmd, cwd=str(REPO_ROOT),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            bufsize=0,
        )
        self._buf = b""
        line = self._readline()
        if line.strip() != "boot succeeded":
            raise RuntimeError(f"harness did not boot: got {line!r}")

    def _readline(self, timeout: float = 60.0):
        """Read one \\n-terminated line from the raw fd, honoring `timeout`.
        Returns the line (stripped of the newline) as str, or None on timeout.
        Raises on pipe close."""
        import os, select
        fd = self.proc.stdout.fileno()
        import time
        deadline = time.time() + timeout
        while b"\n" not in self._buf:
            remaining = max(0.0, deadline - time.time())
            r, _, _ = select.select([fd], [], [], remaining)
            if not r:
                return None
            chunk = os.read(fd, 8192)
            if not chunk:
                raise RuntimeError("harness closed stdout unexpectedly")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode("utf-8", errors="replace")

    def _peek_has_data(self) -> bool:
        """True if there's already buffered data or the pipe has bytes ready."""
        import select
        if self._buf:
            return True
        fd = self.proc.stdout.fileno()
        r, _, _ = select.select([fd], [], [], 0)
        return bool(r)

    def send(self, cmd: str) -> str:
        """Send one command; return the first response line (chomped)."""
        assert self.proc.stdin is not None
        self.proc.stdin.write((cmd.rstrip() + "\n").encode())
        self.proc.stdin.flush()
        line = self._readline()
        if line is None:
            raise TimeoutError(f"send({cmd!r}): no response within timeout")
        return line.rstrip()

    def send_multiline(self, cmd: str, per_line_timeout: float = 30.0,
                       peek_timeout: float = 0.2) -> list[str]:
        """Send a command that may stream multiple lines.

        Harness reply shapes:
          A) single-line: `ok <payload>` (e.g. `mem`, `r32`, `playstate`)
          B) streaming header first: `ok <name> <N>` then body lines then
             `ok end` (e.g. `actors`, `titleactors`)
          C) title/label header first (does NOT start with `ok `): body,
             then `ok <terminator>` (e.g. `compare scene:` ... `ok compare scene`)

        Distinguishing A from B needs a peek — after the first `ok …` line
        arrives, wait `peek_timeout` for another line. If none comes, it
        was A. If one arrives, keep reading until `ok end`.
        """
        assert self.proc.stdin is not None
        assert self.proc.stdout is not None
        self.proc.stdin.write((cmd.rstrip() + "\n").encode())
        self.proc.stdin.flush()
        lines: list[str] = []

        def _read_line(timeout):
            line = self._readline(timeout=timeout)
            if line is None:
                return None
            return line.rstrip()

        first = _read_line(per_line_timeout)
        if first is None:
            raise TimeoutError(f"send_multiline({cmd!r}): no first line in {per_line_timeout}s")
        lines.append(first)

        first_ok = first.startswith("ok ")
        if first_ok:
            # (A) or (B) — peek for a body line.
            peek = _read_line(peek_timeout)
            if peek is None:
                return lines  # (A)
            lines.append(peek)
            # (B): keep reading until `ok end` (or any `ok`/`err` terminator).
        # For (C), we haven't started reading the body yet — first line was
        # the header (not an "ok" line), so we already know we're streaming.

        while True:
            line = _read_line(per_line_timeout)
            if line is None:
                raise TimeoutError(
                    f"send_multiline({cmd!r}): no line for {per_line_timeout}s; "
                    f"got {len(lines)} lines so far "
                    f"(last: {lines[-1]!r})"
                )
            lines.append(line)
            if line == "ok end":
                return lines
            # For shape (C), the terminator is `ok <label>` (not `ok end`).
            # Recognize it if the header did NOT start with `ok`.
            if not first_ok and (line.startswith("ok ") or line.startswith("err ")):
                return lines

    def quit(self) -> None:
        try:
            self.proc.stdin.write(b"quit\n")
            self.proc.stdin.flush()
        except Exception:
            pass
        self.proc.wait(timeout=5)

    def __enter__(self):  return self
    def __exit__(self, *a): self.quit()


def spawn(save_state: Optional[str] = None) -> Harness:
    if not HARNESS_BIN.exists() and not HARNESS_SH.exists():
        raise RuntimeError(f"soh3d_harness not found; expected {HARNESS_BIN} or {HARNESS_SH}")
    cmd = [str(HARNESS_BIN)] if HARNESS_BIN.exists() else [str(HARNESS_SH)]
    h = Harness(cmd)
    if save_state:
        resp = h.send(f"loadstate {save_state}")
        if not resp.startswith("ok"):
            raise RuntimeError(f"loadstate failed: {resp}")
    return h


def wait_for_playstate(h: Harness, poll_every: int = 60,
                       timeout_frames: int = 3600) -> None:
    """Advance frames until `playstate` returns ok (game is in Play mode)."""
    elapsed = 0
    while elapsed < timeout_frames:
        h.send(f"run {poll_every}")
        elapsed += poll_every
        r = h.send("playstate")
        if r.startswith("ok"):
            print(f"[harness_ctl] playstate populated at frame ~{elapsed}: {r}",
                  file=sys.stderr)
            return
    raise RuntimeError(f"gPlayState stayed 0 for {timeout_frames} frames — "
                       "reach gameplay first (loadstate, or drive input)")


# RETRO_DEVICE_ID_JOYPAD bit indices.
BTN_B      = 1 << 0
BTN_Y      = 1 << 1
BTN_SELECT = 1 << 2
BTN_START  = 1 << 3
BTN_UP     = 1 << 4
BTN_DOWN   = 1 << 5
BTN_LEFT   = 1 << 6
BTN_RIGHT  = 1 << 7
BTN_A      = 1 << 8
BTN_X      = 1 << 9


def tap(h: Harness, mask: int, hold: int = 30, release: int = 60) -> None:
    """Press <mask>, run <hold> frames, release, run <release> frames."""
    h.send(f"input 0x{mask:x}")
    h.send(f"run {hold}")
    h.send("input 0")
    h.send(f"run {release}")


def poll_playstate(h: Harness) -> Optional[str]:
    """Return the `playstate` response ONLY if it's ok (else None)."""
    r = h.send("playstate")
    return r if r.startswith("ok") else None


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_send(args, h: Harness) -> None:
    print(h.send(args.line))


def cmd_repl(args, h: Harness) -> None:
    print("harness_ctl repl — 'quit' to exit. Multiline commands ('actors', etc) "
          "read until 'ok end'.", file=sys.stderr)
    multiline = {"actors"}
    while True:
        try:
            line = input("> ")
        except (EOFError, KeyboardInterrupt):
            break
        stripped = line.strip()
        if not stripped:
            continue
        if stripped == "quit":
            break
        head = stripped.split()[0]
        if head in multiline:
            for out in h.send_multiline(stripped):
                print(out)
        else:
            print(h.send(stripped))


def cmd_warp(args, h: Harness) -> None:
    wait_for_playstate(h, timeout_frames=args.wait_frames)
    scene_before = h.send("scene")
    print(f"[harness_ctl] before warp: {scene_before}", file=sys.stderr)
    r = h.send(f"warp 0x{args.entrance:x}")
    print(f"[harness_ctl] warp response: {r}", file=sys.stderr)
    if not r.startswith("ok"):
        return
    for burst in range(args.settle_bursts):
        h.send(f"run {args.settle_step}")
        r = h.send("scene")
        if r.startswith("ok") and r != scene_before:
            frames_ran = (burst + 1) * args.settle_step
            print(f"[harness_ctl] TARGET SCENE REACHED {r} "
                  f"after ~{frames_ran} settle frames", file=sys.stderr)
            return
    print(f"[harness_ctl] scene did not change within "
          f"{args.settle_bursts * args.settle_step} settle frames", file=sys.stderr)


def cmd_boot_to_play(args, h: Harness) -> None:
    """Drive inputs from cold boot to gameplay (New Game -> Kokiri Forest).

    Timing: 3DS logos then title screen emerges around ~1000 frames of emu.
    We soak, then tap START to advance past title -> file select, then
    tap A a few times: File 1 -> New Game / Confirm -> intro cutscene ->
    spawn. During each idle burst we poll playstate; the moment it
    populates, we're done. `--soak <N>` sets the initial cold-boot soak.
    """
    print(f"[harness_ctl] cold-boot soak {args.soak} frames...", file=sys.stderr)
    h.send(f"run {args.soak}")
    if (r := poll_playstate(h)):
        print(f"[harness_ctl] playstate already populated after soak: {r}",
              file=sys.stderr)
        return

    # Tap START, then A a bunch to blast through menus + cutscene.
    schedule = ([BTN_START] * 3) + ([BTN_A] * args.a_presses)
    for i, btn in enumerate(schedule):
        tap(h, btn, hold=args.hold, release=args.release)
        if (r := poll_playstate(h)):
            print(f"[harness_ctl] playstate populated after tap {i+1}/{len(schedule)}: {r}",
                  file=sys.stderr)
            return
    print("[harness_ctl] playstate still not populated after scripted taps — "
          "extend the schedule or hand off to interactive REPL", file=sys.stderr)


def cmd_peek(args, h: Harness) -> None:
    remaining = args.n
    va = args.va
    while remaining > 0:
        chunk = min(remaining, 4096)
        resp = h.send(f"mem 0x{va:08x} {chunk}")
        if not resp.startswith("ok "):
            print(resp)
            return
        blob = bytes.fromhex(resp[3:])
        for off in range(0, len(blob), 16):
            row = blob[off:off + 16]
            hex_ = " ".join(f"{b:02x}" for b in row)
            asc  = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
            print(f"0x{va + off:08x}  {hex_:<47}  {asc}")
        va += chunk
        remaining -= chunk


def main(argv: Iterable[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--save-state", help="loadstate this file after boot")
    sub = p.add_subparsers(dest="cmd", required=True)

    rp = sub.add_parser("repl", help="interactive REPL over the wire protocol")
    rp.set_defaults(func=cmd_repl)

    sd = sub.add_parser("send", help="send one command, print response")
    sd.add_argument("line")
    sd.set_defaults(func=cmd_send)

    w = sub.add_parser("warp", help="wait for playstate, then warp to entrance")
    w.add_argument("entrance", type=lambda s: int(s, 0))
    w.add_argument("--wait-frames", type=int, default=3600,
                   help="frames to wait for gPlayState (default 3600 = ~1min emu)")
    w.add_argument("--settle-bursts", type=int, default=60)
    w.add_argument("--settle-step",   type=int, default=30)
    w.set_defaults(func=cmd_warp)

    bt = sub.add_parser("boot-to-play",
                        help="soak past logos, then tap START+A until playstate populates")
    bt.add_argument("--soak",      type=int, default=1200)
    bt.add_argument("--a-presses", type=int, default=40)
    bt.add_argument("--hold",      type=int, default=30)
    bt.add_argument("--release",   type=int, default=60)
    bt.set_defaults(func=cmd_boot_to_play)

    pk = sub.add_parser("peek", help="hex-dump <n> bytes at <va>")
    pk.add_argument("va", type=lambda s: int(s, 0))
    pk.add_argument("n",  type=int)
    pk.set_defaults(func=cmd_peek)

    args = p.parse_args(list(argv))
    with spawn(save_state=args.save_state) as h:
        args.func(args, h)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
