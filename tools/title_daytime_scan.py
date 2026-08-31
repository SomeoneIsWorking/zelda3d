#!/usr/bin/env python3
"""title_daytime_scan.py — empirically RE the OoT3D title-cs dayTime schedule.

THE PROBLEM: SoH's ported Zelda3D_TitleCsTimeOfDay (zelda3d_cutscene.cpp)
anchors dayTime at the last op-0x8c cs cue (only 2 exist in spot99's cs
stream, both at 4:01 AM: frame 0 and frame 301 — see
tools/walk_oot3d_cs.py output) then LINEARLY EXTRAPOLATES at a constant
+6/cs-frame for the rest of the ~2400-frame demo. That rate was only
verified near the anchor (samples up to ~frame 640); this script measures
the REAL oracle rate across the WHOLE demo, AZ-ONLY (no SoH involved —
loadstate only restores Azahar's libretro core, not SoH's separate
in-process state, so SoH numbers from a shared savestate are not
apples-to-apples for schedule RE; see debug_journal note this session).

Method: load the current-contract settled title checkpoint (Az only), read gSaveContext.dayTime
(new `az_daytime` harness command, fixed VA 0x00587958+0xC — a GLOBAL,
valid even during the title/opening GameState) and the free-running cs
frame counter (0x0054CC3C, `force titletime_read`) at increasing `run N`
offsets, spanning the full 2400-frame loop (the checkpoint is
already partway through the demo; we scan forward from wherever it is,
including the wrap back to 0).

Usage:
    source .env
    tools/title_daytime_scan.py [--step 20] [--total 2400]
Prints a CSV-ish table (cs_frame, daytime, delta_daytime, rate_per_frame)
to stdout and a summary of any rate changes / freezes / wraps found.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from harness_process import spawn  # noqa: E402
from harness_paths import TITLE_STATE  # noqa: E402
from harness_transport import Harness  # noqa: E402

SAVESTATE = TITLE_STATE


def read_az_daytime(h: Harness) -> int:
    r = h.send("az_daytime")
    m = re.search(r"daytime=0x([0-9a-fA-F]+)", r)
    if not m:
        raise RuntimeError(f"az_daytime: unexpected reply {r!r}")
    return int(m.group(1), 16)


def read_az_csframe(h: Harness) -> int:
    lines = h.send_multiline("force titletime_read")
    for ln in lines:
        m = re.search(r"az=0x0054CC3C:\s*(\d+)", ln)
        if m:
            return int(m.group(1))
    raise RuntimeError(f"force titletime_read: no az= line in {lines!r}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--step", type=int, default=20, help="frames per sample")
    ap.add_argument("--total", type=int, default=2400, help="total frames to scan")
    ap.add_argument("--savestate", default=str(SAVESTATE))
    args = ap.parse_args()

    h = spawn(save_state=args.savestate)
    rows = []
    cs0 = read_az_csframe(h)
    dt0 = read_az_daytime(h)
    rows.append((cs0, dt0))
    print(f"# start: cs_frame={cs0} daytime=0x{dt0:04x}", file=sys.stderr)

    n = 0
    while n < args.total:
        h.send(f"run {args.step}")
        n += args.step
        cs = read_az_csframe(h)
        dt = read_az_daytime(h)
        rows.append((cs, dt))

    print("cs_frame,daytime_hex,daytime_dec,d_cs,d_dt,rate_per_csframe")
    for i, (cs, dt) in enumerate(rows):
        if i == 0:
            print(f"{cs},0x{dt:04x},{dt},,,")
            continue
        pcs, pdt = rows[i - 1]
        dcs = cs - pcs
        ddt = (dt - pdt) & 0xFFFF
        # unwrap: if ddt looks like it wrapped backward past 0x8000 for a
        # small dcs, treat as a small negative/near-zero delta instead
        if ddt > 0x8000 and dcs > 0:
            ddt_signed = ddt - 0x10000
        else:
            ddt_signed = ddt
        rate = (ddt_signed / dcs) if dcs else float("nan")
        print(f"{cs},0x{dt:04x},{dt},{dcs},{ddt_signed},{rate:.3f}")

    h.quit()


if __name__ == "__main__":
    main()
