#!/usr/bin/env python3
"""batch_orbit_capture.py — batch multi-angle orbit sweep for the OoT3D oracle pipeline.

For every scene in batch_capture.SCENE_LIST this tool:
  1. Warps the oracle (link_ctl warp + wait_for_scene, exactly as batch_capture.py does).
  2. Dismisses any textbox (oracle_textbox.wait_no_textbox).
  3. Settles, then runs an orbit_sweep around Link's position:
       n angles evenly spaced around 360° at a fixed pitch and distance.
  4. Writes screenshots to scratch/oracle_batch/<scene>/orbit_NNN_YYYyaw.png.
  5. Updates scratch/oracle_batch/<scene>/orbit_meta.json with sweep params + shot list.
  6. Writes a top-level scratch/oracle_batch/orbit_summary.json.

The actors.json + scene.png from batch_capture.py are NOT re-captured; this tool adds only
the multi-angle visual coverage layer on top of an existing (or new) capture run.

## Usage
  # From soh3d repo root, while holding the oracle lock:
  flock scratch/.oraclelock python3 tools/batch_orbit_capture.py
  flock scratch/.oraclelock python3 tools/batch_orbit_capture.py --scenes kokiri_forest links_house
  flock scratch/.oraclelock python3 tools/batch_orbit_capture.py --force    # re-sweep all
  python3 tools/batch_orbit_capture.py --status                             # no oracle needed

## Orbit parameters (tune with CLI flags)
  --n       <int>    number of angles per scene (default: 8)
  --dist    <float>  camera distance from Link (default: 350)
  --pitch   <float>  camera elevation angle in degrees (default: 25)
"""
import argparse, json, math, os, struct, sys, time, threading
from pathlib import Path
from typing import List, Optional

# ── paths ─────────────────────────────────────────────────────────────────────
_HERE   = Path(__file__).resolve().parent
_SOH3D  = _HERE.parent
_DECOMP = _SOH3D.parent / "oot3d-decomp"

sys.path.insert(0, str(_HERE))
sys.path.insert(0, str(_DECOMP / "tools"))

import azahar_rpc as A                                              # noqa: E402
from azahar_rpc import Rpc                                          # noqa: E402
from link_ctl import warp as _warp, link_head as _link_head        # noqa: E402 (oot3d-decomp)
from oracle_textbox import wait_no_textbox as _wait_no_textbox      # noqa: E402

# batch_capture lives in oot3d-decomp/tools and provides SCENE_LIST + helpers.
from batch_capture import (  # noqa: E402
    SCENE_LIST, GAME_TID, GPLAYSTATE, SCENE_OFF,
    connect, current_scene, wait_for_scene,
)

# ── RAM constants (from azahar_repl.py) ───────────────────────────────────────
CAM_EYE_OFF  = 0x1B8   # PlayState + offset → Vec3f eye
CAM_AT_OFF   = 0x1C4   # PlayState + offset → Vec3f look-at
CAM_EYE_NEXT = 0x3F0   # duplicate / interpolation buffer (next-frame)
CAM_AT_NEXT  = 0x3E4
ACTORCTX_OFF = 0x208C  # PlayState → actorCtx
A_POS        = 0x08    # Actor → world.pos Vec3f


# ── camera primitives ─────────────────────────────────────────────────────────

def _cam_write_eye(r: Rpc, ps: int, x: float, y: float, z: float) -> None:
    data = struct.pack("<3f", x, y, z)
    r.write(ps + CAM_EYE_OFF, data)
    r.write(ps + CAM_EYE_NEXT, data)


def _cam_write_at(r: Rpc, ps: int, x: float, y: float, z: float) -> None:
    data = struct.pack("<3f", x, y, z)
    r.write(ps + CAM_AT_OFF, data)
    r.write(ps + CAM_AT_NEXT, data)


def _link_pos(r: Rpc, ps: int) -> tuple:
    h = r.read32(ps + ACTORCTX_OFF + 0x0C + 2 * 8 + 4)
    raw = r.read(h + A_POS, 12)
    if raw is None or len(raw) < 12:
        return (0.0, 0.0, 0.0)
    return struct.unpack("<3f", raw)


def _cam_freeze_bg(eye_xyz, at_xyz, stop_evt):
    """Background thread: spam-write eye+at at ~125 Hz until stop_evt is set."""
    r2 = Rpc()
    try:
        for pid, tid, _ in r2.processes():
            if tid == GAME_TID:
                r2.select(pid)
                break
    except Exception:
        return
    ex, ey, ez = eye_xyz
    ax, ay, az = at_xyz
    while not stop_evt.is_set():
        try:
            ps2 = r2.read32(GPLAYSTATE)
            _cam_write_eye(r2, ps2, ex, ey, ez)
            _cam_write_at(r2, ps2, ax, ay, az)
        except Exception:
            pass
        time.sleep(0.008)


def _save_png(r: Rpc, path: str) -> Optional[str]:
    """Capture framebuffer → PNG. Returns path on success, None on failure."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    ppm = path[:-4] + ".ppm"
    ok = r.screenshot(ppm)
    if not ok:
        return None
    try:
        from PIL import Image
        Image.open(ppm).save(path)
        try:
            os.unlink(ppm)
        except OSError:
            pass
        return path
    except ImportError:
        return ppm  # PIL not available: return .ppm path
    except Exception:
        return None


# ── orbit sweep ───────────────────────────────────────────────────────────────

def orbit_sweep_scene(
    r: Rpc,
    scene_dir: Path,
    n: int = 8,
    dist: float = 350.0,
    pitch_deg: float = 25.0,
    force: bool = False,
) -> dict:
    """Run a full orbit sweep around Link's current position.

    Takes `n` evenly-spaced screenshots at `dist` units from Link at `pitch_deg`
    elevation. Saves to <scene_dir>/orbit_NNN_YYYyaw.png.

    Returns a dict with sweep metadata + per-angle results.
    """
    meta_path = scene_dir / "orbit_meta.json"
    if not force and meta_path.exists():
        existing = json.load(open(meta_path))
        shots_ok = sum(1 for s in existing.get("shots", []) if s.get("path"))
        if shots_ok == n:
            print(f"  [skip] orbit already captured ({shots_ok}/{n} shots) — use --force to redo")
            return existing

    ps = r.read32(GPLAYSTATE)
    tx, ty, tz = _link_pos(r, ps)
    print(f"  Link pos: ({tx:.1f}, {ty:.1f}, {tz:.1f})  dist={dist:.0f}  pitch={pitch_deg:.0f}°  n={n}")

    pitch = math.radians(pitch_deg)
    shots = []

    stop_evt: Optional[threading.Event] = None
    freeze_thread: Optional[threading.Thread] = None

    try:
        for i in range(n):
            yaw_deg = 360.0 * i / n
            yaw = math.radians(yaw_deg)
            ex = tx + dist * math.sin(yaw) * math.cos(pitch)
            ey = ty + dist * math.sin(pitch)
            ez = tz + dist * math.cos(yaw) * math.cos(pitch)

            # Stop old freeze, start new one for this angle.
            if stop_evt is not None and not stop_evt.is_set():
                stop_evt.set()
                if freeze_thread:
                    freeze_thread.join(timeout=0.3)
            stop_evt = threading.Event()
            freeze_thread = threading.Thread(
                target=_cam_freeze_bg,
                args=((ex, ey, ez), (tx, ty, tz), stop_evt),
                daemon=True,
            )
            freeze_thread.start()
            time.sleep(0.25)  # let freeze settle before shot

            shot_name = f"orbit_{i:03d}_{yaw_deg:.0f}yaw.png"
            shot_path = str(scene_dir / shot_name)
            out = _save_png(r, shot_path)
            shots.append({
                "index": i,
                "yaw_deg": round(yaw_deg, 1),
                "eye": [round(ex, 1), round(ey, 1), round(ez, 1)],
                "path": out,
            })
            status = "ok" if out else "FAILED"
            print(f"  [{i+1}/{n}] yaw={yaw_deg:.0f}°  {status}")
    finally:
        # Always release cam freeze when done.
        if stop_evt is not None and not stop_evt.is_set():
            stop_evt.set()
            if freeze_thread:
                freeze_thread.join(timeout=0.5)

    shots_ok = sum(1 for s in shots if s.get("path"))
    meta = {
        "target": [round(tx, 1), round(ty, 1), round(tz, 1)],
        "n": n,
        "dist": dist,
        "pitch_deg": pitch_deg,
        "shots_ok": shots_ok,
        "shots": shots,
    }
    scene_dir.mkdir(parents=True, exist_ok=True)
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)
    print(f"  orbit_sweep done: {shots_ok}/{n} shots  → {meta_path}")
    return meta


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenes", nargs="*", metavar="NAME",
                    help="Only sweep these scene names (default: all 18)")
    ap.add_argument("--force", action="store_true",
                    help="Re-sweep even if orbit_meta.json with full shot count exists")
    ap.add_argument("--status", action="store_true",
                    help="Print sweep status for all scenes (no oracle needed) then exit")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print what would be swept without driving the oracle")
    ap.add_argument("--out", metavar="DIR", default=None,
                    help="Output root dir (default: <soh3d>/scratch/oracle_batch/)")
    ap.add_argument("--n",     type=int,   default=8,   metavar="N",
                    help="Orbit angles per scene (default: 8)")
    ap.add_argument("--dist",  type=float, default=350, metavar="DIST",
                    help="Camera distance from Link in game units (default: 350)")
    ap.add_argument("--pitch", type=float, default=25,  metavar="DEG",
                    help="Camera elevation angle in degrees (default: 25)")
    args = ap.parse_args()

    out_root = Path(args.out) if args.out else _SOH3D / "scratch" / "oracle_batch"
    out_root.mkdir(parents=True, exist_ok=True)
    print(f"Output root: {out_root}")

    # -- status ----------------------------------------------------------------
    if args.status:
        ok_n = miss_n = partial_n = 0
        for spec in SCENE_LIST:
            meta_path = out_root / spec.name / "orbit_meta.json"
            if meta_path.exists():
                meta = json.load(open(meta_path))
                ok = meta.get("shots_ok", 0)
                n  = meta.get("n", "?")
                if ok == n:
                    status = "ok"
                    ok_n += 1
                else:
                    status = f"partial ({ok}/{n})"
                    partial_n += 1
            else:
                status = "MISSING"
                miss_n += 1
            print(f"  {spec.name:<28} {status}")
        print(f"\nTotal: {ok_n} complete, {partial_n} partial, {miss_n} missing")
        return

    # -- filter ----------------------------------------------------------------
    scene_filter = set(args.scenes) if args.scenes else None
    scenes = list(SCENE_LIST)
    if scene_filter:
        scenes = [s for s in scenes if s.name in scene_filter]
        missing = scene_filter - {s.name for s in scenes}
        if missing:
            print(f"[warn] Unknown scene names: {', '.join(sorted(missing))}")
            print(f"Known: {', '.join(s.name for s in SCENE_LIST)}")
    if not scenes:
        print("[error] No scenes to sweep.")
        sys.exit(1)

    # -- dry run ---------------------------------------------------------------
    if args.dry_run:
        print(f"\nDry run — would orbit-sweep {len(scenes)} scene(s):")
        for sp in scenes:
            print(f"  {sp.name:<28} entrance={hex(sp.entrance):<8} n={args.n}  "
                  f"dist={args.dist:.0f}  pitch={args.pitch:.0f}°")
        return

    # -- connect ---------------------------------------------------------------
    print("Connecting to Azahar oracle (RPC :45987)...")
    r = connect()
    init_scene = current_scene(r)
    print(f"Connected. Current scene: {init_scene}")
    print(f"Sweep params: n={args.n}  dist={args.dist:.0f}  pitch={args.pitch:.0f}°\n")

    # -- batch loop ------------------------------------------------------------
    summary_entries = []
    ok_count = fail_count = skip_count = 0

    for spec in scenes:
        scene_dir = out_root / spec.name
        print(f"\n=== {spec.name} (entrance={hex(spec.entrance)}, scene={spec.scene_num}) ===")

        # 1. Warp
        print(f"  Warping (entrance={hex(spec.entrance)})...")
        _warp(r, spec.entrance)

        # 2. Optional gate-TP (same logic as batch_capture)
        if spec.tp_after_warp is not None:
            time.sleep(2.0)
            tx, ty, tz = spec.tp_after_warp
            print(f"  Gate-TP to ({tx:.0f},{ty:.0f},{tz:.0f})...")
            ps = r.read32(GPLAYSTATE)
            h  = r.read32(ps + ACTORCTX_OFF + 0x0C + 2 * 8 + 4)
            r.write(h + A_POS, struct.pack("<3f", tx, ty, tz))

        # 3. Wait for scene
        if spec.scene_num != 0:
            print(f"  Waiting for scene {spec.scene_num}...", end=" ", flush=True)
            ok = wait_for_scene(r, spec.scene_num, timeout=18.0)
            actual = current_scene(r)
            if ok:
                print(f"loaded (scene={actual})")
            else:
                print(f"TIMEOUT — scene={actual}")
                summary_entries.append({
                    "scene_name": spec.name, "entrance": hex(spec.entrance),
                    "status": "warp_timeout", "shots_ok": 0,
                })
                fail_count += 1
                continue
        else:
            time.sleep(4.0)

        # 4. Extra settle + actor spawn
        time.sleep(2.0)

        # 5. Dismiss textboxes (safe no-op when none present)
        _wait_no_textbox(r, max_wait=12.0, press_interval=0.7, button="b", verbose=True)

        # 6. Settle before sweep
        print(f"  Settling {spec.settle}s...")
        time.sleep(spec.settle)

        # 7. Orbit sweep
        scene_dir.mkdir(parents=True, exist_ok=True)
        meta = orbit_sweep_scene(
            r, scene_dir,
            n=args.n, dist=args.dist, pitch_deg=args.pitch,
            force=args.force,
        )

        shots_ok = meta.get("shots_ok", 0)
        if meta.get("shots") is not None and shots_ok == 0 and not meta.get("path"):
            # Check if it was a skip
            pass

        entry = {
            "scene_name": spec.name,
            "entrance": hex(spec.entrance),
            "scene_num": spec.scene_num,
            "status": "ok" if shots_ok == args.n else
                      ("partial" if shots_ok > 0 else "failed"),
            "shots_ok": shots_ok,
            "shots_total": args.n,
        }
        summary_entries.append(entry)
        if entry["status"] == "ok":
            ok_count += 1
        elif entry["status"] == "partial":
            ok_count += 1  # partial still counts as progress
        else:
            fail_count += 1

    # -- write summary ---------------------------------------------------------
    # Merge new results into full summary (same pattern as batch_capture).
    summary_path = out_root / "orbit_summary.json"
    existing: dict = {}
    if summary_path.exists():
        try:
            old = json.load(open(summary_path))
            for entry in old.get("scenes", []):
                existing[entry["scene_name"]] = entry
        except Exception:
            pass
    for entry in summary_entries:
        existing[entry["scene_name"]] = entry
    # Fill gaps for scenes not run this time.
    full_results = []
    for spec in SCENE_LIST:
        if spec.name in existing:
            full_results.append(existing[spec.name])
        else:
            meta_path = out_root / spec.name / "orbit_meta.json"
            if meta_path.exists():
                meta = json.load(open(meta_path))
                n   = meta.get("n", args.n)
                sok = meta.get("shots_ok", 0)
                full_results.append({
                    "scene_name": spec.name, "entrance": hex(spec.entrance),
                    "scene_num": spec.scene_num,
                    "status": "ok" if sok == n else "partial",
                    "shots_ok": sok, "shots_total": n,
                })
            else:
                full_results.append({
                    "scene_name": spec.name, "entrance": hex(spec.entrance),
                    "scene_num": spec.scene_num,
                    "status": "missing", "shots_ok": 0, "shots_total": args.n,
                })

    full_ok  = sum(1 for e in full_results if e["status"] in ("ok", "partial"))
    full_fail = sum(1 for e in full_results if e["status"] not in ("ok", "partial", "skipped"))
    summary = {
        "n_angles": args.n, "dist": args.dist, "pitch_deg": args.pitch,
        "captured": full_ok, "failed": full_fail,
        "scenes": full_results,
    }
    with open(summary_path, "w") as f:
        json.dump(summary, f, indent=2)

    print(f"\n=== Sweep complete: {ok_count} scenes captured, {fail_count} failed ===")
    print(f"Full summary ({full_ok}/{len(SCENE_LIST)} scenes): {summary_path}")
    if fail_count:
        failed = [e for e in summary_entries if e["status"] == "failed"]
        for e in failed:
            print(f"  FAIL: {e['scene_name']} ({e['status']})")


if __name__ == "__main__":
    main()
