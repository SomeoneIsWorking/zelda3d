#!/usr/bin/env python3
"""coverage_scene_sweep.py — whole-game CHARACTER / OBJECT / GEOMETRY render-parity audit.

For EVERY real scene (first entrance from entrance_table.h, reusing scene_crashscan's
skip-lists) it cold-boots SoH3D headless, enumerates the loaded actors via the REPL
`actorsnear` coverage audit, and screenshots the scene. Per actor `actorsnear` reports:
  TABLE / AUTO:<zar>[ (skin)]  -> renders as the OoT3D model (parity OK)
  HANA-*/ISHI-* (3DS)          -> OoT3D prop
  --N64--                      -> NO object->ZAR mapping = still renders as N64 (PARITY GAP)

It answers "across all levels, what still renders as N64 (characters/objects) and does the
world geometry render?" — the deliverable is:
  - per-scene: loaded-actor coverage counts + the list of N64-rendering actor ids
  - a screenshot per scene (scratch/screenshots/coverage/<key>.png) for geometry review
  - aggregate: the GLOBAL set of actor ids that render as N64 anywhere (the coverage gap)

NOTE: SoH3D loads only the current room's actors at an entrance, so this samples each scene's
entrance room (multi-room scenes need roomwarp for full coverage — a follow-up). The N64-actor
list still aggregates the gaps seen across all entrance rooms. Geometry/visual correctness needs
the PNGs eyeballed; this tool flags only the quantitative signals (empty/black frame).

Results: scratch/coverage_results.json (incremental; --resume). Usage:
  python3 tools/coverage_scene_sweep.py [--resume]
"""
import json, os, re, subprocess, sys, shutil, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")
RESULTS = os.path.join(REPO, "scratch/coverage_results.json")
SHOTDIR = os.path.join(REPO, "scratch/screenshots")
OUTDIR = os.path.join(REPO, "scratch/screenshots/coverage")

sys.path.insert(0, os.path.join(REPO, "tools"))
from scene_crashscan import first_entrance_per_scene, TESTSCENES, CUTSCENE_ONLY, CRASH_RE
from PIL import Image


def repl(cmd, timeout=20):
    r = subprocess.run(["tools/soh3d_repl.py", "cmd", cmd], cwd=REPO,
                       capture_output=True, text=True, timeout=timeout)
    return r.stdout + r.stderr


def shot(name):
    subprocess.run(["tools/soh3d_repl.py", "shot", name], cwd=REPO,
                   capture_output=True, text=True, timeout=30)
    src = os.path.join(SHOTDIR, name + ".png")
    if not os.path.exists(src):
        return None
    dst = os.path.join(OUTDIR, name + ".png")
    shutil.move(src, dst)
    return dst


ACTOR_RE = re.compile(r"id=0x([0-9A-Fa-f]+)\s+p=0x([0-9A-Fa-f]+)\s+cat=(\d+)\s+d=\s*\d+\s+(.*)")


def parse_actors(text):
    """-> list of (id, params, cat, coverage_str)."""
    out = []
    for line in text.splitlines():
        m = ACTOR_RE.search(line)
        if m:
            out.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)), m.group(4).strip()))
    return out


def classify(cov):
    if cov.startswith("--N64--"):
        return "n64"
    if "(skin)" in cov:
        return "auto_skin"
    if cov.startswith("AUTO:"):
        return "auto"
    if cov == "TABLE":
        return "table"
    return "prop3ds"  # HANA/ISHI etc.


def frame_lum(path):
    im = Image.open(path).convert("RGB")
    W, H = im.size
    c = im.crop((int(W * 0.10), int(H * 0.10), int(W * 0.90), int(H * 0.90)))
    px = list(c.getdata())
    n = len(px)
    return round(sum(0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] for p in px) / n, 1)


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
        r = subprocess.run(f"SOH3D_HEADLESS=1 tools/soh3d_game.sh start {idx} 0x8000",
                           shell=True, cwd=REPO, capture_output=True, text=True, timeout=150)
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
        txt = repl("actorsnear 30000")
        actors = parse_actors(txt)
        counts = {"table": 0, "auto": 0, "auto_skin": 0, "prop3ds": 0, "n64": 0}
        n64_ids = {}
        for aid, params, cat, cov in actors:
            if cat == 2:  # ACTORCAT_PLAYER: drawn via the dedicated SoH3D_TryDrawPlayer path, not N64
                continue
            k = classify(cov)
            counts[k] += 1
            if k == "n64":
                n64_ids[f"0x{aid:X}"] = cat
        p = shot(key)
        lum = frame_lum(p) if p else None

        rec.update(status="OK", n_actors=len(actors), counts=counts,
                   n64_ids=n64_ids, frame_lum=lum)
        results[key] = rec
        json.dump(results, open(RESULTS, "w"), indent=1)
        print(f"{key} {scene:42s} actors={len(actors):2d} "
              f"3DS={counts['table']+counts['auto']+counts['auto_skin']+counts['prop3ds']:2d} "
              f"N64={counts['n64']:2d} lum={lum} "
              f"{'N64:'+','.join(n64_ids) if n64_ids else ''}", flush=True)

    print("=== DONE ===")
    ok = [v for v in results.values() if v.get("status") == "OK"]
    crashed = [(k, v) for k, v in results.items() if v.get("status") in ("CRASH", "FAIL")]
    # Global N64-rendering actor set across all scenes.
    glob = {}
    for v in ok:
        for aid, cat in (v.get("n64_ids") or {}).items():
            glob.setdefault(aid, {"cat": cat, "scenes": []})
            glob[aid]["scenes"].append(v["scene"])
    print(f"scanned={len(results)} ok={len(ok)} crash/fail={len(crashed)} "
          f"global-N64-actor-ids={len(glob)}")
    for k, v in crashed:
        print(f"  CRASH/FAIL {k} {v['scene']} {v.get('crash','')}")
    print("GLOBAL N64-RENDERING ACTOR IDS (no OoT3D model -> parity gap):")
    for aid, info in sorted(glob.items(), key=lambda kv: -len(kv[1]["scenes"])):
        print(f"  {aid} cat={info['cat']} in {len(info['scenes'])} scene(s): "
              f"{', '.join(info['scenes'][:6])}{' ...' if len(info['scenes'])>6 else ''}")


if __name__ == "__main__":
    main()
