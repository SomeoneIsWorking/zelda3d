#!/usr/bin/env python3
"""Capture a real PICA fragment-lighting configuration at the TITLE screen.

Why here, and not through the committed probe: `tools/cmb_fragment_lighting_oracle_probe.py` starts
from `GAMEPLAY_STATE`, which does not exist (the cold title route cannot reseed it --
`docs/issues/0023`). Every fragment-lighting capture in the record therefore comes from a fixture the
probe itself labels a **PICA-disabled negative control**. The title demo world, by contrast, is lit,
and it needs no save. So the two pieces the probe composes are driven directly here:

    vsuni_log <path>        per-draw discovery: finds a draw whose authoritative
                            `regs.lighting.disable` says fragment lighting is ON
    lighting_capture <n> <path>
                            the PICA state for that exact draw -- raw config0/config1,
                            the light-slot map, all eight light records, and the activated LUTs

The point is a ground-truth triple for one real material: the object's bytes (from
`tools/lit_object_dump.py`), the registers the 3DS actually programmed, and what
`oot3d-decomp/tools/pica_lighting_config.py` predicts from those bytes. Agreement on a LIT material
is what the builder has never been checked against.

Selection is on the authoritative register, not on an independent boolean, and every rejection is
reported -- a discovery that finds no lit draw is a finding, not a reason to report nothing.

Usage:
    source .env
    tools/lit_pica_capture.py
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from harness_process import spawn  # noqa: E402

OUT = REPO / "scratch" / "lit_pica"
# Protocol gotchas from tools/soh3d_harness/AZAHAR_PATCH.md, both of which cost a debug cycle:
# a single retro_run after loadstate renders a CORRUPT frame, and OoT3D draws one 3D frame per TWO
# retro_run calls while the captured framebuffer trails the emulated GPU by ~2 frames. The probe uses
# warm=2, probe=4; the same shape applies here.
WARM_RUNS = 2
PROBE_RUNS = 4
# OoT3D runs its logic at 30 fps on a 60 Hz libretro cadence, so a title-logic frame is 2 retro_runs.
RETRO_RUNS_PER_LOGIC_FRAME = 2


def parse_draws(log_text: str) -> list[dict[str, str]]:
    """Parse the per-draw vsuni_log lines into records, keyed by draw id.

    The line format is `draw n=<id> idx=... hasCol=... vLit=... fLit=... picaLit=... cmdList=...`.
    The id is `n=`, not `draw=` -- matching the wrong token silently yields zero draws and reads
    as "the title has no lit materials" when the log is in fact full of them.
    """
    draws: dict[str, dict[str, str]] = {}
    for line in log_text.splitlines():
        if not line.startswith("draw "):
            continue
        match = re.search(r"\bn=(\d+)", line)
        if not match:
            continue
        record = {"id": match.group(1), "line": line.strip()}
        for key in ("picaLit", "fLit", "vLit", "hasCol", "tex0", "texEn"):
            found = re.search(rf"\b{key}=(\S+)", line)
            if found:
                record[key] = found.group(1)
        draws[record["id"]] = record
    return [draws[k] for k in sorted(draws, key=int)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--title-frames", type=int, default=200, help="title logic frames to run first")
    parser.add_argument("--max-draw", type=int, default=400, help="draw ids to try, highest first")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR", str(REPO / "scratch" / "logs" / "lit_pica_capture.log"))
    harness = spawn()
    try:
        boot = harness.send("soh_boot")
        if not boot.startswith("ok"):
            raise SystemExit(f"soh_boot failed: {boot}")
        print("[lit_pica] " + (harness.send("playstate") or "").strip())

        # Warm up past the corrupt first frame, then advance in whole logic frames.
        for _ in range(WARM_RUNS):
            harness.send("run 1", per_line_timeout=240.0)
        logic = args.title_frames
        for done in range(0, logic, 50):
            harness.send(f"run {50 * RETRO_RUNS_PER_LOGIC_FRAME}", per_line_timeout=300.0)

        log_path = OUT / "vsuni_title.log"
        log_path.unlink(missing_ok=True)
        reply = harness.send(f"vsuni_log {log_path}")
        print(f"[lit_pica] vsuni_log: {reply.strip()}")
        for _ in range(PROBE_RUNS):
            harness.send("run 1", per_line_timeout=240.0)
        harness.send("vsuni_log off")

        if not log_path.exists():
            print("[lit_pica] no vsuni_log file was written -- discovery failed", file=sys.stderr)
            return 1
        draws = parse_draws(log_path.read_text(errors="replace"))
        lit = [d for d in draws if d.get("picaLit") == "1"]
        vertex_lit = [d for d in draws if d.get("vLit") == "1"]
        print(
            f"[lit_pica] draws discovered: {len(draws)} | picaLit=1: {len(lit)} | "
            f"vLit=1: {len(vertex_lit)} | hasCol=0: {sum(1 for d in draws if d.get('hasCol') == '0')}"
        )
        for record in lit[:8]:
            print(f"    draw n={record['id']:>4} picaLit={record.get('picaLit')} tex0={record.get('tex0','?')[:44]}")
        if not lit:
            # A negative with a denominator, and the denominator is the finding: this scene renders
            # entirely with vertex lighting, so it cannot supply the lit-material ground truth that
            # FRAG_PRIMARY needs. That is a reason to go to gameplay, not a reason to report nothing.
            print(
                f"[lit_pica] NO draw has PICA fragment lighting enabled ({len(draws)} draws, "
                f"{len(vertex_lit)} vertex-lit). This scene cannot provide a lit-material fixture; "
                f"the ground truth for the +0x18A / per-slot-enable question needs a gameplay scene.",
                file=sys.stderr,
            )
            return 2

        for record in lit[:3]:
            draw_id = record["id"]
            path = OUT / f"lighting_draw{draw_id}.json"
            path.unlink(missing_ok=True)
            reply = harness.send(f"lighting_capture {draw_id} {path}")
            for _ in range(PROBE_RUNS):
                harness.send("run 1", per_line_timeout=240.0)
            harness.send("lighting_capture off")
            if not path.exists():
                print(f"[lit_pica] draw {draw_id}: no capture written ({reply.strip()})")
                continue
            payload = json.loads(path.read_text())
            summary = {
                k: v for k, v in payload.items()
                if isinstance(v, (int, str)) and ("config" in k or "light" in k or "slot" in k or "max" in k)
            }
            print(f"[lit_pica] draw {draw_id}: " + " ".join(f"{k}={v}" for k, v in sorted(summary.items())[:10]))
            print(f"    -> {path}")
    finally:
        harness.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
