#!/usr/bin/env python3
"""title_sbs_verify.py — durable verification tool for the embedded-harness
title-cs SBS sync (TitleSyncController, tools/soh3d_harness/title_sync.h).

Starts the harness, drives it through the default title-sync arm sequence
(HOLD -> integer cursor lock -> LOCKED, see title_sync.h), then samples K
matched instants spread across one full title-cs loop (csFrame wraps
0..2399), writing a labeled side-by-side PNG + content_score for each to
scratch/title_ab/. content_score here is the VERIFICATION metric only —
sync itself is integer-cursor driven (see title_sync.h's history note).
Deliberately includes late-loop instants (csFrame > 1600), the region where
the retired content-search sync drifted worst.

Sampling starts after the controller's lock frame (azLockCs, read live from
the `titlesync` line) because the controller re-HOLDs at each SoH loop wrap
until SoH's cursor climbs back to the oracle's anchor frame — instants
inside [0, azLockCs) are by-design unsynced.

Usage:
    source .env   # ZELDA3D_OOT3D_ROM
    tools/title_sbs_verify.py                  # default K=8 instants
    tools/title_sbs_verify.py --k 12 --name run2

Exit code is 0 iff every sampled score >= --min-score (default 0.7). Prints
a score table (with the controller's live delta/corrections stats) either
way so a caller can inspect low scores.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness_process import spawn  # noqa: E402
from harness_transport import Harness  # noqa: E402
from title_ab import content_score, ppm_to_png, compose_sxs, OUTDIR  # noqa: E402

# SoH's title-cs cursor advances 0.5 cs-frame per engine frame (RE'd rate
# law, debug_journal/2026-07-09-title-cs-phase-sync.md) -> engine frames
# per cs frame:
ENGINE_FRAMES_PER_CS = 2


def step(h: Harness, n: int, timeout: float = 900.0) -> str:
    h.proc.stdin.write((f"step {n}\n").encode())
    h.proc.stdin.flush()
    line = h._readline(timeout=timeout)
    if line is None:
        raise TimeoutError(f"step {n}: no response within {timeout}s")
    return line.rstrip()


def titlesync_state(h: Harness) -> str:
    return h.send("titlesync")


def parse_field(state_line: str, key: str) -> str:
    return state_line.split(f"{key}=")[1].split()[0]


def snapshot_and_score(h: Harness, name: str) -> float:
    base = OUTDIR / name
    h.send_multiline(f"snapshot {base}")
    az = ppm_to_png(str(base) + ".az.ppm")
    soh = ppm_to_png(str(base) + ".soh.ppm")
    score = content_score(str(az), str(soh))
    compose_sxs(az, soh, OUTDIR / f"{name}_sxs.png")
    return score


def drive_to_locked(h: Harness, boot_step: int = 20, frame_budget: int = 3000) -> str:
    """Advance until TitleSyncController reaches LOCKED (auto-arms on the
    first `step`). Raises if it never locks within frame_budget SoH frames."""
    while True:
        step(h, boot_step)
        t = titlesync_state(h)
        if parse_field(t, "state") == "LOCKED":
            return t
        if int(parse_field(t, "sohFrame")) > frame_budget:
            raise RuntimeError(f"title-sync never reached LOCKED: {t}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--k", type=int, default=8, help="number of sampled instants (default 8)")
    ap.add_argument("--name", default="verify", help="output filename prefix (default 'verify')")
    ap.add_argument("--min-score", type=float, default=0.7,
                     help="score below which an instant is flagged (default 0.7)")
    ap.add_argument("--loop-len", type=int, default=2400,
                     help="title-cs loop length in csFrame units (default 2400)")
    ap.add_argument("--cs-first", type=int, default=150,
                     help="first sampled csFrame (raised to azLockCs+50 "
                          "automatically; below azLockCs is the by-design "
                          "re-HOLD window at each wrap)")
    ap.add_argument("--cs-last", type=int, default=2350,
                     help="last sampled csFrame (default 2350)")
    args = ap.parse_args()

    OUTDIR.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR", f"scratch/logs/title_sbs_verify_{args.name}.log")

    h = spawn(save_state=None)
    results = []  # (target, actual_cs, score, delta, corrections, state)
    try:
        t = drive_to_locked(h)
        print(f"[title_sbs_verify] locked: {t}", file=sys.stderr)

        az_lock_cs = int(parse_field(t, "azLockCs"))
        cs_first = max(args.cs_first, az_lock_cs + 50)
        span = args.cs_last - cs_first
        targets = [cs_first + round(i * span / max(1, args.k - 1)) for i in range(args.k)]

        cur_cs = int(parse_field(t, "csFrame"))
        for i, target in enumerate(targets):
            delta_cs = target - cur_cs
            if delta_cs < 0:
                delta_cs += args.loop_len  # wrap forward to the next occurrence
            if delta_cs > 0:
                step(h, delta_cs * ENGINE_FRAMES_PER_CS)
            t = titlesync_state(h)
            cur_cs = int(parse_field(t, "csFrame"))
            state = parse_field(t, "state")
            gdelta = int(parse_field(t, "delta"))
            corrections = int(parse_field(t, "corrections"))
            score = snapshot_and_score(h, f"{args.name}_{i:02d}_cs{target}")
            results.append((target, cur_cs, score, gdelta, corrections, state))
            print(f"[title_sbs_verify] point {i}: target_cs={target} actual_cs={cur_cs} "
                  f"score={score:.4f}  {t}", file=sys.stderr)
    finally:
        h.close()

    print("\n=== title_sbs_verify score table ===")
    print(f"{'idx':>3}  {'target_cs':>9}  {'actual_cs':>9}  {'score':>7}  "
          f"{'delta':>5}  {'corr':>5}  state    flag")
    ok = True
    for i, (target, actual, score, gdelta, corr, state) in enumerate(results):
        flag = "" if score >= args.min_score else "LOW"
        if score < args.min_score:
            ok = False
        print(f"{i:>3}  {target:>9}  {actual:>9}  {score:>7.4f}  "
              f"{gdelta:>5}  {corr:>5}  {state:<7}  {flag}")

    print(f"\nmin_score threshold: {args.min_score}")
    print("PASS" if ok else "FAIL (see LOW rows and their *_sxs.png)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
