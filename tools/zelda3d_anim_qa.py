#!/usr/bin/env python3
"""Zelda3D player-animation QA sweep.

Systematically drives Link through a set of action-state transitions (idle, walk-stop, roll,
talk, sword draw/sheathe, turning run) via the REAL action machine and records, per RENDERED
frame, the pose-discontinuity metric (`posescan`: the largest per-bone rotation jump vs the
previous frame). The 3d3 named-CSAB path never blends morphs, so a hard cut at a transition
shows as an ISOLATED single-frame spike far above the animation's natural per-frame motion.
This flags those automatically across the whole set -- it reveals "what can go wrong with
different animations" instead of waiting for a playtest report.

The scan is sampled in the DRAW path (where the pose updates). Headless only renders ON DEMAND
(each frame dump triggers one render -> one draw -> one recorded sample), and under `freeze` a
dump returns a CACHED frame with no draw -- so the sweep runs WITHOUT freeze and OVERSAMPLES via
back-to-back dumps (faster than the ~0.22s/logic-frame rate) so every logic frame is captured at
least once; redundant redraws of an unchanged pose record ~0deg and are de-duped in analysis.

An authored fast motion (e.g. the roll somersault) shows SUSTAINED high jumps; a missing-morph
pop shows a lone spike -- the report distinguishes them (spike = max > SPIKE_RATIO x median and
> SPIKE_MIN_DEG) and prints the csab the spike landed on so it's attributable to a transition.
The metric is validated: scrubbing one CSAB frame-by-frame (animframe 0..N) yields a smooth
~3deg/frame series, so spikes in live playback are real transition hard-cuts, not math artifacts.

Requires the game already running (tools/zelda3d_game.py start 238 0x6000) with link 1 / linksrc 3ds.

Usage:  tools/zelda3d_anim_qa.py            # full sweep
        tools/zelda3d_anim_qa.py <name>...  # only the named transitions
"""
import sys, os, time, statistics
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zelda3d_repl as r

SPIKE_RATIO = 3.0
SPIKE_MIN_DEG = 25.0
SPOT = "tpf -66 740 0"
TRASH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scratch", "qa_trash.ppm")

# (name, setup cmds run AFTER reset+posescan-on, number of frame-steps to capture)
TRANSITIONS = [
    ("idle",           [],                               24),
    ("walk_stop",      ["gcam 1", "walkhold 16 0 110"],  28),
    ("run_stop",       ["gcam 1", "walkhold 34 0 127"],  44),
    ("turn_walk_stop", ["gcam 1", "walkhold 20 80 100"], 30),
    ("roll",           ["linkstate roll"],               26),
    ("talk",           ["linkstate talk"],               22),
    ("sword_draw",     ["btnhold 0x4000 3"],             22),
]

def reset():
    r.send("posescan off")
    r.send("freeze 0")         # run at normal speed: headless only renders on demand (each dump), and
    r.send("gcam 0"); r.send("linkpin 0")  # under freeze a dump returns a CACHED frame (no draw, no record).
    r.send("linkstate idle")   # safe reset out of any forced talk/roll (no CLOSING crash)
    r.send(SPOT)
    for _ in range(3):
        r._dump_frame(TRASH)

def parse_dump(text):
    rows = []
    for ln in text.splitlines():
        parts = ln.split(",")
        if len(parts) == 5 and parts[0].isdigit():
            try:
                rows.append((float(parts[1]), int(parts[2]), float(parts[3]), parts[4]))
            except ValueError:
                pass
    return rows

def dedup(rows):
    # Oversampling redraws the SAME logic frame many times (delta ~0). Collapse runs of near-zero
    # frames that sit between real motion so the median/sparkline reflect logic frames, not redraws.
    out = []
    for row in rows:
        if out and row[0] < 0.05 and out[-1][0] < 0.05:
            continue
        out.append(row)
    return out

def run_case(name, setup, ndumps):
    reset()
    r.send("posescan on")
    for c in setup:
        r.send(c)
    # No freeze: each dump forces ONE render (-> draw path -> records this frame's pose-discontinuity).
    # Oversample faster than the ~0.22s/logic-frame rate so every logic frame is captured at least once;
    # redundant redraws of an unchanged pose record ~0 and are de-duped in analysis.
    for _ in range(ndumps):
        r._dump_frame(TRASH)
    r.send("posescan off")
    dump = r.send("posescan dump", timeout=6.0)
    return dedup(parse_dump(dump))

def analyze(name, rows):
    if not rows:
        return f"{name:16s} (no frames recorded)"
    degs = [d for d, _, _, _ in rows]
    mx = max(degs); idx = degs.index(mx); _, bone, frame, csab = rows[idx]
    med = statistics.median(degs)
    spike = mx > SPIKE_MIN_DEG and mx > SPIKE_RATIO * max(med, 0.5)
    flag = "  <== SPIKE (isolated pop)" if spike else ""
    spark = "".join(" .:-=+*#@"[min(8, int(d / 8))] for d in degs)
    return (f"{name:16s} max={mx:6.1f}deg bone={bone:<3d} med={med:5.1f} n={len(rows):<3d}"
            f" csab@max={csab}{flag}\n                 [{spark}]")

def main():
    r.send("link 1"); r.send("linksrc 3ds")
    only = set(sys.argv[1:])
    print("=== Zelda3D player-anim QA sweep (per-DRAWN-frame max bone-rotation jump, deg) ===")
    print("spark: ' '=0 .=8 :=16 -=24 ==32 +=40 *=48 #=56 @>=64 ; SPIKE = lone pop >%gx median\n"
          % SPIKE_RATIO)
    for name, setup, extra in TRANSITIONS:
        if only and name not in only:
            continue
        rows = run_case(name, setup, extra)
        print(analyze(name, rows))
    reset()
    print("\nreset to idle.")

if __name__ == "__main__":
    main()
