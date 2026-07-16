#!/usr/bin/env python3
"""parity_state_sweep.py — DISCRETE-STATE Link anim-SELECTION parity sweep (Zelda3D vs OoT3D oracle).

The discrete-state analog of parity_speed_sweep.py (which covers the locomotion CONTINUUM by
speed). This tool drives both engines INTO a named discrete state (idle, walk, run, and — as their
setup paths get built — shield/attack/jump/climb/swim/carry/...) and compares the SELECTED CSAB
name each side resolves. Both sides live in the SAME OoT3D CSAB namespace (Zelda3D plays the OoT3D
rig's CSABs), so the names compare directly. This is the "mature the SWEEP" / per-state PASS-FAIL
parity report (respawn_brief task #3).

WHY a separate tool from the speed sweep: locomotion is a continuum the speed sweep already bins by
speedXZ. Discrete states are reached by a BUTTON/CONTEXT recipe, not a stick magnitude, and the two
engines use DIFFERENT button-bit namespaces (Zelda3D `btnhold` = N64 controller.h bits; oracle
set_input = 3DS PadState bits). So each state carries a per-SIDE input recipe, not a shared mask.

READOUT SURFACE (both already exist — see respawn_brief §3):
  Zelda3D : REPL `linkanimstate` -> resolved base CSAB + speedXZ + st1 ; `btnhold <n64hexmask> <frames>`
          (B=0x4000 A=0x8000 R=0x0010 L=0x0020 Z=0x2000 Start=0x1000) ; `walkhold <frames> <sx> <sy>`.
  oracle: animId @ PLAYER+0x254+0x30 -> name via player_animid_names.json ; set_input(btnMask3ds, (cx,cy)).

THE HARD PART (honest), and how each state is now driven end-to-end:
  - idle / walk / run : input-reachable on both engines. idle = live oracle A/B (gt=oracle); walk/run
    = the SPEED CONTINUUM, delegated to parity_speed_sweep (matched-speed; PASS post-#117).
  - jump / swim / damage / shield / attack / climb : context-gated (need a ledge / deep water / an
    enemy hit / a drawn sword / a wall). DRIVEN on the Zelda3D side via the real OoT3D action func +
    canonical anim through REPL `linkstate <s>` (Zelda3D_PlayerForce* in z_player.c), which bypasses ONLY
    the entry gate headless can't satisfy — the action + anim are exactly what OoT3D runs, so Zelda3D's
    live CSAB-selection path is exercised faithfully. Read under `freeze` so the selection can't
    transition out before it's captured.
  - carry : the SESSION-5 cucco-grab recipe (Kakariko 0xDB) — a persistent two-source state; read the
    UPPER (carry-hold) CSAB.

GROUND TRUTH. The equipment-less oracle save carries NO sword/shield/liftable, so the gated states
CANNOT be driven live on the oracle (holding R/B just yields the idle fidget — verified). The
achievable ground truth for them is the OoT3D DECOMP: the CSAB family each state's action func plays,
via the N64->OoT3D-CSAB map (100%-verified vs the live zar; the jump_climb->hang and water->run_dive
reuse cross-confirmed vs the OoT3D binary anim-group table @0x53a5f8 in task #2). Each state carries
`gt` (ground-truth source: oracle|decomp) + `expect` (the CSAB family it must select). Honest by
construction: PASS means Zelda3D's DRIVEN selection resolved to the decomp-documented CSAB (not idle
fallback, age-correct in both zars), not that a live oracle A/B was faked.

USAGE
  tools/parity_state_sweep.py                 # run all states, print the PASS/FAIL table
  tools/parity_state_sweep.py --json out.json
  tools/parity_state_sweep.py --skip-oracle   # Zelda3D side only (idle then has no oracle A/B)
  tools/parity_state_sweep.py --only jump,carry
  tools/parity_state_sweep.py --list          # list states + gt/expect, run nothing
"""
import argparse, json, math, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import parity_speed_sweep as S  # reuse soh_cmd/soh_state/soh_ensure_free/Oracle/oracle_warp_open  # noqa: E402

KOKIRI = 0xEE
IDLE_MAX = 0.5   # speedXZ below this = standing

# Logical button -> (N64 bit for Zelda3D btnhold, 3DS button name for oracle buttons_mask).
BTN = {"A": (0x8000, "a"), "B": (0x4000, "b"), "R": (0x0010, "r"),
       "L": (0x0020, "l"), "Start": (0x1000, "start")}


# A state recipe. `kind`:
#   "idle"      : settle with no input, read the standing anim (oracle-confirmed, gt="oracle").
#   "delegated" : locomotion continuum -> parity_speed_sweep.py (matched-speed; see note below).
#   "forcestate": drive Zelda3D into the state via `linkstate <s>` under freeze (a real OoT3D action
#                 func + canonical anim; bypasses only the equipment/positional ENTRY gate that
#                 headless can't satisfy — see Zelda3D_PlayerForce* in z_player.c). Read the SELECTED
#                 base CSAB and compare to `expect` (the family the OoT3D decomp action-func plays).
#   "carry"     : the SESSION-5 cucco-grab recipe (Kakariko 0xDB) — a persistent two-source state;
#                 read the UPPER CSAB (the carry-hold) and compare to `expect`.
# `expect` = the OoT3D-decomp ground-truth CSAB family (substring) the state must select. `gt` names
#   the ground-truth SOURCE: "oracle" = live OoT3D A/B (only states the equipment-less oracle save can
#   reach — idle/walk/run); "decomp" = the action-func anim from oot3d-decomp (the faithful N64 twin,
#   N64->OoT3D-CSAB map 100%-verified vs the live zar; hang/dive cross-confirmed vs the OoT3D binary
#   anim-group table @0x53a5f8 in task #2). The oracle save carries NO sword/shield/liftable, so the
#   gated states cannot be driven live on the oracle — decomp IS the achievable ground truth for them.
# LOCOMOTION (walk/run) is the SPEED CONTINUUM, DELEGATED to parity_speed_sweep (matched-SPEED compare;
# a fixed-magnitude compare here would mis-read idle/run at the boundary — the calibration that tool
# already solves and reports PASS for after #117). The state sweep OWNS the genuinely DISCRETE states.
STATES = [
    {"name": "idle", "kind": "idle", "gt": "oracle", "expect": "wait",
     "note": "standing, no input -> nml_wait* fidget/idle"},
    {"name": "walk", "kind": "delegated",
     "note": "locomotion continuum -> run parity_speed_sweep.py (matched-speed; PASS post-#117)"},
    {"name": "run",  "kind": "delegated",
     "note": "locomotion continuum -> run parity_speed_sweep.py (matched-speed)"},

    # --- discrete states, driven via Zelda3D_PlayerForce* (REPL linkstate). gt=decomp (the equipment-
    #     less oracle save can't reach them live). expect = the CSAB family the OoT3D action func plays.
    {"name": "jump",   "kind": "forcestate", "force": "jump",   "gt": "decomp", "expect": "nml_jump",
     "note": "free-fall/jump (func_80838940 -> Player_Action_8084411C + normal_jump) -> nml_jump"},
    {"name": "swim",   "kind": "forcestate", "force": "swim",   "gt": "decomp", "expect": "sw_swim",
     "note": "swim-wait (func_80838F18 -> Player_Action_8084D610 + swimer_swim_wait) -> sw_swim_wait"},
    {"name": "damage", "kind": "forcestate", "force": "damage", "gt": "decomp", "expect": "_hit",
     "note": "grounded recoil (Player_Action_8084370C + normal_front_hit) -> nml_front_hit"},
    {"name": "shield", "kind": "forcestate", "force": "shield", "gt": "decomp", "expect": "defense",
     "note": "defend (Player_Action_80843188 + ANIMGROUP_defense) -> nml_defense(_free)"},
    {"name": "attack", "kind": "forcestate", "force": "attack", "gt": "decomp", "expect": "kiru",
     "note": "sword slash (Player_Action_808502D0 + fighter_normal_kiru) -> ft_nml_kiru"},
    {"name": "climb",  "kind": "forcestate", "force": "climb",  "gt": "decomp", "expect": "hang",
     "note": "wall-grab/hang (jump_climb -> hang family, task #2) -> nml_hang_* ; forced via climb anim"},
    {"name": "putdown", "kind": "forcestate", "force": "putdown", "gt": "decomp", "expect": "nml_put",
     "note": "PUT_DOWN branch of func_8083EAF0 (Player_Action_808464B0 + ANIMGROUP_put) -> nml_put; "
             "distinct from THROW (func_8083EA94). Run standalone: reads the selected base CSAB"},

    # --- carry: two-source state, driven by the SESSION-5 cucco-grab recipe (Kakariko 0xDB). Read the
    #     UPPER csab (the carry hold); lower is locomotion (speed-gated, covered by the speed sweep).
    {"name": "carry",  "kind": "carry", "gt": "decomp", "expect": "carryB",
     "note": "lift+hold a cucco (Kakariko 0xDB) -> upper=nml_carryB_wait, lower=locomotion"},
]


def is_idle(name):  return bool(name) and ("wait" in name or "_nwait" in name)
def is_run(name):   return bool(name) and "run" in name and "walk" not in name
def is_walk(name):  return bool(name) and "walk" in name


def soh_animstate():
    """Parse the full `linkanimstate` line -> (base, upper, speedXZ, st1). base/upper are CSAB names
    (None if unmapped/null). Extends parity_speed_sweep.soh_state, which only returns base."""
    line = S.soh_cmd("linkanimstate")
    base = upper = None
    spd = st1 = None
    # tokens: base=<name> ... | upper=<name> ... | ... speedXZ=<f> st1=0x<hex>
    seg = line.split("upper=")
    if seg and "base=" in seg[0]:
        base = seg[0].split("base=")[1].split()[0]
    if len(seg) > 1:
        upper = seg[1].split()[0]
    for tok in line.split():
        if tok.startswith("speedXZ="):
            try: spd = float(tok[8:])
            except ValueError: pass
        elif tok.startswith("st1=0x"):
            try: st1 = int(tok[4:], 16)
            except ValueError: pass
    for v in ("base", "upper"):
        if locals()[v] in ("(unmapped)", "(null)", "(none)"):
            if v == "base": base = None
            else: upper = None
    return base, upper, spd, st1


# ---------------- Zelda3D side ----------------
def soh_reach(st):
    """Drive Zelda3D into state `st`; return the resolved CSAB to compare (base, or upper for carry)."""
    kind = st["kind"]

    if kind == "idle":
        S.soh_cmd(f"warp 0x{KOKIRI:x}"); time.sleep(2.5)
        S.soh_ensure_free(); S.soh_cmd("link 1")
        base_last = None
        for _ in range(8):
            base, _, spd, _ = soh_animstate()
            if base and (spd is None or spd < IDLE_MAX):
                base_last = base
            time.sleep(0.1)
        return base_last

    if kind == "forcestate":
        # warp to clean open ground, settle to free gameplay, then FORCE the state under freeze so the
        # selected anim is read deterministically before any Play_Update can transition it back.
        S.soh_cmd(f"warp 0x{KOKIRI:x}"); time.sleep(2.5)
        S.soh_ensure_free(); S.soh_cmd("link 1")
        S.soh_cmd("freeze 1")
        S.soh_cmd(f"linkstate {st['force']}")
        base, _, _, _ = soh_animstate()
        S.soh_cmd("freeze 0")
        S.soh_cmd("linkstate idle")  # reset out of the forced state
        return base

    if kind == "carry":
        return soh_force_carry()

    return None


KAK = 0xDB  # Kakariko: native cuccos (id 0x19) + open ground; the SESSION-5 carry recipe scene.


def soh_force_carry():
    """SESSION-5 cucco-grab recipe: warp to Kakariko, freeze a cucco onto Link, tap A to lift, then
    walk. Returns the UPPER (carry-hold) CSAB. None if the grab didn't take."""
    S.soh_cmd(f"warp 0x{KAK:x}"); time.sleep(3.0)
    S.soh_ensure_free(); S.soh_cmd("link 1")
    # Link's position to overlap a cucco onto.
    info = S.soh_cmd("posinfo")
    lx = lz = None
    if "link=(" in info:
        try:
            lx, ly, lz = (float(v) for v in info.split("link=(")[1].split(")")[0].split(","))
        except Exception:
            ly = 0.0
    if lx is None:
        return None
    S.soh_cmd("asel 0x19"); S.soh_cmd("afreeze 1")
    S.soh_cmd(f"apos {lx + 10:.0f} {ly:.0f} {lz:.0f}")
    time.sleep(0.3)
    upper_last = None
    for _ in range(5):
        S.soh_cmd("btnhold 0x8000 4")  # tap A to grab
        time.sleep(0.4)
        _, upper, _, _ = soh_animstate()
        if upper and "carry" in upper:
            upper_last = upper
            break
    # advance to carry-WALK too (lower=locomotion), confirming the two-source path holds while moving
    S.soh_cmd("gcam 1")
    for _ in range(4):
        S.soh_cmd("walkhold 30 0 60"); time.sleep(0.25)
    _, upper, _, _ = soh_animstate()
    if upper and "carry" in upper:
        upper_last = upper
    S.soh_cmd("walkhold 0"); S.soh_cmd("afreeze 0"); S.soh_cmd("btnhold 0 0")
    return upper_last


# ---------------- oracle side (only idle is live-reachable; the save lacks equipment) ----------------
def ora_reach(ora, st):
    """Drive the oracle into state `st` and return the selected CSAB name. Only idle is reachable on
    the equipment-less oracle save (no sword/shield/liftable -> gated states stay idle live); returns
    None for non-idle so those fall back to the decomp ground truth."""
    if st["kind"] != "idle":
        return None
    S.oracle_warp_open(); ora._reactor()
    _, name = ora._drive(0, 0, int(1.0 * 30))  # settle with neutral stick; read the standing anim
    return name


def verdict(st, soh, ora):
    """PASS/FAIL for one state. gt=='oracle' (idle): both sides must select the same FAMILY (the live
    A/B). gt=='decomp': Zelda3D's driven selection must contain the decomp-derived expected CSAB family
    (`expect`) — the achievable ground truth for states the equipment-less oracle save can't reach."""
    if not soh:
        return "INCOMPLETE"
    if st.get("gt") == "oracle":
        if not ora or ora == "<wedged>":
            return "INCOMPLETE"
        if st["name"] == "idle":
            return "PASS" if (is_idle(soh) and is_idle(ora)) else "FAIL"
        return "PASS" if soh == ora else "FAIL"
    # gt == "decomp": Zelda3D selection vs the expected CSAB family (substring match).
    exp = st.get("expect")
    if not exp:
        return "INCOMPLETE"
    return "PASS" if exp in soh else "FAIL"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=None)
    ap.add_argument("--skip-oracle", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", default=None, help="comma list of state names to run")
    args = ap.parse_args()

    if args.list:
        for st in STATES:
            gt = st.get("gt", "-")
            print(f"  {st['name']:8s} kind={st['kind']:10s} gt={gt:9s} expect={st.get('expect','-'):10s} {st['note']}")
        return

    only = set(args.only.split(",")) if args.only else None
    ora = None
    if not args.skip_oracle:
        try:
            ora = S.Oracle()
        except Exception as e:
            print(f"oracle unavailable ({e}); Zelda3D-only", file=sys.stderr)

    rows = []
    for st in STATES:
        if only and st["name"] not in only:
            continue
        if st["kind"] == "delegated":
            print(f"  [{st['name']:8s}] DELEGATED: {st['note']}")
            rows.append({"state": st["name"], "verdict": "DELEGATED", "note": st["note"]})
            continue
        soh = soh_reach(st)
        oraname = ora_reach(ora, st) if ora else None
        v = verdict(st, soh, oraname)
        gt = st.get("gt", "-")
        exp = st.get("expect", "-")
        print(f"  [{st['name']:8s}] {v:10s} soh={soh}  expect={exp} gt={gt}"
              + (f" oracle={oraname}" if gt == "oracle" else ""))
        rows.append({"state": st["name"], "verdict": v, "soh": soh, "expect": exp,
                     "gt": gt, "oracle": oraname})

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
