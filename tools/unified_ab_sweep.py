#!/usr/bin/env python3
"""unified_ab_sweep.py — the render-unification differential gate (kanban #131).

A self-referential old-vs-new pixel differ (NOT an oracle compare — the Azahar 3DS oracle can't
judge N64 color-combiner correctness, only the game's own two renderer paths can be compared to
each other). Two modes:

  --mode selfcheck (default)
    Diffs a FRESH screenshot of each scene against the Phase 0 golden captured by
    render_unify_corpus_sweep.py (scratch/render_unify/golden/). Validates two things before any
    real toggle exists: (1) the harness/diff mechanism itself is correct, (2) the game is
    deterministic enough at a frozen entrance spawn for pixel-diffing to be meaningful at all —
    if selfcheck isn't clean, no later old-vs-new comparison can be trusted either.

  --mode ab --env-b VAR=VAL[,VAR2=VAL2]
    Real differential gate for later phases: launches each scene twice (env A = baseline, env B =
    baseline + the given overrides, e.g. ZELDA3D_UNIFIED=1 once Phase 2 lands that toggle) and
    diffs the two fresh screenshots against each other.

Pass bar (per the render-unification plan): mean per-pixel channel delta and the fraction of
pixels exceeding LSB_TOL are reported; a scene is a HARD FAIL if MORE than (1 - PASS_FRACTION) of
pixels exceed LSB_TOL. Diff images for any failing scene are written for inspection — nothing is
silently skipped.

Output: scratch/render_unify/ab_results.json (incremental; --resume), diff images under
scratch/render_unify/diffs/<scene>_diff.png for any scene over tolerance.

Usage:
  python3 tools/unified_ab_sweep.py [--mode selfcheck|ab] [--env-b VAR=VAL,...] [--resume]
  python3 tools/unified_ab_sweep.py --scenes SCENE_KOKIRI_FOREST,SCENE_MARKET   # subset for smoke
"""
import json, os, subprocess, sys
from PIL import Image
import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTDIR = os.path.join(REPO, "scratch/render_unify")
GOLDDIR = os.path.join(OUTDIR, "golden")
DIFFDIR = os.path.join(OUTDIR, "diffs")
RESULTS = os.path.join(OUTDIR, "ab_results.json")
LOG = os.path.join(REPO, "scratch/logs/run.log")

sys.path.insert(0, os.path.join(REPO, "tools"))
from scene_crashscan import first_entrance_per_scene, TESTSCENES, CUTSCENE_ONLY, CRASH_RE

LSB_TOL = 1          # per-channel 0-255 delta treated as float/rounding noise, not a regression
PASS_FRACTION = 0.995  # >=99.5% of pixels must be within LSB_TOL for a scene to pass
FREEZE_NOISE_FRAME = "0"  # must match render_unify_corpus_sweep.py's value; pins the frame_count-
                          # seeded alpha-dither noise so A and B captures share the same dither
                          # pattern and only real rendering differences show up in the diff.
FREEZE_STEP_TICKS = 400   # must match render_unify_corpus_sweep.py's value; see boot_and_shoot
FREEZE_RAND_SEED = "1"    # must match render_unify_corpus_sweep.py's value; pins Rand_Seed(osGetTime())


def parse_env_overrides(s):
    out = {}
    if not s:
        return out
    for pair in s.split(","):
        if "=" not in pair:
            continue
        k, v = pair.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def boot_and_shoot(idx, name, env_overrides):
    env = dict(os.environ)
    env["ZELDA3D_HEADLESS"] = "1"
    env["ZELDA3D_FREEZE_NOISE_FRAME"] = FREEZE_NOISE_FRAME
    env["ZELDA3D_FREEZE_INTERP"] = "1"
    env["ZELDA3D_FREEZE_RAND_SEED"] = FREEZE_RAND_SEED
    env.update(env_overrides)
    r = subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "start", str(idx), "0x8000"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=150)
    ready = "ready (pid" in (r.stdout + r.stderr)
    crash = ""
    try:
        mm = CRASH_RE.search(open(LOG, errors="ignore").read()[-6000:])
        crash = mm.group(0) if mm else ""
    except Exception:
        pass
    if not ready or crash:
        return None, ("CRASH" if crash else "FAIL"), crash
    # Freeze+step to a FIXED tick count (not a wall-clock sleep) so this capture lands on the exact
    # same logic state as the golden/other-side capture — see render_unify_corpus_sweep.py's
    # repl_shot for the full rationale (sim-timing jitter otherwise swamps the diff).
    subprocess.run(["tools/zelda3d_repl.py", "cmd", "freeze 1"], cwd=REPO,
                   capture_output=True, text=True, timeout=15)
    remaining = FREEZE_STEP_TICKS
    while remaining > 0:
        n = min(600, remaining)
        subprocess.run(["tools/zelda3d_repl.py", "cmd", f"step {n}"], cwd=REPO,
                       capture_output=True, text=True, timeout=30)
        remaining -= n
    subprocess.run(["tools/zelda3d_repl.py", "shot", name], cwd=REPO,
                   capture_output=True, text=True, timeout=30)
    src = os.path.join(REPO, "scratch/screenshots", name + ".png")
    return (src if os.path.exists(src) else None), "OK", ""


def diff_images(path_a, path_b):
    a = np.asarray(Image.open(path_a).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(path_b).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        return None, {"shape_mismatch": [list(a.shape), list(b.shape)]}
    delta = np.abs(a - b)
    max_channel_delta = delta.max(axis=2)  # per-pixel worst channel
    over = max_channel_delta > LSB_TOL
    frac_over = float(over.mean())
    stats = {
        "mean_delta": float(max_channel_delta.mean()),
        "max_delta": int(max_channel_delta.max()),
        "frac_pixels_over_tol": frac_over,
        "pass": frac_over <= (1.0 - PASS_FRACTION),
    }
    diff_vis = None
    if not stats["pass"]:
        amplified = np.clip(max_channel_delta.astype(np.float32) * 4, 0, 255).astype(np.uint8)
        diff_vis = Image.fromarray(amplified, mode="L")
    return diff_vis, stats


def main():
    argv = sys.argv[1:]
    mode = "selfcheck"
    env_b_str = ""
    resume = "--resume" in argv
    scene_filter = None
    for i, a in enumerate(argv):
        if a == "--mode" and i + 1 < len(argv):
            mode = argv[i + 1]
        if a == "--env-b" and i + 1 < len(argv):
            env_b_str = argv[i + 1]
        if a == "--scenes" and i + 1 < len(argv):
            scene_filter = set(argv[i + 1].split(","))

    os.makedirs(DIFFDIR, exist_ok=True)
    results = json.load(open(RESULTS)) if (resume and os.path.exists(RESULTS)) else {}

    scenes = [(i, s) for i, s in first_entrance_per_scene()
              if s not in TESTSCENES and s not in CUTSCENE_ONLY]
    if scene_filter:
        scenes = [(i, s) for i, s in scenes if s in scene_filter]

    env_b = parse_env_overrides(env_b_str)
    fail_count = 0

    for idx, scene in scenes:
        key = f"0x{idx:03X}"
        if resume and key in results:
            continue

        if mode == "selfcheck":
            golden = os.path.join(GOLDDIR, f"{key}_{scene}.png")
            if not os.path.exists(golden):
                results[key] = {"scene": scene, "status": "NO_GOLDEN"}
                print(f"{key} {scene:42s} NO_GOLDEN (run render_unify_corpus_sweep.py first)", flush=True)
                continue
            path_b, status, crash = boot_and_shoot(idx, f"ab_selfcheck_{key}_{scene}", {})
            path_a = golden
        else:  # ab
            path_a, status_a, crash_a = boot_and_shoot(idx, f"ab_A_{key}_{scene}", {})
            if status_a != "OK":
                results[key] = {"scene": scene, "status": status_a, "crash": crash_a, "side": "A"}
                json.dump(results, open(RESULTS, "w"), indent=1)
                print(f"{key} {scene:42s} {status_a} (side A) {crash_a}", flush=True)
                continue
            path_b, status, crash = boot_and_shoot(idx, f"ab_B_{key}_{scene}", env_b)

        if status != "OK" or not path_b or not path_a:
            results[key] = {"scene": scene, "status": status, "crash": crash}
            json.dump(results, open(RESULTS, "w"), indent=1)
            print(f"{key} {scene:42s} {status} {crash}", flush=True)
            continue

        diff_vis, stats = diff_images(path_a, path_b)
        rec = {"scene": scene, "status": "OK", **stats}
        if not stats.get("pass", False):
            fail_count += 1
            diffpath = os.path.join(DIFFDIR, f"{key}_{scene}_diff.png")
            if diff_vis:
                diff_vis.save(diffpath)
                rec["diff_image"] = diffpath
        results[key] = rec
        json.dump(results, open(RESULTS, "w"), indent=1)
        tag = "PASS" if stats.get("pass") else "FAIL"
        print(f"{key} {scene:42s} {tag} mean={stats['mean_delta']:.3f} "
              f"max={stats['max_delta']} frac_over={stats['frac_pixels_over_tol']:.4f}", flush=True)

    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "stop"],
        cwd=REPO,
        capture_output=True,
    )
    n = len(results)
    passed = sum(1 for r in results.values() if r.get("status") == "OK" and r.get("pass"))
    print(f"\ndone: {passed}/{n} scenes within tolerance ({mode} mode). Results -> {RESULTS}")
    if fail_count:
        print(f"{fail_count} scene(s) over tolerance — diff images in {DIFFDIR}")


if __name__ == "__main__":
    main()
