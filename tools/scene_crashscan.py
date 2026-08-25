#!/usr/bin/env python3
"""scene_crashscan.py — load EVERY distinct scene and report which crash on load.

Whole-game load-regression check: for each distinct scene it takes that scene's first
entrance (from entrance_table.h) and cold-boots Zelda3D there via tools/zelda3d_game.py `start`
(no rebuild). A scene that segfaults on load dies before the REPL `.out` appears, so
game.sh returns nonzero and run.log shows a fatal signal. Writes results incrementally to
scratch/crashscan_results.json and prints a summary.

This is the cheap regression net that proved its worth: after #114 (En_Box chest collider
ABI) it confirmed all reachable scenes load, and surfaced the Chamber-of-Sages cutscene-only
false positive (load it with `cswarp <ent> 0xfff0`, not a plain warp — see that REPL cmd).

NOTE: only catches LOAD-time crashes (a few seconds after spawn). Post-load cutscene/anim
crashes and render/behaviour parity are NOT covered. Cutscene-only scenes (Chamber of Sages)
"crash" here because a plain dev-warp picks their non-cutscene setup, which has no player —
that's expected; use cswarp to load them properly.

Usage:
  python3 tools/scene_crashscan.py            # scan all, skip pure test scenes
  python3 tools/scene_crashscan.py --all      # include test scenes too
  python3 tools/scene_crashscan.py --resume   # skip scenes already in results json
"""
import json, os, re, subprocess, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")
RESULTS = os.path.join(REPO, "scratch/crashscan_results.json")
ENTR_TABLE = os.path.join(REPO, "Shipwright/soh/include/tables/entrance_table.h")

# Pure test/debug scenes never reached in normal play — skipped unless --all.
TESTSCENES = {
    "SCENE_UNUSED_6E", "SCENE_SASATEST", "SCENE_SYOTES", "SCENE_SYOTES2", "SCENE_TESTROOM",
    "SCENE_SUTARU", "SCENE_HAIRAL_NIWA2", "SCENE_TEST01", "SCENE_DEPTH_TEST", "SCENE_BESITU",
    "SCENE_CUTSCENE_MAP",
}
# Cutscene-only scenes: a plain start picks their non-cutscene (playerless) setup and "crashes".
# Not a real crash — load with the cswarp REPL cmd. Flagged so the summary doesn't cry wolf.
CUTSCENE_ONLY = {"SCENE_CHAMBER_OF_THE_SAGES"}

CRASH_RE = re.compile(r"Signal:\s*11|SIGSEGV|FATAL signal|Segmentation fault|signal SIG", re.I)


def first_entrance_per_scene():
    seen, order = {}, []
    for line in open(ENTR_TABLE):
        m = re.search(r"/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*DEFINE_ENTRANCE\(\s*(\w+),\s*(\w+),", line)
        if not m:
            continue
        idx, scene = int(m.group(1), 16), m.group(3)
        if scene not in seen:
            seen[scene] = idx
            order.append(scene)
    return [(seen[s], s) for s in order]


def main():
    include_test = "--all" in sys.argv
    resume = "--resume" in sys.argv
    results = json.load(open(RESULTS)) if (resume and os.path.exists(RESULTS)) else {}

    for idx, scene in first_entrance_per_scene():
        test = scene in TESTSCENES
        if test and not include_test:
            continue
        key = f"0x{idx:03X}"
        if resume and key in results:
            continue
        r = subprocess.run(
            [sys.executable, "tools/zelda3d_game.py", "start", str(idx), "0x6000"],
            cwd=REPO,
            env={**os.environ, "ZELDA3D_HEADLESS": "1"},
            capture_output=True,
            text=True,
            timeout=120,
        )
        ready = "ready (pid" in (r.stdout + r.stderr)
        crash = ""
        try:
            tail = open(LOG, errors="ignore").read()[-6000:]
            m = CRASH_RE.search(tail)
            crash = m.group(0) if m else ""
        except Exception:
            pass
        status = "ALIVE" if (ready and not crash) else ("CRASH" if crash else "FAIL")
        results[key] = {"scene": scene, "entrance": idx, "status": status, "crash": crash,
                        "test": test, "cutscene_only": scene in CUTSCENE_ONLY}
        json.dump(results, open(RESULTS, "w"), indent=1)
        tag = " (cutscene-only: use cswarp)" if scene in CUTSCENE_ONLY else (" (test)" if test else "")
        print(f"{key} {scene:42s} {status}{tag} {crash}", flush=True)

    print("=== DONE ===")
    probs = [(k, v) for k, v in results.items()
             if v["status"] != "ALIVE" and not v.get("cutscene_only") and not v.get("test")]
    print(f"scanned={len(results)} alive={sum(v['status']=='ALIVE' for v in results.values())} "
          f"real-problems={len(probs)}")
    for k, v in probs:
        print(f"  PROBLEM {k} {v['scene']} -> {v['status']} {v['crash']}")


if __name__ == "__main__":
    main()
