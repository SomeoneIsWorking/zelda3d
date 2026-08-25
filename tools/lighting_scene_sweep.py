#!/usr/bin/env python3
"""lighting_scene_sweep.py — whole-game #111 world-lighting parity sweep.

For EVERY real scene (first entrance from entrance_table.h, reusing scene_crashscan's
skip-lists) it cold-boots Zelda3D headless and captures three frames with quantitative
floor-region measurements:
  - OFF-day  (worldshade 0, time 0x8000)  -> regression baseline
  - ON-day   (worldshade 1, time 0x8000)  -> must not regress vs OFF-day
  - ON-night (worldshade 1, time 0x0000)  -> must darken+cool vs ON-day (outdoor)

It is the cheap exhaustive net for #111: it does NOT do a per-scene oracle A/B (too slow
for ~100 scenes), but it catches the failure modes that matter — a scene that crashes,
renders BLACK/BLOWN-OUT, or where worldshade INVERTS day/night or REGRESSES the day frame.
Interiors have neutral white-light palettes so worldshade ~no-ops there (day~night, on~off);
those are reported but not flagged. Review the flagged scenes' PNGs.

Shots: scratch/screenshots/lightsweep/<key>_<offday|onday|onnight>.png
Results: scratch/lightsweep_results.json  (incremental; --resume to continue)

Usage: ZELDA3D_OOT3D_ROM unused here; just `python3 tools/lighting_scene_sweep.py [--resume]`
"""
import json, os, re, subprocess, sys, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")
RESULTS = os.path.join(REPO, "scratch/lightsweep_results.json")
SHOTDIR = os.path.join(REPO, "scratch/screenshots")
OUTDIR = os.path.join(REPO, "scratch/screenshots/lightsweep")
ENTR_TABLE = os.path.join(REPO, "Shipwright/soh/include/tables/entrance_table.h")

# Reuse scene_crashscan's classifications.
sys.path.insert(0, os.path.join(REPO, "tools"))
from scene_crashscan import first_entrance_per_scene, TESTSCENES, CUTSCENE_ONLY, CRASH_RE

from PIL import Image


def repl(cmd):
    subprocess.run(["tools/zelda3d_repl.py", "cmd", cmd], cwd=REPO,
                   capture_output=True, text=True, timeout=20)


def shot(name):
    """Take a shot (flat name) and move it into OUTDIR; return its final path or None."""
    r = subprocess.run(["tools/zelda3d_repl.py", "shot", name], cwd=REPO,
                       capture_output=True, text=True, timeout=30)
    src = os.path.join(SHOTDIR, name + ".png")
    if not os.path.exists(src):
        return None
    dst = os.path.join(OUTDIR, name + ".png")
    shutil.move(src, dst)
    return dst


def measure(path):
    """Mean RGB of a central-lower (floor) region + whole-frame lum + black/blown fractions."""
    im = Image.open(path).convert("RGB")
    W, H = im.size
    c = im.crop((int(W * 0.30), int(H * 0.55), int(W * 0.70), int(H * 0.88)))
    px = list(c.getdata())
    n = len(px)
    r = sum(p[0] for p in px) / n
    g = sum(p[1] for p in px) / n
    b = sum(p[2] for p in px) / n
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    blk = sum(1 for p in px if max(p) < 12) / n
    blown = sum(1 for p in px if min(p) > 248) / n
    return dict(r=round(r, 1), g=round(g, 1), b=round(b, 1), lum=round(lum, 1),
                blk=round(blk, 3), blown=round(blown, 3))


def main():
    resume = "--resume" in sys.argv
    os.makedirs(OUTDIR, exist_ok=True)
    results = json.load(open(RESULTS)) if (resume and os.path.exists(RESULTS)) else {}

    scenes = [(i, s) for i, s in first_entrance_per_scene()
              if s not in TESTSCENES and s not in CUTSCENE_ONLY]
    for idx, scene in scenes:
        key = f"0x{idx:03X}"
        if resume and key in results:
            continue
        r = subprocess.run(
            [sys.executable, "tools/zelda3d_game.py", "start", str(idx), "0x8000"],
            cwd=REPO,
            env={**os.environ, "ZELDA3D_HEADLESS": "1"},
            capture_output=True, text=True, timeout=150)
        ready = "ready (pid" in (r.stdout + r.stderr)
        crash = ""
        try:
            crash = (CRASH_RE.search(open(LOG, errors="ignore").read()[-6000:]) or [None])
            crash = crash.group(0) if hasattr(crash, "group") else ""
        except Exception:
            pass
        rec = {"scene": scene, "entrance": idx}
        if not ready or crash:
            rec["status"] = "CRASH" if crash else "FAIL"
            rec["crash"] = crash
            results[key] = rec
            json.dump(results, open(RESULTS, "w"), indent=1)
            print(f"{key} {scene:42s} {rec['status']} {crash}", flush=True)
            continue

        repl("worldshade 0")
        offday = shot(f"{key}_offday")
        repl("worldshade 1")
        onday = shot(f"{key}_onday")
        repl("time 0x0000")
        onnight = shot(f"{key}_onnight")

        m = {}
        for tag, p in (("offday", offday), ("onday", onday), ("onnight", onnight)):
            m[tag] = measure(p) if p else None

        flags = []
        if not all(m.values()):
            flags.append("shot-missing")
        else:
            od, on, nt = m["offday"], m["onday"], m["onnight"]
            if on["blk"] > 0.9:
                flags.append("on-day-black")
            if on["blown"] > 0.5:
                flags.append("on-day-blown")
            # regression: ON-day much darker/brighter than OFF-day (worldshade should ~preserve day)
            if od["lum"] > 6 and (on["lum"] < 0.6 * od["lum"] or on["lum"] > 1.5 * od["lum"]):
                flags.append(f"day-shift {od['lum']}->{on['lum']}")
            # inversion: night BRIGHTER than day (worldshade should darken night)
            if nt["lum"] > on["lum"] * 1.1 and on["lum"] > 6:
                flags.append(f"night-brighter {on['lum']}->{nt['lum']}")

        rec.update(status="OK", **m, flags=flags)
        results[key] = rec
        json.dump(results, open(RESULTS, "w"), indent=1)
        ftag = (" FLAG:" + ",".join(flags)) if flags else ""
        print(f"{key} {scene:42s} OK  offday_lum={m['offday']['lum'] if m['offday'] else '?'} "
              f"onday_lum={m['onday']['lum'] if m['onday'] else '?'} "
              f"onnight_lum={m['onnight']['lum'] if m['onnight'] else '?'}{ftag}", flush=True)

    print("=== DONE ===")
    flagged = [(k, v) for k, v in results.items() if v.get("flags")]
    crashed = [(k, v) for k, v in results.items() if v.get("status") in ("CRASH", "FAIL")]
    print(f"scanned={len(results)} ok={sum(v.get('status')=='OK' for v in results.values())} "
          f"crash/fail={len(crashed)} flagged={len(flagged)}")
    for k, v in crashed:
        print(f"  CRASH/FAIL {k} {v['scene']} {v.get('crash','')}")
    for k, v in flagged:
        print(f"  FLAG {k} {v['scene']} -> {v['flags']}")


if __name__ == "__main__":
    main()
