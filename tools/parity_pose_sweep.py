#!/usr/bin/env python3
"""parity_pose_sweep.py — geometry-level Link POSE parity sweep (the POSE analog of
parity_state_sweep.py, which verifies SELECTION only).

For each locomotion regime it drives BOTH engines into a sustained, matched-speed cycle, captures the
per-bone pose on each side, and runs tools/parity_pose_diff.py to get a median per-bone divergence and
PASS/FAIL verdict:

  Zelda3D : REPL `walkhold` (re-armed each tick so the hold survives headless's uncapped frame rate) until
          linkanimstate reports the target CSAB at steady speed, then `skindump` a short burst.
  oracle: the EMBEDDED harness (harness_process.spawn + `analog` stick hold + `az_linkjoints` per
          logic frame). Rewired 2026-07-23 — the old standalone-Azahar capture tools are gone.

States with a live pose oracle: idle, walk, run. Gated states (attack/jump/climb/swim/carry/damage)
cannot be posed on the equipment-less oracle save; their selection + frame advancement are decomp-
verified instead (see parity_state_sweep.py + oot3d-decomp docs/player_anim_states.md §6/6b/6c/6d).

Usage:
  tools/parity_pose_sweep.py                 # run idle+walk+run, capture both sides, diff, PASS/FAIL
  tools/parity_pose_sweep.py --states run
  tools/parity_pose_sweep.py --reuse         # skip capture, just re-diff existing scratch/parity CSVs
"""
import argparse, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DECOMP = os.path.join(ROOT, "oot3d-decomp")  # in-repo submodule
OUT = os.path.join(ROOT, "scratch", "parity")
KOKIRI = 0xEE
PASS_DEG = 12.0  # median best-mean per-bone angle below this = MATCH (phase-granularity headroom)

# state -> soh stick magnitude, oracle libretro analog Y (s16; negative = forward — see the walk
# calibration 2026-07-23: 14000 selects nml_45_turn, 17000..24000 walk, 32767 run), target CSAB,
# min steady speedXZ (SoH side only — the oracle ACTOR_SPEEDXZ probe reads ~-0.8 during a real
# walk, so oracle steadiness is judged on the CSAB name alone), oracle capture count.
STATES = {
    # idle: nml_wait_free is an 89-frame breathing loop — BOTH captures must span a full cycle or the
    # diff compares disjoint phases and reports a phantom divergence (first measurement 2026-07-23:
    # 12 oracle caps @f10-14 vs a SoH burst pinned @f0 -> "head 10 deg off"; full-cycle re-capture
    # -> parity). Hence caps=95 oracle-side and the long settle+dump SoH-side in capture_soh.
    "idle": {"soh_mag": 0,   "ora_mag": 0,      "csab": "nml_wait_free", "minspd": 0.0, "caps": 95},
    "walk": {"soh_mag": 50,  "ora_mag": -20000, "csab": "nml_walk_free", "minspd": 2.5, "caps": 45},
    "run":  {"soh_mag": 127, "ora_mag": -32767, "csab": "nml_run_free",  "minspd": 5.0, "caps": 45},
}

ORACLE_STATE = os.path.join(ROOT, "scratch", "kokiri_pose.state")
ANIM_NAMES = os.path.join(DECOMP, "tools", "skeldata", "player_animid_names.json")


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
    if mag == 0:
        # idle: wait until the wait anim is selected AND has advanced past the spawn walk-in stop,
        # then dump a burst long enough to span the full 89-frame breathing loop (idle phase-locks
        # to N64 curFrame, which advances per LOGIC frame — a short draw burst all lands on one f).
        base = None
        for i in range(80):
            time.sleep(0.1)
            base, _ = soh_state()
            if base == cfg["csab"]:
                break
        if base != cfg["csab"]:
            print(f"  soh {st}: never settled to {cfg['csab']} (last {base})"); return None
        time.sleep(2.0)  # get clear of the walk-in stop transition
        print("  soh", soh(f"skindump {out} 2000", timeout=60))
        time.sleep(8.0)  # let the dump span multiple seconds of logic frames
        return out
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


_ANIM_NAME_CACHE = None


def _anim_names():
    global _ANIM_NAME_CACHE
    if _ANIM_NAME_CACHE is None:
        with open(ANIM_NAMES) as f:
            _ANIM_NAME_CACHE = json.load(f)["names"]
    return _ANIM_NAME_CACHE


def _oracle_anim(h):
    r = h.send("az_linkanim")
    m = re.search(r"animId=(\d+) speedXZ=([-\d.]+)", r or "")
    if not m:
        return None, 0.0
    names = _anim_names()
    aid = int(m.group(1))
    return (names[aid] if 0 <= aid < len(names) else f"<id {aid}>"), float(m.group(2))


def capture_oracle(st, cfg):
    """Live oracle pose capture via the embedded harness.

    loadstate the cached Kokiri gameplay state (captured once via boot_to_gameplay +
    warp 0xEE), hold the circle pad until az_linkanim selects the target CSAB, then read
    az_linkjoints once per LOGIC frame (`run 2` — OoT3D draws one 3D frame per two
    retro_run calls). Rows: cap,t_ms,bone,r0..r8 (the live jointTable LOCAL rotation),
    the format parity_pose_diff.load_oracle_local expects.
    """
    sys.path.insert(0, HERE)
    from harness_gameplay import boot_to_gameplay
    from harness_process import spawn
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, f"oracle_{st}.csv")
    h = spawn()
    try:
        if os.path.exists(ORACLE_STATE):
            h.send(f"loadstate {ORACLE_STATE}")
            h.send("run 6")
        else:
            if not boot_to_gameplay(h, entrance=KOKIRI):
                print(f"  oracle {st}: boot_to_gameplay failed"); return None
            h.send(f"savestate {ORACLE_STATE}")
        mag = cfg["ora_mag"]
        if mag:
            h.send(f"analog 0 {mag}")
        name = None
        for i in range(40):
            h.send("run 30")
            name, _spd = _oracle_anim(h)
            if name == cfg["csab"]:
                print(f"  oracle steady {st} iter {i}: {name}"); break
        if name != cfg["csab"]:
            print(f"  oracle {st}: never reached {cfg['csab']} (last {name})"); return None
        rows = []
        for cap in range(cfg.get("caps", 45)):
            lines = h.send_multiline("az_linkjoints")
            for ln in lines:
                m = re.match(r"\s+(\d+)((?:\s+-?[\d.]+){9})$", ln)
                if m:
                    rows.append([cap, cap * 50, int(m.group(1))] + m.group(2).split())
            h.send("run 2")
        if mag:
            h.send("analog 0 0")
    finally:
        h.quit()
    if not rows:
        print(f"  oracle {st}: az_linkjoints produced no rows"); return None
    with open(out, "w") as f:
        f.write("cap,t_ms,bone,r0,r1,r2,r3,r4,r5,r6,r7,r8\n")
        for r in rows:
            f.write(",".join(str(x) for x in r) + "\n")
    ncaps = len({r[0] for r in rows})
    print(f"  oracle {st}: {ncaps} caps -> {out}")
    return out


def diff(st, soh_csv, ora_csv):
    r = subprocess.run([sys.executable, os.path.join(HERE, "parity_pose_diff.py"),
                        "--soh", soh_csv, "--oracle", ora_csv, "--state", st],
                       capture_output=True, text=True, timeout=60)
    print(r.stdout.rstrip())
    m = re.search(r"MEDIAN best mean-angle = ([\d.]+)", r.stdout)
    return float(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--states", default="idle,walk,run")
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
