#!/usr/bin/env python3
"""parity_selection_sweep.py — automatic Link anim-SELECTION parity sweep (Zelda3D vs OoT3D oracle).

The deliverable for #117's "mature parity sweep" mandate: drive a list of Link locomotion states on
BOTH the live Zelda3D game and the OoT3D oracle (Azahar), capture which CSAB each side SELECTS per
frame, and report every state where the selected CSAB set diverges. Both sides resolve into the SAME
OoT3D CSAB namespace (Zelda3D plays the OoT3D rig's own CSABs), so the names compare directly.

SIDES
- ORACLE: tools/oracle_link_animid.py reads OoT3D's live curAnimId (PLAYER+0x254+0x30) -> CSAB name
  via the ZAR-order table (oot3d-decomp player_animid_names.json, RE'd from FUN_0034807c).
- Zelda3D:  REPL `linkanimstate` reports the resolved base CSAB per poll (the name Zelda3D draws).

Both are driven by an analog-stick hold (the oracle via its Input mod, Zelda3D via `walkhold`), warped
to open ground (Kokiri Forest 0xEE) so locomotion sustains. Cutscene-safe: both sides poll until
free gameplay (stateFlags1 & IN_CUTSCENE == 0) before driving.

USAGE
  tools/parity_selection_sweep.py                 # full locomotion sweep, print a table
  tools/parity_selection_sweep.py --states run,walk,idle
  tools/parity_selection_sweep.py --frames 40 --json scratch/parity/sel_sweep.json

NOTE: this is the SELECTION half. The GEOMETRY half (does the selected CSAB's POSE match per frame)
is tools/parity_pose_diff.py + skindump / oracle_link_pose.py. A state can select right yet pose
wrong (or vice-versa); run both for full parity.
"""
import argparse, json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import azahar_rpc as A  # noqa: E402

GPLAYSTATE = 0x0050AF34
PLAYER_STATE1_IN_CUTSCENE = 0x20000000
KOKIRI_ENTRANCE = 0xEE

# state name -> (stick_x, stick_y, buttons). Forward = +y.
# NOTE: this raw-stick sweep cannot compare INTERMEDIATE-speed states because the N64 stick (~±84) and
# the 3DS circle pad (±100) map a magnitude to different speeds. The fix is the SPEED-CALIBRATED sweep
# tools/parity_speed_sweep.py, which compares selection at matched speedXZ. That sweep CONFIRMED a REAL
# divergence at walk speed (NOT the "calibration artifact" earlier sessions assumed): at speedXZ~1.0 the
# oracle selects nml_walk_free while Zelda3D selects nml_run_free — Zelda3D reads the live N64 player anim
# (one run anim, speed-scaled playback) and maps it straight to nml_run_free, whereas OoT3D/Grezzo
# SELECTS distinct walk/run CSABs by speed. Use parity_speed_sweep.py for walk; this tool stays valid
# for the unambiguous endpoints idle (no motion) and run (full deflection = top speed on both sides).
STATES = {
    "idle":     (0,   0,   ""),
    "walk":     (0,   40,  ""),  # advisory only (calibration caveat above)
    "run":      (0,   127, ""),
    "backwalk": (0,  -100, ""),
}


# ---------- Zelda3D side (REPL) ----------
def soh_cmd(text):
    p = subprocess.run([os.path.join(HERE, "zelda3d_repl.py"), "cmd", text],
                       capture_output=True, text=True, timeout=15)
    return (p.stdout or "").strip().splitlines()[:1] and (p.stdout or "").strip().splitlines()[0] or ""


def soh_state():
    """Return (resolvedBaseCsab, st1) parsed from linkanimstate, or (None, None)."""
    line = soh_cmd("linkanimstate")
    base, st1 = None, None
    for tok in line.split():
        if tok.startswith("base="):
            base = tok[5:]
        if tok.startswith("st1=0x"):
            try:
                st1 = int(tok[4:], 16)
            except ValueError:
                pass
    if base == "(unmapped)" or base == "(null)":
        base = None
    return base, st1


def soh_ensure_free():
    for attempt in range(2):
        for _ in range(12):
            _, st1 = soh_state()
            if st1 == 0:
                return True
            time.sleep(0.6)
        soh_cmd(f"warp 0x{KOKIRI_ENTRANCE:x}")
        time.sleep(2.5)
    return False


def _dominant(samples):
    """The CSAB held longest across the BACK HALF of the capture (steady-state selection).
    Transitions live in the front half; the steady selection is what we compare."""
    back = samples[len(samples) // 2:] or samples
    counts = {}
    for s in back:
        counts[s] = counts.get(s, 0) + 1
    return max(counts, key=counts.get) if counts else None


def soh_capture(stick_x, stick_y, buttons, frames, hz):
    """Reset to a clean idle, drive the input, return (steadyCsab, fullOrderedSet)."""
    soh_cmd(f"warp 0x{KOKIRI_ENTRANCE:x}")  # reset position so each state starts from spawn
    time.sleep(2.5)
    soh_ensure_free()
    soh_cmd("link 1")
    soh_cmd("gcam 1")
    samples, seen, period = [], [], 1.0 / hz
    for i in range(frames):
        # re-arm the hold every few polls (walkhold counts down per frame)
        if i % 5 == 0:
            soh_cmd(f"walkhold 60 {stick_x} {stick_y}")
        base, st1 = soh_state()
        if st1 == PLAYER_STATE1_IN_CUTSCENE:
            soh_ensure_free()
            continue
        if base:
            samples.append(base)
            if base not in seen:
                seen.append(base)
        time.sleep(period)
    soh_cmd("walkhold 0")
    return _dominant(samples), seen


# ---------- Oracle side (subprocess to oracle_link_animid.py) ----------
def oracle_capture(stick_x, stick_y, frames, hz):
    """Reset Link to spawn, drive the held stick; return (steadyCsab, fullOrderedSet).
    The steady selection is the LAST timeline entry (after the idle->target transition)."""
    oracle_warp_open()  # reset position per state so a prior run's drift doesn't carry over
    cmd = [os.path.join(HERE, "oracle_link_animid.py"), "--frames", str(frames),
           "--hz", str(hz), "--hold-circle", f"{stick_x},{stick_y}"]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=frames / hz + 30)
    except subprocess.TimeoutExpired:
        return None, None
    seen = []
    for ln in (p.stdout or "").splitlines():
        # timeline lines: "  [  4] animId 0x6c nml_run_free          morph=..."
        parts = ln.split()
        if "animId" in parts:
            j = parts.index("animId")
            if j + 2 < len(parts):
                name = parts[j + 2]  # animId <hexid> <name>
                if name not in seen:
                    seen.append(name)
    return (seen[-1] if seen else None), seen


def oracle_warp_open():
    """Warp the oracle to Kokiri Forest via oot3d-decomp link_ctl (open ground for sustained run)."""
    lc = os.path.join(HERE, "..", "..", "oot3d-decomp", "tools", "link_ctl.py")
    if os.path.exists(lc):
        try:
            subprocess.run([lc, "warp", "0xEE"], capture_output=True, text=True, timeout=20)
            time.sleep(2.0)
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--states", default=",".join(STATES), help="comma list (default: all)")
    ap.add_argument("--frames", type=int, default=40)
    ap.add_argument("--hz", type=float, default=20.0)
    ap.add_argument("--json", default=None)
    ap.add_argument("--skip-oracle", action="store_true", help="Zelda3D side only")
    args = ap.parse_args()

    states = [s.strip() for s in args.states.split(",") if s.strip() in STATES]
    if not args.skip_oracle:
        oracle_warp_open()
    if not soh_ensure_free():
        print("WARN: Zelda3D never reached free gameplay (stuck in cutscene)", file=sys.stderr)

    results = {}
    for name in states:
        sx, sy, btn = STATES[name]
        soh_steady, soh_set = soh_capture(sx, sy, btn, args.frames, args.hz)
        if args.skip_oracle:
            ora_steady, ora_set = None, None
        else:
            ora_steady, ora_set = oracle_capture(sx, sy, args.frames, args.hz)
        match = (ora_steady is not None) and (soh_steady == ora_steady)
        results[name] = {"drive": [sx, sy, btn], "zelda3d_steady": soh_steady, "zelda3d_set": soh_set,
                         "oracle_steady": ora_steady, "oracle_set": ora_set, "match": match}

    # report — compare the STEADY-STATE selected CSAB (the front-half transition is morph's concern)
    print(f"\n{'state':<10} {'verdict':<8} {'Zelda3D steady':<24} {'OoT3D steady':<24}")
    print("-" * 70)
    for name in states:
        r = results[name]
        m = "OK" if r["match"] else ("SKIP" if r["oracle_steady"] is None else "DIVERGE")
        print(f"{name:<10} {m:<8} {(r['zelda3d_steady'] or '-'):<24} {(r['oracle_steady'] or '(skipped)'):<24}")
    diverged = [n for n in states if not results[n]["match"] and results[n]["oracle_steady"] is not None]
    print(f"\n{len(diverged)} divergent state(s): {', '.join(diverged) or 'none'}")
    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        json.dump(results, open(args.json, "w"), indent=2)
        print(f"# wrote {args.json}")
    return 1 if diverged else 0


if __name__ == "__main__":
    sys.exit(main())
