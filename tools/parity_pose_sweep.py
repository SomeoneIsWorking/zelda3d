#!/usr/bin/env python3
"""parity_pose_sweep.py — geometry-level Link POSE parity sweep (the POSE analog of
parity_state_sweep.py, which verifies SELECTION only).

For each locomotion regime it drives BOTH engines into a sustained, matched-speed cycle, captures the
per-bone pose on each side, and runs tools/parity_pose_diff.py to get a median per-bone divergence and
PASS/FAIL verdict:

  Zelda3D : REPL `walkhold` (re-armed each tick so the hold survives headless's uncapped frame rate) until
          linkanimstate reports the target CSAB at steady speed, then `skindump` a short burst.
  oracle: oot3d-decomp link_ctl.py warp 0xEE -> tools/oracle_link_pose.py with --hold-circle.

Only locomotion (walk/run) is covered: it is the only motion the equipment-less oracle save can REACH
live, so it is the only state where a geometry-level oracle A/B is possible. idle is low-motion + fidget-
variant-dependent (skipped — selection parity already covers it). Gated states (attack/jump/climb/swim/
carry/damage) cannot be posed on the oracle save at all; their selection + frame advancement are decomp-
verified instead (see parity_state_sweep.py + oot3d-decomp docs/player_anim_states.md §6/6b/6c/6d).

Usage:
  tools/parity_pose_sweep.py                 # run walk+run, capture both sides, diff, PASS/FAIL
  tools/parity_pose_sweep.py --states run
  tools/parity_pose_sweep.py --reuse         # skip capture, just re-diff existing scratch/parity CSVs
"""
import argparse, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DECOMP = os.path.join(ROOT, "oot3d-decomp")  # in-repo submodule
OUT = os.path.join(ROOT, "scratch", "parity")
KOKIRI = 0xEE
PASS_DEG = 12.0  # median best-mean per-bone angle below this = MATCH (phase-granularity headroom)

# state -> (soh stick magnitude, oracle circle magnitude, target CSAB, min steady speedXZ)
STATES = {
    "walk": {"soh_mag": 50, "ora_mag": 55, "csab": "nml_walk_free", "minspd": 2.5},
    "run":  {"soh_mag": 127, "ora_mag": 100, "csab": "nml_run_free", "minspd": 5.0},
}


def soh(text, timeout=15):
    return subprocess.run([os.path.join(HERE, "zelda3d_repl.py"), "cmd", text],
                          capture_output=True, text=True, timeout=timeout).stdout.strip()


def soh_state():
    line = soh("linkanimstate")
    m = re.search(r"base=(\S+).*speedXZ=([\d.]+)", line)
    return (m.group(1), float(m.group(2))) if m else (None, 0.0)


def capture_soh(st, cfg):
    out = os.path.join(OUT, f"soh_{st}.csv")
    soh(f"warp 0x{KOKIRI:x}"); time.sleep(0.6)
    soh("link 1"); soh("gcam 1")
    mag = cfg["soh_mag"]
    steady = False
    for i in range(80):
        soh(f"walkhold 30 0 {mag}"); time.sleep(0.05)
        base, spd = soh_state()
        if base == cfg["csab"] and spd >= cfg["minspd"]:
            steady = True; print(f"  soh steady {st} iter {i}: {base} spd={spd}"); break
    if not steady:
        print(f"  soh {st}: NOT steady (last {base} spd={spd})"); soh("walkhold 0"); return None
    # capture a DENSE burst: headless renders many draws per logic tick, so consecutive draws collapse
    # to the same pose (skindump dedups). 120 draws -> ~40 distinct phase samples, enough for a tight
    # best-phase match (40 draws gave only ~19 distinct -> sparse coverage -> inflated divergence).
    soh(f"walkhold 400 0 {mag}")
    print("  soh", soh(f"skindump {out} 120"))
    for _ in range(20):
        soh(f"walkhold 400 0 {mag}"); time.sleep(0.03)
    soh("walkhold 0")
    return out


def capture_oracle(st, cfg):
    out = os.path.join(OUT, f"oracle_{st}.csv")
    lc = os.path.join(DECOMP, "tools", "link_ctl.py")
    subprocess.run([sys.executable, lc, "warp", "0xEE"], capture_output=True, text=True,
                   cwd=DECOMP, timeout=20)
    time.sleep(2.0)
    r = subprocess.run([sys.executable, os.path.join(HERE, "oracle_link_pose.py"),
                        "--frames", "90", "--hold-circle", f"0,{cfg['ora_mag']}",
                        "--settle", "0.9", "--out", out], capture_output=True, text=True, timeout=90)
    print("  oracle", r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()[-160:])
    return out if os.path.exists(out) else None


def diff(st, soh_csv, ora_csv):
    r = subprocess.run([sys.executable, os.path.join(HERE, "parity_pose_diff.py"),
                        "--soh", soh_csv, "--oracle", ora_csv, "--state", st],
                       capture_output=True, text=True, timeout=60)
    print(r.stdout.rstrip())
    m = re.search(r"MEDIAN best mean-angle = ([\d.]+)", r.stdout)
    return float(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--states", default="walk,run")
    ap.add_argument("--reuse", action="store_true", help="skip capture, re-diff existing CSVs")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    results = {}
    for st in [s.strip() for s in args.states.split(",") if s.strip()]:
        cfg = STATES.get(st)
        if not cfg:
            print(f"[{st}] unknown state"); continue
        print(f"== {st} ==")
        if args.reuse:
            soh_csv = os.path.join(OUT, f"soh_{st}.csv"); ora_csv = os.path.join(OUT, f"oracle_{st}.csv")
        else:
            ora_csv = capture_oracle(st, cfg)
            soh_csv = capture_soh(st, cfg)
        if not (soh_csv and ora_csv and os.path.exists(soh_csv) and os.path.exists(ora_csv)):
            print(f"[{st}] CAPTURE FAILED -> SKIP"); results[st] = None; continue
        results[st] = diff(st, soh_csv, ora_csv)
    print("\n== SUMMARY ==")
    rc = 0
    for st, deg in results.items():
        if deg is None:
            print(f"  {st:6s} SKIP (capture failed)"); continue
        ok = deg <= PASS_DEG
        print(f"  {st:6s} {deg:5.1f} deg  {'PASS' if ok else 'FAIL'}")
        if not ok: rc = 1
    sys.exit(rc)


if __name__ == "__main__":
    main()
