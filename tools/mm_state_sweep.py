#!/usr/bin/env python3
"""mm_state_sweep.py — regression sweep for the MM Zelda3D_PlayerForce* layer.

Drives Link through every ported force-state via the `linkstate` command owned by
`2ship/2s2h/zelda3d/repl/mm3d_link_repl.cpp` and the force interface owned by
`2ship/2s2h/zelda3d/mm3d_player_force.{c,h}`. The hook must install its expected action func (or,
for a context-gated state with no live context, decline SAFELY) and MM must survive. This is the
mm_sweep.py orchestrator's state-coverage half (re_control_debug_ backlog #11); the
OoT3D/MM3D-oracle parity comparison is a later add-on (blocked on the MM oracle).

It exists because runtime is the only place some hook bugs surface: it was written right after a
force-hook (carry) that installed cleanly but null-deref-crashed MM a frame later — a sweep like
this catches that class automatically.

Usage:
    tools/mm_game.py start          # or let this script's --start do it
    tools/mm_state_sweep.py         # sweep a running instance
    tools/mm_state_sweep.py --start # (re)start MM headless, then sweep
Exit code: 0 = all states OK, 1 = a state crashed MM or gave an unexpected reply.
"""

import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
import mm_control

MM_GAME = os.path.join(REPO, "tools", "mm_game.py")

# (state, token that proves the hook installed its action func, [tokens that mean "safely declined"
#  because the live context — held actor / wall / NPC — isn't present, which is an EXPECTED pass]).
STATES = [
    ("idle", "Player_Action_Idle", []),
    ("walk", "Player_Action_13", []),
    ("run", "Player_Action_14", []),
    ("turn", "Player_Action_TurnInPlace", []),
    ("roll", "Player_Action_26", []),
    ("throw", "Player_Action_42", []),
    ("attack", "Player_Action_84", []),
    ("jump", "Player_Action_25", []),
    ("shield", "Player_Action_18", []),
    ("getitem", "Player_Action_WaitForPutAway", []),
    ("talk", "talking", ["NO NPC"]),  # needs a nearby NPC
    ("putdown", "Player_Action_41", []),
    ("death", "health=0", []),
    ("damage", "Player_Action_20", []),
    ("hang", "Player_Action_48", []),
    (
        "carry",
        "Player_UpperAction_CarryActor",
        ["NO heldActor"],
    ),  # needs a lifted actor
    (
        "climb",
        "Player_Action_50",
        [],
    ),  # reply always names it; "no wallPoly"/"declined" are OK sub-states
    ("swim", "Player_Action_54", []),
    ("swimdive", "Player_Action_59", []),
    ("itemuse", "Player_Action_68", []),
    ("backwalk", "Player_Action_15", ["decode declined"]),
]


def q(cmd):
    return mm_control.request_query(cmd)


def mm_alive():
    result = subprocess.run(
        [MM_GAME, "status"], capture_output=True, text=True, check=False
    )
    return result.returncode == 0


def wait_for_gameplay(timeout=60):
    t = 0
    while t < timeout:
        r = q("posinfo")
        if "scene=" in r:
            return True
        time.sleep(3)
        t += 3
    return False


def main(argv):
    if "--start" in argv and not mm_alive():
        subprocess.run([MM_GAME, "stop"], check=True)
        subprocess.run([MM_GAME, "start"], check=True)

    if not mm_alive():
        sys.exit("MM not running — start it (tools/mm_game.py start) or pass --start.")
    if not wait_for_gameplay():
        sys.exit("MM never reached gameplay (posinfo had no scene).")

    print(f"{'state':<10} {'verdict':<9} reply")
    print("-" * 78)
    fails = []
    for state, install_tok, decline_toks in STATES:
        q("linkstate idle")  # clean slate
        reply = q(f"linkstate {state}")
        time.sleep(1.0)  # let a Play_Update frame run (delayed crashes surface here)
        alive = mm_alive()
        if not alive:
            verdict = "CRASH"
            fails.append(state)
        elif not reply:
            verdict = "NO-REPLY"
            fails.append(state)
        elif install_tok in reply:
            verdict = "OK"
        elif any(tok in reply for tok in decline_toks):
            verdict = (
                "DECLINE"  # context-gated, safe — expected without the live context
            )
        else:
            verdict = "UNEXPECTED"
            fails.append(state)
        print(f"{state:<10} {verdict:<9} {reply[:56]}")
        if not alive:
            print("\n!! MM crashed — restarting for the remaining states")
            subprocess.run([MM_GAME, "stop"], check=True)
            subprocess.run([MM_GAME, "start"], check=True)
            wait_for_gameplay()

    print("-" * 78)
    if fails:
        print(f"FAIL: {len(fails)} state(s) crashed/unexpected: {', '.join(fails)}")
        return 1
    print(
        f"PASS: all {len(STATES)} force-states install cleanly (or safely decline); MM survived."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
