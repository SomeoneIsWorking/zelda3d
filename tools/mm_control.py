#!/usr/bin/env python3
"""mm_control.py — driver for native-MM (2s2h) headless control (see docs/MM_NATIVE.md N3.4 2b).

Talks to a long-lived headless "zelda3d mm" instance (launched via tools/mm_game.sh) over TWO FIFOs:
  - INPUT  ($SHIP_SCRIPTED_FIFO)  the SHARED libultraship scripted-input seam — synthetic pad
  - QUERY  ($ZELDA3D_MM_REPL)     the MM per-game REPL — PlayState-only queries

Input is unified in libultraship (same seam OoT will use); only decomp-typed state is per-game.

Subcommands:
  query "<cmd>"          send a REPL query, print the reply
                         (posinfo | actors [n] | warp <ent> | tp <x> <y> <z> | turn <deg>
                          | roomwarp <n> | cam <yawDeg> [dist] [h] | cam off | mscale | mlist | ping)
  input "<cmd>"          send a raw scripted-input command (enable 0|1 | btn <hex> | stick <x> <y> | reset)
  walk <secs> [x] [y]    enable + hold the stick (default 0 72 = forward) for <secs>, then neutral
  press <hexmask> [ms]   tap a button mask (default 100ms), e.g. press 0x1000 = START
  pos                    shorthand for `query posinfo`
  actors [n]             shorthand for `query actors [n]`
  warp <entrance>        shorthand for `query warp <entrance>`
  tp <x> <y> <z>         shorthand for `query tp <x> <y> <z>` — teleport Link
  turn <deg>             shorthand for `query turn <deg>` — snap Link's yaw
  roomwarp <n>           shorthand for `query roomwarp <n>` — force-load room n
  cam <yaw> [dist] [h]   shorthand for `query cam ...` — persistent side framing
  cam off                releases the persistent cam framing

Examples:
  tools/mm_control.py pos
  tools/mm_control.py walk 3           # walk forward 3s
  tools/mm_control.py press 0x1000     # open the menu
  tools/mm_control.py actors 5
  tools/mm_control.py tp 1200 60 -500  # teleport Link to (1200, 60, -500)
  tools/mm_control.py cam 90 200 60    # side-profile from Link's right
"""
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGDIR = os.path.join(REPO, "scratch", "logs", "mm_n2")
INPUT_FIFO = os.environ.get("SHIP_SCRIPTED_FIFO", os.path.join(LOGDIR, "mm_input.fifo"))
REPL_FIFO = os.environ.get("ZELDA3D_MM_REPL", os.path.join(LOGDIR, "mm_repl.fifo"))


def _send(fifo, line):
    if not os.path.exists(fifo):
        sys.exit(f"FIFO {fifo} not present — is mm running? (tools/mm_game.sh start)")
    with open(fifo, "w") as f:
        f.write(line + "\n")


def _reply(fifo, line, settle=0.4):
    """Send one command and return the reply text appended to <fifo>.out."""
    out = fifo + ".out"
    mark = os.path.getsize(out) if os.path.exists(out) else 0
    _send(fifo, line)
    time.sleep(settle)
    if not os.path.exists(out):
        return ""
    with open(out, "r") as f:
        f.seek(mark)
        return f.read().strip()


def query(cmd):
    print(_reply(REPL_FIFO, cmd))


def main(argv):
    if not argv:
        sys.exit(__doc__)
    cmd = argv[0]
    if cmd == "query":
        query(" ".join(argv[1:]))
    elif cmd == "input":
        print(_reply(INPUT_FIFO, " ".join(argv[1:])))
    elif cmd == "pos":
        query("posinfo")
    elif cmd == "actors":
        query("actors " + (argv[1] if len(argv) > 1 else "0"))
    elif cmd == "warp":
        query("warp " + argv[1])
    elif cmd == "tp":
        query("tp " + " ".join(argv[1:4]))
    elif cmd == "turn":
        query("turn " + argv[1])
    elif cmd == "roomwarp":
        query("roomwarp " + argv[1])
    elif cmd == "cam":
        query("cam " + " ".join(argv[1:]))
    elif cmd == "walk":
        secs = float(argv[1]) if len(argv) > 1 else 2.0
        x = argv[2] if len(argv) > 2 else "0"
        y = argv[3] if len(argv) > 3 else "72"
        _send(INPUT_FIFO, "enable 1")
        _send(INPUT_FIFO, f"stick {x} {y}")
        time.sleep(secs)
        _send(INPUT_FIFO, "stick 0 0")
        print(f"walked {secs}s stick=({x},{y})")
    elif cmd == "press":
        mask = argv[1]
        ms = int(argv[2]) if len(argv) > 2 else 100
        _send(INPUT_FIFO, "enable 1")
        _send(INPUT_FIFO, f"btn {mask}")
        time.sleep(ms / 1000.0)
        _send(INPUT_FIFO, "btn 0x0")
        print(f"pressed {mask} for {ms}ms")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
