#!/usr/bin/env python3
"""render_unify_corpus_sweep.py — Phase 0 of the render-unification effort (kanban #131).

Cold-boots every distinct scene (first entrance per scene_crashscan's table, skipping its
test/cutscene-only lists) with SOH3D_CC_DUMP set, so the SoH3D_LogNewCombinerKey instrumentation
(interpreter.cpp) appends every distinct N64 color-combiner permutation actually exercised into
one shared manifest. Also captures a golden screenshot per scene for later old-vs-new diffing by
tools/unified_ab_sweep.py (not built yet).

This does NOT change rendering behavior (SOH3D_CC_DUMP is a pure logging opt-in) — it just walks
content. Output:
  scratch/render_unify/cc_corpus.log          combine_mode/options lines, one append run per scene
                                               (dedupe downstream: sort -u by the two hex fields)
  scratch/render_unify/golden/<scene>.png     one screenshot per scene (frozen camera at spawn)
  scratch/render_unify/sweep_results.json     per-scene status (incremental; --resume)

Usage:
  python3 tools/render_unify_corpus_sweep.py [--resume]
"""
import json, os, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTDIR = os.path.join(REPO, "scratch/render_unify")
GOLDDIR = os.path.join(OUTDIR, "golden")
CC_LOG = os.path.join(OUTDIR, "cc_corpus.log")
RESULTS = os.path.join(OUTDIR, "sweep_results.json")
LOG = os.path.join(REPO, "scratch/logs/run.log")

sys.path.insert(0, os.path.join(REPO, "tools"))
from scene_crashscan import first_entrance_per_scene, TESTSCENES, CUTSCENE_ONLY, CRASH_RE


def repl_shot(name):
    subprocess.run(["tools/soh3d_repl.py", "shot", name], cwd=REPO,
                   capture_output=True, text=True, timeout=30)
    src = os.path.join(REPO, "scratch/screenshots", name + ".png")
    return src if os.path.exists(src) else None


def main():
    resume = "--resume" in sys.argv
    os.makedirs(GOLDDIR, exist_ok=True)
    results = json.load(open(RESULTS)) if (resume and os.path.exists(RESULTS)) else {}

    scenes = [(i, s) for i, s in first_entrance_per_scene()
              if s not in TESTSCENES and s not in CUTSCENE_ONLY]

    for idx, scene in scenes:
        key = f"0x{idx:03X}"
        if resume and key in results:
            continue
        env = dict(os.environ)
        env["SOH3D_HEADLESS"] = "1"
        env["SOH3D_CC_DUMP"] = CC_LOG
        r = subprocess.run(f"tools/soh3d_game.sh start {idx} 0x8000",
                           shell=True, cwd=REPO, env=env, capture_output=True, text=True, timeout=150)
        ready = "ready (pid" in (r.stdout + r.stderr)
        crash = ""
        try:
            mm = CRASH_RE.search(open(LOG, errors="ignore").read()[-6000:])
            crash = mm.group(0) if mm else ""
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

        time.sleep(4)  # let the entrance room's actors finish spawning (headless runs slowly)
        shotname = f"render_unify_{key}_{scene}"
        src = repl_shot(shotname)
        dst = os.path.join(GOLDDIR, f"{key}_{scene}.png")
        if src:
            os.replace(src, dst)
            rec["status"] = "OK"
            rec["golden"] = dst
        else:
            rec["status"] = "NO_SHOT"
        results[key] = rec
        json.dump(results, open(RESULTS, "w"), indent=1)
        print(f"{key} {scene:42s} {rec['status']}", flush=True)

    subprocess.run("tools/soh3d_game.sh stop", shell=True, cwd=REPO, capture_output=True)
    ok = sum(1 for r in results.values() if r.get("status") == "OK")
    print(f"\ndone: {ok}/{len(results)} scenes OK; cc corpus log -> {CC_LOG}")
    if os.path.exists(CC_LOG):
        distinct = len(set(open(CC_LOG).read().splitlines()))
        print(f"distinct combiner-key lines so far: {distinct}")


if __name__ == "__main__":
    main()
