#!/usr/bin/env python3
"""parity_speed_sweep.py — SPEED-CALIBRATED Link anim-SELECTION parity sweep (Zelda3D vs OoT3D oracle).

Supersedes an earlier raw-stick comparison for INTERMEDIATE states. That approach
drove both sides at the same raw analog magnitude and dismissed `walk` as a "calibration artifact"
because the N64 control stick (~±84) and the 3DS circle pad (±100) map a given magnitude to a
DIFFERENT speed. That excuse hid a REAL divergence. The fix is to compare at matched SPEED, not
matched stick: speedXZ is the physical quantity both engines animate from, and BOTH expose it
  - Zelda3D : REPL `linkanimstate` field speedXZ
  - oracle: PLAYER + 0x221c (linearVelocity, = N64 speedXZ; oot3d-decomp player_port.md)

METHOD (no fragile closed-loop): for each side, sweep the forward analog magnitude over a grid; at
each magnitude warp to open ground (Kokiri 0xEE) so the drive starts clean, hold the stick ~1s to let
speed ramp to steady state, then read (steadyspeedXZ, selectedCSAB). That yields a speed→selection
CURVE per side. We then bin both curves by speedXZ and report, per bin, the CSAB each side selects.
A bin where the two sides select different CSABs at the SAME speed is a TRUE divergence.

Both sides resolve into the same OoT3D CSAB namespace (Zelda3D plays the OoT3D rig's CSABs), so the
selected names compare directly: idle `nml_waitF_typeA_20f`, walk `nml_walk_free` (animId 0x47),
run `nml_run_free` (0x6c).

USAGE
  tools/parity_speed_sweep.py                       # full sweep, print the curve + divergence table
  tools/parity_speed_sweep.py --soh-mags 0,20,40,60,80,127 --ora-mags 0,30,50,70,100
  tools/parity_speed_sweep.py --json scratch/parity/speed_sweep.json
  tools/parity_speed_sweep.py --skip-oracle        # Zelda3D curve only

HISTORY (2026-06-25): this sweep first surfaced the #117 walk divergence — Zelda3D selected nml_run_free
at EVERY nonzero speed while the oracle selects nml_walk_free at walk speed. Root cause: Zelda3D read the
live N64 player anim (one run anim, speed-scaled playback) and mapped it straight to nml_run_free;
OoT3D/Grezzo instead SELECTS distinct walk/run CSABs by speed (RE'd: FUN_002be660 picker in the
FUN_004ba378 run action family). FIXED by Zelda3D_LinkWalkRunGate (zelda3d_link.cpp): below the measured
walk→run threshold (~3.6, in the oracle gap walk_max 3.2 / run_min 3.8) play nml_walk_free. After the
fix this sweep reports PASS (idle/walk/run selections match + walk→run transition windows overlap).
NOTE: the gate renders in `linksrc 3ds` mode (the architecture-directive keeper); in the default N64
retarget mode the pose is the N64 jointTable (one run cycle), so the walk cycle only shows in 3ds mode.
"""
import argparse, json, os, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

OOT3D_TID = 0x0004000000033500
GPLAYSTATE = 0x0050AF34
SPEEDXZ_OFF = 0x221c          # PLAYER -> linearVelocity (= N64 speedXZ)
SKELANIME_OFF = 0x254
ANIMID_OFF = 0x30
PLAYER_STATE1_IN_CUTSCENE = 0x20000000
KOKIRI_ENTRANCE = 0xEE

IDLE_MAX = 0.5  # speedXZ below this = not moving (idle)


def classify(curve):
    """From a speed->csab curve, derive each regime's selection + the walk->run transition window.
    Returns dict: idle_csab, walk_csab, run_csab, walk_max (highest speed still selecting walk),
    run_min (lowest speed selecting a run cycle). The transition window [walk_max, run_min] is the
    speed band where the side flips walk->run; two sides AGREE if their windows overlap (same
    threshold) — that is the parity metric, independent of either engine's exact speed-vs-stick map."""
    moving = [p for p in curve if p["speedXZ"] >= IDLE_MAX and p["csab"]]
    idle = [p for p in curve if p["speedXZ"] < IDLE_MAX and p["csab"]]
    def is_run(c): return c and "run" in c and "walk" not in c
    walks = [p for p in moving if not is_run(p["csab"])]
    runs = [p for p in moving if is_run(p["csab"])]
    return {
        "idle_csab": (idle[-1]["csab"] if idle else None),
        "walk_csab": (walks[0]["csab"] if walks else None),
        "run_csab": (runs[0]["csab"] if runs else None),
        "walk_max": (max(p["speedXZ"] for p in walks) if walks else None),
        "run_min": (min(p["speedXZ"] for p in runs) if runs else None),
    }


# ---------------- Zelda3D side (REPL) ----------------
def soh_cmd(text):
    p = subprocess.run([os.path.join(HERE, "zelda3d_repl.py"), "cmd", text],
                       capture_output=True, text=True, timeout=15)
    out = (p.stdout or "").strip().splitlines()
    return out[0] if out else ""


def soh_state():
    """(base_csab, speedXZ, st1) from linkanimstate, or (None, None, None)."""
    line = soh_cmd("linkanimstate")
    base, spd, st1 = None, None, None
    for tok in line.split():
        if tok.startswith("base="):
            base = tok[5:]
        elif tok.startswith("speedXZ="):
            try: spd = float(tok[8:])
            except ValueError: pass
        elif tok.startswith("st1=0x"):
            try: st1 = int(tok[4:], 16)
            except ValueError: pass
    if base in ("(unmapped)", "(null)"):
        base = None
    return base, spd, st1


def soh_ensure_free():
    for _ in range(2):
        for _ in range(12):
            _, _, st1 = soh_state()
            if st1 == 0:
                return True
            time.sleep(0.5)
        soh_cmd(f"warp 0x{KOKIRI_ENTRANCE:x}")
        time.sleep(2.5)
    return False


def soh_sample_at(mag, hold_s=1.2, hz=20.0):
    """Warp clean, drive forward at magnitude `mag` for hold_s, return (steadyspeedXZ, csab)."""
    soh_cmd(f"warp 0x{KOKIRI_ENTRANCE:x}")
    time.sleep(2.5)
    soh_ensure_free()
    soh_cmd("link 1"); soh_cmd("gcam 1")
    n = max(1, int(hold_s * hz))
    period = 1.0 / hz
    spd_last, base_last = 0.0, None
    for i in range(n):
        if i % 4 == 0:
            soh_cmd(f"walkhold 30 0 {mag}")
        base, spd, st1 = soh_state()
        if st1 == PLAYER_STATE1_IN_CUTSCENE:
            soh_ensure_free(); continue
        if spd is not None: spd_last = spd
        if base is not None: base_last = base
        time.sleep(period)
    soh_cmd("walkhold 0")
    return spd_last, base_last


def soh_curve(mags):
    out = []
    for m in mags:
        spd, csab = soh_sample_at(m)
        out.append({"mag": m, "speedXZ": round(spd, 3), "csab": csab})
        print(f"  [soh] mag={m:4d} speedXZ={spd:5.2f} -> {csab}")
    return out


# The oracle half of this sweep used to live here, driving the standalone Azahar
# over UDP RPC. That transport is gone; link_sweep.py's OracleSession drives the
# embedded harness and calls into this module for the SoH-side curve only
# (classify / soh_curve / windows_overlap). Run this tool with --skip-oracle, or
# go through link_sweep.py for a two-sided comparison.

# ---------------- compare ----------------
def windows_overlap(a, b):
    """Do two [walk_max, run_min] transition windows agree? They agree if the bands overlap, i.e. one
    side's walk_max is not clearly above the other's run_min (and vice-versa) — a consistent threshold."""
    if None in (a["walk_max"], a["run_min"], b["walk_max"], b["run_min"]):
        return None  # incomplete data (a side never sampled both walk and run)
    # the windows are [walk_max, run_min] per side; they are consistent if max(walk_max) <= min(run_min)
    # (a single threshold can satisfy both), allowing a small tolerance for oracle/engine speed noise.
    return max(a["walk_max"], b["walk_max"]) <= min(a["run_min"], b["run_min"]) + 0.6


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--soh-mags", default="0,20,40,60,80,100,127")
    ap.add_argument("--json", default=None)
    ap.add_argument("--skip-oracle", action="store_true")
    args = ap.parse_args()

    soh_mags = [int(x) for x in args.soh_mags.split(",")]

    print("== Zelda3D speed->selection curve ==")
    if not soh_ensure_free():
        print("WARN: Zelda3D stuck in cutscene", file=sys.stderr)
    soh = soh_curve(soh_mags)

    ora = []
    if not args.skip_oracle:
        sys.exit("this tool is SoH-side only — pass --skip-oracle, or use "
                 "link_sweep.py for a two-sided comparison against the harness oracle")

    sc = classify(soh)
    oc = classify(ora) if ora else None

    def fmt_win(c):
        wm = f"{c['walk_max']:.2f}" if c['walk_max'] is not None else "-"
        rm = f"{c['run_min']:.2f}" if c['run_min'] is not None else "-"
        return f"walk≤{wm} run≥{rm}"

    print(f"\n{'regime':<14} {'Zelda3D':<20} {'OoT3D oracle':<20} verdict")
    print("-" * 66)
    fails = []
    rows = [("idle select", "idle_csab"), ("walk select", "walk_csab"), ("run select", "run_csab")]
    for label, key in rows:
        s = sc.get(key); o = (oc.get(key) if oc else None)
        if args.skip_oracle or o is None:
            v = "—"
        elif s == o:
            v = "OK"
        else:
            v = "FAIL"; fails.append(label)
        print(f"{label:<14} {(s or '-'):<20} {(o or '(skipped)'):<20} {v}")
    # walk->run transition window agreement (the real threshold-parity metric)
    if oc:
        ov = windows_overlap(sc, oc)
        vv = "OK" if ov else ("—" if ov is None else "FAIL")
        if ov is False:
            fails.append("walk/run threshold")
        print(f"{'walk→run xs':<14} {fmt_win(sc):<20} {fmt_win(oc):<20} {vv}")

    print(f"\n{'PASS' if not fails else 'FAIL'}: {len(fails)} mismatch(es)"
          + (f": {', '.join(fails)}" if fails else ""))

    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        json.dump({"soh": soh, "oracle": ora, "soh_class": sc, "oracle_class": oc,
                   "fails": fails}, open(args.json, "w"), indent=2)
        print(f"# wrote {args.json}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
