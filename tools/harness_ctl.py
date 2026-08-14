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
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable, Optional

REPO_ROOT   = Path(__file__).resolve().parent.parent
HARNESS_SH  = REPO_ROOT / "tools" / "soh3d_harness.sh"
HARNESS_BIN = REPO_ROOT / "Azahar" / "build-harness" / "bin" / "Release" / "soh3d_harness"


def _load_repo_environment() -> None:
    """Load every repo ``.env`` setting without overriding the caller."""
    env_file = REPO_ROOT / ".env"
    if not env_file.is_file():
        return

    # Let bash parse the same shell syntax accepted by the launch scripts.  `set
    # -a` makes plain NAME=value entries visible to the harness too; requiring
    # every local setting to spell `export` made harness_ctl silently ignore
    # otherwise valid .env configuration.  Merge instead of replacing so an
    # explicit process environment remains the highest-priority source.
    result = subprocess.run(
        ["bash", "-c", 'set -a; source "$1"; env -0',
         "harness-env", str(env_file)],
        check=True, stdout=subprocess.PIPE,
    )
    for entry in result.stdout.split(b"\0"):
        if not entry or b"=" not in entry:
            continue
        raw_name, raw_value = entry.split(b"=", 1)
        name = os.fsdecode(raw_name)
        if name not in os.environ:
            os.environ[name] = os.fsdecode(raw_value)


def _provision_rom_environment() -> None:
    """Apply the launcher's env -> repo .env -> drop-in ROM contract."""
    _load_repo_environment()
    script = REPO_ROOT / "tools" / "rom_provision.sh"
    command = (
        'source "$1"; zelda3d_provision_roms "$2"; '
        'printf "%s\\0%s\\0" "${ZELDA3D_OOT3D_ROM-}" "${ZELDA3D_OOT_ROM-}"'
    )
    result = subprocess.run(
        ["bash", "-c", command, "harness-rom-provision", str(script), str(REPO_ROOT)],
        check=True, stdout=subprocess.PIPE,
    )
    values = result.stdout.split(b"\0")
    for name, value in zip(("ZELDA3D_OOT3D_ROM", "ZELDA3D_OOT_ROM"), values):
        if value:
            os.environ[name] = os.fsdecode(value)

    rom = os.environ.get("ZELDA3D_OOT3D_ROM")
    if not rom:
        raise RuntimeError(
            "OoT3D ROM provisioning scanned the process environment, repo .env, "
            "and repo-root *.3ds files; matched 0"
        )
    if not Path(rom).is_file():
        raise RuntimeError(f"provisioned OoT3D ROM does not exist: {rom}")

# ---------------------------------------------------------------------------
# OracleCache — persistent cache of deterministic embedded-Azahar output.
# ---------------------------------------------------------------------------
#
# The oracle's output at az (Azahar) frame N is fully determined by three
# inputs: the savestate loaded at t=0, the ROM bytes, and whatever the
# soh3d_harness Azahar patches (tools/soh3d_harness/AZAHAR_PATCH.md) do to
# rendering. Given those three held fixed, re-running Az to frame N always
# produces the same pixels/state — so it's cacheable and repeated A/B or
# probe runs shouldn't pay the Az boot+step cost again for a frame already
# captured. See docs/parity-workflow.md "Oracle data cache" and
# debug_journal/2026-07-11-oracle-cache.md for the design writeup.
#
# Cache lives at scratch/oracle_cache/<key>/ (gitignored — ROM-derived pixel
# data must never be committed). <key> = sha256(savestate)[:16] +
# sha256(rom)[:16] + a marker derived from AZAHAR_PATCH.md's heading list,
# so any savestate change, ROM swap, or Azahar-patch edit mints a fresh,
# independent cache context instead of silently serving stale frames.

CACHE_ROOT = REPO_ROOT / "scratch" / "oracle_cache"
AZAHAR_PATCH_MD = REPO_ROOT / "tools" / "soh3d_harness" / "AZAHAR_PATCH.md"


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _patch_marker() -> str:
    """Short marker derived from AZAHAR_PATCH.md's heading list (patch
    count + a hash of the heading text). Changes whenever a patch is
    added/removed/renamed — a coarse but cheap proxy for "did the Azahar
    rendering patches change" without diffing the (gitignored, not always
    present) Azahar tree itself."""
    if not AZAHAR_PATCH_MD.exists():
        return "nopatchmd"
    headings = re.findall(r'^#{1,2}\s.*$', AZAHAR_PATCH_MD.read_text(), re.MULTILINE)
    h = hashlib.sha256("\n".join(headings).encode()).hexdigest()[:8]
    return f"p{len(headings)}-{h}"


def cache_key(savestate: Path, rom: Optional[Path] = None) -> tuple[str, dict]:
    """Compute the cache key + full metadata (for auditability — stored
    verbatim in each cache context's index.json)."""
    savestate = Path(savestate)
    savestate_sha = _sha256_file(savestate)[:16] if savestate.exists() else "nostate"
    rom_path = Path(rom) if rom else (
        Path(os.environ["ZELDA3D_OOT3D_ROM"]) if os.environ.get("ZELDA3D_OOT3D_ROM") else None)
    rom_sha = _sha256_file(rom_path)[:16] if rom_path and rom_path.exists() else "norom"
    patch = _patch_marker()
    key = f"{savestate_sha}_{rom_sha}_{patch}"
    meta = {
        "key": key,
        "savestate_path": str(savestate),
        "savestate_sha256_16": savestate_sha,
        "rom_path": str(rom_path) if rom_path else None,
        "rom_sha256_16": rom_sha,
        "azahar_patch_marker": patch,
        "azahar_patch_md": str(AZAHAR_PATCH_MD),
    }
    return key, meta


class OracleCache:
    """get/put cache for deterministic embedded-Azahar oracle output.

    Two kinds of entries, both under scratch/oracle_cache/<key>/:
      - frames: PNG captures at a given az (Azahar) frame number, e.g. the
        `<name>.az.ppm` snapshots title_ab.py produces (frames/az<N>.png).
      - probes: JSON blobs for structured, deterministic probe commands
        (az camera eye/at, az_daytime, titleactors, vsuni_log, az_fog, ...)
        keyed by (probe_name, az_frame, args).

    index.json at the context root records the full key metadata (for
    auditability) plus a manifest of every cached frame/probe.
    """

    def __init__(self, savestate: Path, rom: Optional[Path] = None):
        self.key, self.meta = cache_key(savestate, rom)
        self.dir = CACHE_ROOT / self.key
        self.frames_dir = self.dir / "frames"
        self.probes_dir = self.dir / "probes"
        self.index_path = self.dir / "index.json"
        self._index: Optional[dict] = None

    def _load_index(self) -> dict:
        if self._index is not None:
            return self._index
        if self.index_path.exists():
            self._index = json.loads(self.index_path.read_text())
        else:
            self._index = {"meta": self.meta, "frames": {}, "probes": {}}
        return self._index

    def _save_index(self) -> None:
        self.dir.mkdir(parents=True, exist_ok=True)
        idx = self._load_index()
        idx["meta"] = self.meta
        self.index_path.write_text(json.dumps(idx, indent=2, sort_keys=True))

    # -- frames --------------------------------------------------------

    def get_frame(self, az_frame: int) -> Optional[Path]:
        idx = self._load_index()
        entry = idx["frames"].get(str(az_frame))
        if not entry:
            return None
        p = self.dir / entry["file"]
        return p if p.exists() else None

    def put_frame(self, az_frame: int, src_image_path) -> Path:
        from PIL import Image
        idx = self._load_index()
        self.frames_dir.mkdir(parents=True, exist_ok=True)
        dst = self.frames_dir / f"az{az_frame}.png"
        Image.open(src_image_path).convert("RGB").save(dst)
        idx["frames"][str(az_frame)] = {
            "file": str(dst.relative_to(self.dir)),
            "captured": time.time(),
            "source": str(src_image_path),
        }
        self._save_index()
        return dst

    # -- probes ----------------------------------------------------------

    @staticmethod
    def _probe_key(probe_name: str, az_frame: int, args: Optional[dict]) -> str:
        args_s = json.dumps(args or {}, sort_keys=True)
        h = hashlib.sha256(args_s.encode()).hexdigest()[:10]
        return f"{probe_name}_{az_frame}_{h}"

    def get_probe(self, probe_name: str, az_frame: int, args: Optional[dict] = None):
        idx = self._load_index()
        pk = self._probe_key(probe_name, az_frame, args)
        entry = idx["probes"].get(pk)
        if not entry:
            return None
        p = self.dir / entry["file"]
        if not p.exists():
            return None
        return json.loads(p.read_text())

    def put_probe(self, probe_name: str, az_frame: int, args: Optional[dict], data) -> None:
        idx = self._load_index()
        pk = self._probe_key(probe_name, az_frame, args)
        self.probes_dir.mkdir(parents=True, exist_ok=True)
        dst = self.probes_dir / f"{pk}.json"
        dst.write_text(json.dumps(data, indent=2, sort_keys=True, default=str))
        idx["probes"][pk] = {
            "file": str(dst.relative_to(self.dir)),
            "probe_name": probe_name,
            "az_frame": az_frame,
            "args": args or {},
            "captured": time.time(),
        }
        self._save_index()

    # -- housekeeping ------------------------------------------------------

    def stats(self) -> dict:
        idx = self._load_index()
        total_bytes = 0
        if self.dir.exists():
            for root, _dirs, files in os.walk(self.dir):
                for fn in files:
                    total_bytes += (Path(root) / fn).stat().st_size
        return {
            "key": self.key,
            "dir": str(self.dir),
            "n_frames": len(idx["frames"]),
            "n_probes": len(idx["probes"]),
            "bytes": total_bytes,
        }

    def invalidate(self) -> None:
        import shutil
        if self.dir.exists():
            shutil.rmtree(self.dir)
        self._index = None


class Harness:
    def __init__(self, cmd: list[str]):
        # bufsize=0 → unbuffered binary I/O so Python's TextIOWrapper doesn't
        # gobble multiple lines into an internal buffer that select() on the
        # raw fd can't see (was breaking every multi-line peek).
        self.proc = subprocess.Popen(
            cmd, cwd=str(REPO_ROOT),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=(open(os.environ["HARNESS_STDERR"], "wb") if os.environ.get("HARNESS_STDERR")
                    else subprocess.DEVNULL),
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

    def close(self, timeout: float = 5.0) -> None:
        """Terminate the harness subprocess cleanly and unconditionally.

        Tries the graceful `quit` REPL command first (same as quit()), then
        falls back to SIGTERM, then SIGKILL if it still hasn't exited. Only
        ever signals THIS instance's own tracked self.proc (a specific pid
        this object Popen'd) -- never pkill/pgrep by name/pattern, which
        risks self-matching the calling shell (see the safe-kill skill).
        Safe to call multiple times / after the process already exited."""
        if self.proc.poll() is not None:
            return
        try:
            self.quit()
        except Exception:
            pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=timeout)

    def __enter__(self):  return self
    def __exit__(self, *a): self.close()


def _ensure_headless_env() -> None:
    """Force the harness onto a private Xvfb display so neither its SBS
    window nor the embedded SoH3D's Vulkan window reaches the user's
    Wayland desktop. Idempotent — once a process has set these, child
    spawns inherit them. Mirrors tools/zelda3d_game.sh's setup_headless,
    required because SDL3-GPU's window creation ignores libultraship's
    SOH_HEADLESS knob (the GPU backend has no headless path; only the
    legacy gfx_sdl3.cpp one does). ZELDA3D_HEADLESS=0 opts out (debugging
    only — the SBS window will then appear on the user's display)."""
    if os.environ.get("ZELDA3D_HEADLESS", "1") != "1":
        return
    disp = os.environ.get("ZELDA3D_HEADLESS_DISPLAY", ":99")
    # Bring up Xvfb on :99 if it isn't already up.
    import subprocess as _sp
    try:
        _sp.run(["xdpyinfo"], check=True, env={**os.environ, "DISPLAY": disp},
                 stdout=_sp.DEVNULL, stderr=_sp.DEVNULL)
    except Exception:
        import pathlib as _pl
        log_dir = REPO_ROOT / "scratch" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        log = open(log_dir / "xvfb_harness.log", "wb")
        _sp.Popen(["Xvfb", disp, "-screen", "0", "1920x1080x24"],
                  stdout=log, stderr=_sp.STDOUT, start_new_session=True)
        import time as _t
        up = False
        for _ in range(20):
            try:
                _sp.run(["xdpyinfo"], check=True, env={**os.environ, "DISPLAY": disp},
                         stdout=_sp.DEVNULL, stderr=_sp.DEVNULL)
                up = True
                break
            except Exception:
                _t.sleep(0.5)
        if not up:
            raise RuntimeError(f"Xvfb failed to come up on {disp}")
    os.environ["DISPLAY"]            = disp
    os.environ["XAUTHORITY"]         = "/dev/null"
    os.environ["SDL_VIDEODRIVER"]    = "x11"
    os.environ["SDL_AUDIODRIVER"]    = "dummy"
    os.environ["SOH3D_HARNESS_HEADLESS"] = "1"  # disable harness SBS window
    os.environ.pop("WAYLAND_DISPLAY", None)


def _ensure_scalable_malloc() -> None:
    """Preload jemalloc (else tcmalloc) into the harness so Azahar's headless
    SOFTWARE rasterizer doesn't livelock. That rasterizer runs a multi-threaded
    per-scanline worker pool that allocates on one thread and frees on another;
    under a complex scene with motion (Kokiri Forest while Link walks) glibc's
    malloc arena lock serializes those cross-thread alloc/frees, each frame slows
    ~10x, and the 5s frame-watchdog kills the harness — the walk/run oracle-sweep
    hang (gdb 2026-07-15: SwRenderer work threads all in _int_free_chunk on the
    arena lock). A per-thread-cache allocator removes the contention: ~seconds ->
    ~70ms/frame. os.environ is inherited by the Popen below. No-op if the caller
    already set LD_PRELOAD or if neither lib is present."""
    if os.environ.get("LD_PRELOAD"):
        return
    import glob as _glob
    cands = ["/usr/lib64/libjemalloc.so.2", "/usr/lib/x86_64-linux-gnu/libjemalloc.so.2"]
    cands += _glob.glob("/usr/lib*/libjemalloc.so.2") + _glob.glob("/lib*/libjemalloc.so.2")
    cands += _glob.glob("/usr/lib*/libtcmalloc_minimal.so.4")
    for c in cands:
        if c and os.path.exists(c):
            os.environ["LD_PRELOAD"] = c
            return


def spawn(save_state: Optional[str] = None) -> Harness:
    if not HARNESS_BIN.exists() and not HARNESS_SH.exists():
        raise RuntimeError(f"soh3d_harness not found; expected {HARNESS_BIN} or {HARNESS_SH}")
    _provision_rom_environment()
    _ensure_headless_env()
    _ensure_scalable_malloc()
    cmd = [str(HARNESS_BIN)] if HARNESS_BIN.exists() else [str(HARNESS_SH)]
    h = Harness(cmd)
    if save_state:
        resp = h.send(f"loadstate {save_state}")
        if not resp.startswith("ok"):
            raise RuntimeError(f"loadstate failed: {resp}")
    return h


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


def in_gameplay(h: Harness) -> bool:
    """True only in a real gameplay scene (harness `gameplay` command).

    Reads gPlayState and rejects the title demo, so a driver cannot conclude
    "we're in game" while sitting on the title and then warp/snapshot there.
    (`playstate` cannot answer this: it deliberately falls back to the title's
    PlayState so introspection works there, so it reports ok on the title.)
    """
    r = (h.send("gameplay") or "").strip()
    return r.startswith("ok") and r.split()[-1] == "yes"


# ---------------------------------------------------------------------------
# Drive to gameplay — the SoH-equivalent warp entry point
# ---------------------------------------------------------------------------
#
# WHY THIS EXISTS. OoT3D's `warp` (nextEntranceIndex @ play+0x5c32 +
# transitionTrigger @ play+0x5c2d = TRANS_TRIGGER_START, RE'd in
# oot3d-decomp/docs/ram_map.md) is the same mechanism SoH warps with, and the
# harness has always implemented it correctly. It cannot work from the TITLE:
# no save file is loaded there, so the transition driver has nothing to spawn
# into. Everything that looked like "the harness can't warp" was really "we
# were still on the title and `playstate` said ok anyway".
#
# So warping like SoH = being in a loaded save first. That is a one-time cost:
# drive the title once, snapshot the emulator, and every later session starts
# with `loadstate` + `warp` and never touches the input path again.
GAMEPLAY_STATE = REPO_ROOT / "scratch" / "gameplay_settled.state"

# gSaveContext is a fixed .bss global (0x00587958, oot3d-decomp docs/ram_map.md);
# dayTime is the u16 at +0x0C. Global, so it is writable at the title too.
GSAVECONTEXT_VA = 0x00587958
GSAVECONTEXT_DAYTIME_VA = GSAVECONTEXT_VA + 0x0C

# skyboxTime — the clock the ENVIRONMENT actually reads. Writing dayTime alone
# moves the game clock but leaves the sky and ambient lighting untouched, which
# silently invalidates any lighting A/B (found 2026-07-22: the oracle rendered
# dusk at dayTime=0x6000 while Zelda3D rendered morning). N64 keeps this at
# SaveContext+0x141A; OoT3D moved it to +0x15A8, located by scanning gSaveContext
# for the u16 mirroring dayTime.
GSAVECONTEXT_SKYBOXTIME_VA = GSAVECONTEXT_VA + 0x15A8


def read_time_of_day(h) -> tuple[int, int]:
    """(dayTime, skyboxTime) as the oracle currently holds them."""
    def rd(va: int) -> int:
        resp = h.send(f"r16 0x{va:08x}")
        for tok in reversed(resp.replace(",", " ").split()):
            try:
                return int(tok, 0) & 0xFFFF
            except ValueError:
                continue
        raise RuntimeError(f"read_time_of_day: could not parse {resp!r}")
    return rd(GSAVECONTEXT_DAYTIME_VA), rd(GSAVECONTEXT_SKYBOXTIME_VA)


def set_time_of_day(h, daytime: int, settle: int = 8, tolerance: int = 0x180) -> None:
    """Set BOTH clocks, so the render actually follows. See GSAVECONTEXT_SKYBOXTIME_VA.

    THE CLOCK KEEPS RUNNING, and that is what makes this function easy to misuse. It used to
    `run 120` after writing, and callers then ran hundreds more settle frames before capturing — so
    the frame was taken at a sun position far from the one requested. That produced an ours-vs-oracle
    shadow comparison at MISMATCHED lighting which was mistaken for a renderer defect and cost a
    full investigation (instrument I001, 2026-07-28). Set the clock LAST, advance only enough frames
    for the environment to pick it up, and VERIFY — a silent drift is indistinguishable from a
    correctly-honoured request.
    """
    h.send(f"w16 0x{GSAVECONTEXT_DAYTIME_VA:08x} 0x{daytime:04x}")
    h.send(f"w16 0x{GSAVECONTEXT_SKYBOXTIME_VA:08x} 0x{daytime:04x}")
    h.send(f"run {settle}")
    day, sky = read_time_of_day(h)
    drift = max(abs(day - daytime), abs(sky - daytime))
    if drift > tolerance:
        raise RuntimeError(
            f"set_time_of_day({daytime:#06x}) did NOT hold: dayTime={day:#06x} skyboxTime={sky:#06x} "
            f"(drift {drift:#x} > tolerance {tolerance:#x}). Any light-dependent comparison from this "
            f"frame would be measuring the clock, not the renderer — see instrument I001.")


def boot_to_gameplay(h: Harness, entrance: Optional[int] = None,
                     settle_frames: int = 180) -> bool:
    """Put the oracle in a real gameplay scene, then optionally warp there.

    Fast path: `loadstate` the cached gameplay state (no input driving at all).
    Cold path (state absent): drive the title with SHORT rapid taps — hold=4,
    release=8. The long taps harness_ctl/link_sweep used (hold=30/release=60) do
    not advance OoT3D's title/file-select at all. Once in gameplay the state is
    saved so the cold path runs at most once per machine.

    Returns True only if `gameplay` reports yes at the end.
    """
    if GAMEPLAY_STATE.exists():
        h.send(f"loadstate {GAMEPLAY_STATE}")
        h.send("run 60")
        if not in_gameplay(h):
            print(f"[harness_ctl] {GAMEPLAY_STATE.name} did not land in gameplay — "
                  "delete it to force a re-capture", file=sys.stderr)
            return False
    else:
        if not _drive_title_to_gameplay(h):
            return False
        GAMEPLAY_STATE.parent.mkdir(parents=True, exist_ok=True)
        h.send(f"savestate {GAMEPLAY_STATE}")
        print(f"[harness_ctl] captured {GAMEPLAY_STATE} — future boots skip the "
              "title entirely", file=sys.stderr)

    if entrance is not None:
        r = (h.send(f"warp 0x{entrance:x}") or "").strip()
        if not r.startswith("ok"):
            print(f"[harness_ctl] warp 0x{entrance:x} failed: {r}", file=sys.stderr)
            return False
        for _ in range(max(1, settle_frames // 60)):
            h.send("run 60")
        if not in_gameplay(h):
            print(f"[harness_ctl] warp 0x{entrance:x} left gameplay", file=sys.stderr)
            return False
    return True


def _drive_title_to_gameplay(h: Harness, rounds: int = 6) -> bool:
    """Cold path: mash through logo/title/file-select with short rapid taps."""
    h.send("run 300")
    if in_gameplay(h):
        return True
    for btn in (BTN_START, BTN_A):
        for _ in range(rounds):
            for _ in range(12):
                tap(h, btn, hold=4, release=8)
            h.send("run 60")
            if in_gameplay(h):
                return True
    print("[harness_ctl] never reached gameplay from the title", file=sys.stderr)
    return False


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
    """Warp to an entrance and report the scene it landed in.

    Goes through boot_to_gameplay so the precondition is guaranteed: OoT3D's
    warp writes nextEntranceIndex + transitionTrigger, and with no save loaded
    (i.e. at the title) there is nothing for the transition driver to spawn
    into. The previous version waited on `playstate`, which answers ok at the
    title, then warped there and reported the unchanged scene as a timeout.
    """
    before = h.send("scene")
    if not boot_to_gameplay(h, entrance=args.entrance, settle_frames=args.settle_frames):
        print("[harness_ctl] warp failed — see above", file=sys.stderr)
        sys.exit(1)
    print(f"[harness_ctl] scene {before} -> {h.send('scene')}", file=sys.stderr)


def cmd_boot_to_play(args, h: Harness) -> None:
    """Put the oracle in a real gameplay scene (optionally at --entrance).

    Thin CLI wrapper over boot_to_gameplay() so the shell path and the library
    path cannot drift apart. The old implementation soaked, tapped
    hold=30/release=60, and stopped when `playstate` answered ok — a schedule
    that never advances OoT3D's title and a check that reports ok while still
    on it, so it "succeeded" without ever leaving the title screen.
    """
    ok = boot_to_gameplay(h, entrance=args.entrance)
    print(f"[harness_ctl] boot-to-play: {'ok' if ok else 'FAILED'}", file=sys.stderr)
    if not ok:
        sys.exit(1)


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

    w = sub.add_parser("warp", help="reach gameplay, then warp to entrance")
    w.add_argument("entrance", type=lambda s: int(s, 0))
    w.add_argument("--settle-frames", type=int, default=180,
                   help="frames to run after the warp (default 180)")
    w.set_defaults(func=cmd_warp)

    bt = sub.add_parser("boot-to-play",
                        help="loadstate/capture a gameplay state, then optionally warp")
    bt.add_argument("--entrance", type=lambda s: int(s, 0), default=None)
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
