#!/usr/bin/env python3
"""title_settle.py — (re)generate scratch/title_settled.state.

`title_ab.py`, `oracle_cache.py`, `title_daytime_scan.py` etc. all treat
`scratch/title_settled.state` as an input they require but none of them
produce it — it was hand-captured in an early session (2026-07-04) and
just carried forward as a gitignored, machine-local artifact ever since.
That's a workflow gap: a fresh checkout / fresh machine has no way to
warm the oracle without this file. This tool closes it.

Procedure: cold-boot the embedded-Azahar harness straight from the ROM
(no savestate — `spawn(None)`), run forward far enough that the title
cutscene's free-running frame counter (Az VA 0x0054CC3C, read via the
harness's `force titletime_read`) is confirmed ACTIVELY INCREMENTING at
its steady-state rate (title cs already running, not still on the
pre-title Nintendo/logo sequence where the counter stays at 0), then
`savestate` that moment to `scratch/title_settled.state`.

The exact cs-frame the resulting savestate lands on is whatever cold-boot
determinism produces — it does NOT need to match any previously-recorded
offset (e.g. the old capture's "88 cs-frames into the loop", see
title_ab.py's ANCHORS comment). Every consumer either free-runs forward
from this file's own t=0 (title_ab.py, oracle_cache warm sweep) or reads
the absolute cs-frame counter directly (`force titletime_read`,
`az_daytime`) rather than assuming a fixed intercept, so a fresh capture
at a different absolute offset is fully interchangeable with the old one.

Usage:
    source .env   # ZELDA3D_OOT3D_ROM
    tools/title_settle.py                  # warm scratch/title_settled.state
    tools/title_settle.py --run 900        # override the cold-boot run count
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import harness_ctl as hc  # noqa: E402

SAVESTATE = REPO / "scratch" / "title_settled.state"

# Cold-boot run budget before the title cs is expected to be live. Measured
# empirically this session (2026-07-14, current harness build): the Az cs
# frame counter (0x0054CC3C) increments from 0 essentially immediately on
# cold boot, at a steady ~0.9 cs-frame per az `run`-frame rate (0, 45, 95,
# 145, ... at run=50,100,150,...) — no multi-hundred-frame dead startup
# period before it goes live (unlike the ~88 cs-frame intercept the OLD,
# now-stale capture happened to land on — see the module docstring: the
# absolute offset is not load-bearing for any consumer). A small warm-up is
# still used, purely to avoid capturing before Azahar's own per-frame init
# has settled, and to leave headroom for the liveness check below.
DEFAULT_RUN = 30


def read_az_csframe(h: hc.Harness) -> int:
    lines = h.send_multiline("force titletime_read")
    for ln in lines:
        m = re.search(r"az=0x0054CC3C:\s*(\d+)", ln)
        if m:
            return int(m.group(1))
    raise RuntimeError(f"force titletime_read: no az= line in {lines!r}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--run", type=int, default=DEFAULT_RUN,
                     help=f"cold-boot frames to run before checking the cs "
                          f"counter is live (default {DEFAULT_RUN})")
    ap.add_argument("--out", type=Path, default=SAVESTATE)
    args = ap.parse_args()

    print(f"[title_settle] cold-booting harness (no savestate)...", file=sys.stderr)
    h = hc.spawn(save_state=None)
    try:
        print(f"[title_settle] run {args.run} (cold-boot warm-up)...", file=sys.stderr)
        c0 = read_az_csframe(h)
        remaining = args.run
        chunk = 100
        while remaining > 0:
            k = min(chunk, remaining)
            h.send(f"run {k}")
            remaining -= k
        c1 = read_az_csframe(h)

        # Confirm the counter is actually incrementing at this point (title
        # cs live, not stuck at 0 on a pre-title logo screen).
        h.send("run 60")
        c2 = read_az_csframe(h)
        print(f"[title_settle] cs-frame counter: t0={c0} -> after warm-up={c1} "
              f"-> +60 frames={c2}", file=sys.stderr)
        if c2 <= c1:
            sys.exit(f"[title_settle] cs-frame counter not incrementing "
                      f"({c1} -> {c2}) after {args.run} warm-up frames — title "
                      f"cs is not live yet; increase --run and retry")

        args.out.parent.mkdir(parents=True, exist_ok=True)
        r = h.send(f"savestate {args.out}")
        if not r.startswith("ok"):
            sys.exit(f"[title_settle] savestate failed: {r}")
        print(f"[title_settle] wrote {args.out} at cs-frame {c2} "
              f"(cold-boot run count: {args.run + 60})", file=sys.stderr)
    finally:
        h.quit()


if __name__ == "__main__":
    main()
