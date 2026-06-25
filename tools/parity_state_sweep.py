#!/usr/bin/env python3
"""parity_state_sweep.py — DISCRETE-STATE Link anim-SELECTION parity sweep (SoH3D vs OoT3D oracle).

The discrete-state analog of parity_speed_sweep.py (which covers the locomotion CONTINUUM by
speed). This tool drives both engines INTO a named discrete state (idle, walk, run, and — as their
setup paths get built — shield/attack/jump/climb/swim/carry/...) and compares the SELECTED CSAB
name each side resolves. Both sides live in the SAME OoT3D CSAB namespace (SoH3D plays the OoT3D
rig's CSABs), so the names compare directly. This is the "mature the SWEEP" / per-state PASS-FAIL
parity report (respawn_brief task #3).

WHY a separate tool from the speed sweep: locomotion is a continuum the speed sweep already bins by
speedXZ. Discrete states are reached by a BUTTON/CONTEXT recipe, not a stick magnitude, and the two
engines use DIFFERENT button-bit namespaces (SoH3D `btnhold` = N64 controller.h bits; oracle
set_input = 3DS PadState bits). So each state carries a per-SIDE input recipe, not a shared mask.

READOUT SURFACE (both already exist — see respawn_brief §3):
  SoH3D : REPL `linkanimstate` -> resolved base CSAB + speedXZ + st1 ; `btnhold <n64hexmask> <frames>`
          (B=0x4000 A=0x8000 R=0x0010 L=0x0020 Z=0x2000 Start=0x1000) ; `walkhold <frames> <sx> <sy>`.
  oracle: animId @ PLAYER+0x254+0x30 -> name via player_animid_names.json ; set_input(btnMask3ds, (cx,cy)).

THE HARD PART (honest): driving each side INTO a state headless. Easy = idle/walk/run (done, live-
verified). Context-gated (attack needs a drawn sword; jump needs a ledge; climb needs a wall;
swim/dive needs deep water; carry needs a liftable + grab) need scene setup and are listed as
gated=True TODOs — they print SKIP with the reason rather than faking a result. As each setup path
is built, flip gated->False and fill its recipe. Ground truth for what a gated state SHOULD select
can also be read STATICALLY (the OoT3D anim-group table @0x53a5f8 + action-func immediates — the
task-#2 technique; see oot3d-decomp docs/player_anim_states.md §6c).

USAGE
  tools/parity_state_sweep.py                 # run all non-gated states, print PASS/FAIL table
  tools/parity_state_sweep.py --json out.json
  tools/parity_state_sweep.py --skip-oracle   # SoH3D side only
  tools/parity_state_sweep.py --list          # list states + gated status, run nothing
"""
import argparse, json, math, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import parity_speed_sweep as S  # reuse soh_cmd/soh_state/soh_ensure_free/Oracle/oracle_warp_open  # noqa: E402

KOKIRI = 0xEE
IDLE_MAX = 0.5   # speedXZ below this = standing

# Logical button -> (N64 bit for SoH3D btnhold, 3DS button name for oracle buttons_mask).
BTN = {"A": (0x8000, "a"), "B": (0x4000, "b"), "R": (0x0010, "r"),
       "L": (0x0020, "l"), "Start": (0x1000, "start")}


# A state recipe. `kind`:
#   "idle"  : settle with no input, read the standing anim.
#   "stick" : hold the forward stick at magnitude `mag` (locomotion).
#   "button": hold logical buttons `btns` (+ optional stick) — for shield/attack/etc.
# `gated`=True states have no working headless setup yet (need scene context); they SKIP.
# LOCOMOTION (walk/run) is the SPEED CONTINUUM and is DELEGATED to parity_speed_sweep, which compares
# at matched SPEED (the N64 stick ~±84 and 3DS pad ±100 map a magnitude to DIFFERENT speed, so a
# fixed-magnitude compare here would mis-read idle/run at the boundary — exactly the calibration that
# tool already solves and reports PASS for after the #117 walk-gate fix). The state sweep OWNS the
# genuinely DISCRETE states: idle + the button/context-gated ones below.
STATES = [
    {"name": "idle", "kind": "idle",
     "note": "standing, no input -> nml_wait* fidget/idle"},
    {"name": "walk", "kind": "delegated",
     "note": "locomotion continuum -> run parity_speed_sweep.py (matched-speed; PASS post-#117)"},
    {"name": "run",  "kind": "delegated",
     "note": "locomotion continuum -> run parity_speed_sweep.py (matched-speed)"},

    # --- gated: need scene/equipment setup before they can be driven headless (TODO) ---
    {"name": "shield",   "kind": "button", "btns": ["R"], "gated": True,
     "note": "hold R -> nml_*tate* (shield). child needs Deku shield equipped"},
    {"name": "attack",   "kind": "button", "btns": ["B"], "gated": True,
     "note": "B -> ft_* slash. needs a drawn sword (child: Kokiri sword scene)"},
    {"name": "jump",     "kind": "button", "gated": True,
     "note": "run off a ledge -> nml_*jump*. needs a ledge (positional)"},
    {"name": "climb",    "kind": "button", "gated": True,
     "note": "grab+climb a wall -> nml_Fclimb_*/nml_climb_*. needs a climbable"},
    {"name": "carry",    "kind": "button", "gated": True,
     "note": "lift+hold (cucco/pot) -> nml_carry*. Kakariko 0xDB recipe (brief §SESSION5)"},
    {"name": "swim",     "kind": "button", "gated": True,
     "note": "in deep water -> sw_swim*. needs a water scene"},
    {"name": "damage",   "kind": "button", "gated": True,
     "note": "take a hit -> nml_*damage*. needs a hazard/enemy"},
]


def is_idle(name):  return bool(name) and ("wait" in name or "_nwait" in name)
def is_run(name):   return bool(name) and "run" in name and "walk" not in name
def is_walk(name):  return bool(name) and "walk" in name


# ---------------- SoH3D side ----------------
def soh_reach(st):
    """Drive SoH3D into state `st`; return the resolved base CSAB (or None)."""
    S.soh_cmd(f"warp 0x{KOKIRI:x}"); time.sleep(2.5)
    S.soh_ensure_free()
    S.soh_cmd("link 1")
    kind = st["kind"]
    base_last = None
    if kind == "idle":
        for _ in range(8):
            base, spd, st1 = S.soh_state()
            if base and (spd is None or spd < IDLE_MAX):
                base_last = base
            time.sleep(0.1)
        return base_last
    if kind == "button":
        mask = 0
        for b in st.get("btns", []):
            mask |= BTN[b][0]
        for i in range(int(1.2 * 20)):
            if i % 4 == 0 and mask:
                S.soh_cmd(f"btnhold 0x{mask:x} 30")
            base, _, _ = S.soh_state()
            if base:
                base_last = base
            time.sleep(1 / 20)
        S.soh_cmd("btnhold 0 0")
        return base_last
    return None


# ---------------- oracle side ----------------
def ora_reach(ora, st):
    """Drive the oracle into state `st`; return the selected CSAB name (or None)."""
    kind = st["kind"]
    if kind == "idle":
        S.oracle_warp_open(); ora._reactor()
        # settle with neutral stick; read the standing anim
        _, name = ora._drive(0, 0, int(1.0 * 30))
        return name
    if kind == "button":
        import azahar_rpc as A
        mask = A.buttons_mask([BTN[b][1] for b in st.get("btns", [])]) if st.get("btns") else 0
        S.oracle_warp_open(); ora._reactor()
        period = 1 / 30
        name = None
        for _ in range(int(1.2 * 30)):
            ora.rpc.set_input(mask, (0, 0))
            try:
                _, name = ora.animid()
            except Exception:
                pass
            time.sleep(period)
        return name
    return None


def verdict(state_name, soh, ora):
    """PASS if both sides select the same FAMILY for the state. Idle/walk/run use family predicates
    (the exact fidget variant differs frame-to-frame); other states require an exact name match."""
    if not soh or not ora or ora == "<wedged>":
        return "INCOMPLETE"
    if state_name == "idle":
        return "PASS" if (is_idle(soh) and is_idle(ora)) else "FAIL"
    if state_name == "walk":
        return "PASS" if (is_walk(soh) and is_walk(ora)) else "FAIL"
    if state_name == "run":
        return "PASS" if (is_run(soh) and is_run(ora)) else "FAIL"
    return "PASS" if soh == ora else "FAIL"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=None)
    ap.add_argument("--skip-oracle", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", default=None, help="comma list of state names to run")
    args = ap.parse_args()

    if args.list:
        for st in STATES:
            print(f"  {st['name']:8s} gated={st.get('gated', False)!s:5s} {st['note']}")
        return

    only = set(args.only.split(",")) if args.only else None
    ora = None
    if not args.skip_oracle:
        try:
            ora = S.Oracle()
        except Exception as e:
            print(f"oracle unavailable ({e}); SoH3D-only", file=sys.stderr)

    rows = []
    for st in STATES:
        if only and st["name"] not in only:
            continue
        if st["kind"] == "delegated":
            print(f"  [{st['name']:8s}] DELEGATED: {st['note']}")
            rows.append({"state": st["name"], "verdict": "DELEGATED", "note": st["note"]})
            continue
        if st.get("gated"):
            print(f"  [{st['name']:8s}] SKIP (gated): {st['note']}")
            rows.append({"state": st["name"], "verdict": "SKIP", "note": st["note"]})
            continue
        soh = soh_reach(st)
        oraname = ora_reach(ora, st) if ora else None
        v = verdict(st["name"], soh, oraname)
        print(f"  [{st['name']:8s}] {v:10s} soh={soh}  oracle={oraname}")
        rows.append({"state": st["name"], "verdict": v, "soh": soh, "oracle": oraname})

    npass = sum(1 for r in rows if r["verdict"] == "PASS")
    nfail = sum(1 for r in rows if r["verdict"] == "FAIL")
    nskip = sum(1 for r in rows if r["verdict"] in ("SKIP", "INCOMPLETE", "DELEGATED"))
    print(f"\n# {npass} PASS, {nfail} FAIL, {nskip} SKIP/DELEGATED (of {len(rows)})")
    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        json.dump(rows, open(args.json, "w"), indent=2)
        print(f"# wrote {args.json}")


if __name__ == "__main__":
    main()
