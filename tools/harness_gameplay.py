"""Gameplay-entry and game-state control policy for harness sessions."""

from __future__ import annotations

import sys
from pathlib import Path

from harness_paths import GAMEPLAY_STATE
from harness_transport import Harness

# RETRO_DEVICE_ID_JOYPAD bit indices.
BTN_B = 1 << 0
BTN_Y = 1 << 1
BTN_SELECT = 1 << 2
BTN_START = 1 << 3
BTN_UP = 1 << 4
BTN_DOWN = 1 << 5
BTN_LEFT = 1 << 6
BTN_RIGHT = 1 << 7
BTN_A = 1 << 8
BTN_X = 1 << 9

GSAVECONTEXT_VA = 0x00587958
GSAVECONTEXT_DAYTIME_VA = GSAVECONTEXT_VA + 0x0C
GSAVECONTEXT_SKYBOXTIME_VA = GSAVECONTEXT_VA + 0x15A8


def tap(harness: Harness, mask: int, hold: int = 30, release: int = 60) -> None:
    """Press one input mask, run, release it, and run again."""
    harness.send(f"input 0x{mask:x}")
    harness.send(f"run {hold}")
    harness.send("input 0")
    harness.send(f"run {release}")


def in_gameplay(harness: Harness) -> bool:
    """Return whether the harness reports a real non-title gameplay scene.

    ``playstate`` deliberately falls back to the title demo's PlayState, so it
    cannot establish the loaded-save precondition required by ``warp``.
    """
    response = (harness.send("gameplay") or "").strip()
    return response.startswith("ok") and response.split()[-1] == "yes"


def read_time_of_day(harness: Harness) -> tuple[int, int]:
    """Return the oracle's dayTime and independently stored skyboxTime."""

    def read_u16(address: int) -> int:
        response = harness.send(f"r16 0x{address:08x}")
        for token in reversed(response.replace(",", " ").split()):
            try:
                return int(token, 0) & 0xFFFF
            except ValueError:
                continue
        raise RuntimeError(f"read_time_of_day: could not parse {response!r}")

    return read_u16(GSAVECONTEXT_DAYTIME_VA), read_u16(GSAVECONTEXT_SKYBOXTIME_VA)


def set_time_of_day(
    harness: Harness, daytime: int, settle: int = 8, tolerance: int = 0x180
) -> None:
    """Set both game clocks and reject a frame whose lighting clock drifted.

    Environment lighting reads skyboxTime independently of dayTime. Setting
    only dayTime creates a plausible but invalid lighting comparison.
    """
    if not 0 <= daytime <= 0xFFFF:
        raise ValueError(f"daytime must fit an unsigned 16-bit clock: {daytime}")
    if settle < 0:
        raise ValueError(f"settle must be non-negative: {settle}")
    if not 0 <= tolerance <= 0x8000:
        raise ValueError(f"tolerance must be in [0, 0x8000]: {tolerance}")

    harness.send(f"w16 0x{GSAVECONTEXT_DAYTIME_VA:08x} 0x{daytime:04x}")
    harness.send(f"w16 0x{GSAVECONTEXT_SKYBOXTIME_VA:08x} 0x{daytime:04x}")
    if settle:
        harness.send(f"run {settle}")
    day, sky = read_time_of_day(harness)

    def circular_drift(observed: int) -> int:
        return abs(((observed - daytime + 0x8000) & 0xFFFF) - 0x8000)

    drift = max(circular_drift(day), circular_drift(sky))
    if drift > tolerance:
        raise RuntimeError(
            f"set_time_of_day({daytime:#06x}) did NOT hold: "
            f"dayTime={day:#06x} skyboxTime={sky:#06x} "
            f"(drift {drift:#x} > tolerance {tolerance:#x}). Any light-dependent "
            "comparison from this frame would be measuring the clock, not the "
            "renderer — see instrument I001."
        )


def _drive_title_to_gameplay(harness: Harness, rounds: int = 6) -> bool:
    """Cold-boot through title and file select using empirically valid short taps."""
    harness.send("run 300", per_line_timeout=180.0)
    if in_gameplay(harness):
        return True
    for button in (BTN_START, BTN_A):
        for _ in range(rounds):
            for _ in range(12):
                tap(harness, button, hold=4, release=8)
            harness.send("run 60")
            if in_gameplay(harness):
                return True
    print("[harness] never reached gameplay from the title", file=sys.stderr)
    return False


def boot_to_gameplay(
    harness: Harness,
    entrance: int | None = None,
    settle_frames: int = 180,
    *,
    gameplay_state: Path = GAMEPLAY_STATE,
) -> bool:
    """Reach a loaded save, cache the cold boot, and optionally warp.

    The cached state avoids title input entirely. The one-time cold path uses
    short 4/8-frame taps because the former long tap schedule did not advance
    OoT3D's title/file-select flow.
    """
    if gameplay_state.exists():
        harness.send(f"loadstate {gameplay_state}")
        harness.send("run 60")
        if not in_gameplay(harness):
            print(
                f"[harness] {gameplay_state.name} did not land in gameplay — "
                "delete it to force a re-capture",
                file=sys.stderr,
            )
            return False
    else:
        if not _drive_title_to_gameplay(harness):
            return False
        gameplay_state.parent.mkdir(parents=True, exist_ok=True)
        harness.send(f"savestate {gameplay_state}")
        print(
            f"[harness] captured {gameplay_state} — future boots skip the title entirely",
            file=sys.stderr,
        )

    if entrance is None:
        return True

    if settle_frames < 0:
        raise ValueError(f"settle_frames must be non-negative: {settle_frames}")

    response = (harness.send(f"warp 0x{entrance:x}") or "").strip()
    if not response.startswith("ok"):
        print(f"[harness] warp 0x{entrance:x} failed: {response}", file=sys.stderr)
        return False
    remaining = settle_frames
    while remaining:
        chunk = min(remaining, 60)
        harness.send(f"run {chunk}")
        remaining -= chunk
    if not in_gameplay(harness):
        print(f"[harness] warp 0x{entrance:x} left gameplay", file=sys.stderr)
        return False
    return True
