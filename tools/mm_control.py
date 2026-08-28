#!/usr/bin/env python3
"""Drive a managed native-MM instance through correlated FIFO RPC.

Talks to a long-lived headless ``zelda3d mm`` instance launched via
``tools/mm_game.py`` over two tagged request/reply FIFOs:
  - INPUT  ($SHIP_SCRIPTED_FIFO)  the SHARED libultraship scripted-input seam — synthetic pad
  - QUERY  ($ZELDA3D_MM_REPL)     the MM per-game REPL — PlayState-only queries

Input is unified in libultraship (same seam OoT will use); only decomp-typed state is per-game.

Subcommands:
  query "<cmd>"          send a REPL query, print the reply
                         (posinfo | actors [n] | warp <ent> | tp <x> <y> <z> | turn <deg>
                          | roomwarp <n> | cam <yawDeg> [dist] [h] | cam off
                          | linkinfo | linkform <human|deku|goron|zora|fd>
                          | linkequip <c-left|c-down|c-right> <ItemId> | linkitem <ItemId>
                          | mscale | mlist | mptrace <modelId|off> | ping)
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
  info                   shorthand for `query linkinfo`
  form <name>            request human/deku/goron/zora/fd through the real mask item-use path
  equip <slot> <ItemId>  equip a numeric MM ItemId on c-left/c-down/c-right
  item <ItemId>          request a numeric MM ItemId through the real item-use path
  trace <modelId|off>    select the existing renderer submission trace for one exact model

Examples:
  tools/mm_control.py pos
  tools/mm_control.py walk 3           # walk forward 3s
  tools/mm_control.py press 0x1000     # open the menu
  tools/mm_control.py actors 5
  tools/mm_control.py tp 1200 60 -500  # teleport Link to (1200, 60, -500)
  tools/mm_control.py cam 90 200 60    # side-profile from Link's right
  tools/mm_control.py form goron       # requires the Goron Mask in the active save
  tools/mm_control.py equip c-left 0x12 # equip an empty bottle on C-left
  tools/mm_control.py item 0x12        # request the empty bottle ItemId
  tools/mm_control.py info             # poll form, mask, action, speed, and position state
"""

import os
import sys
import time
from pathlib import Path

from fifo_rpc import FifoRpcClient, FifoRpcError

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGDIR = os.path.join(REPO, "scratch", "logs", "mm_n2")
INPUT_FIFO = Path(
    os.environ.get("SHIP_SCRIPTED_FIFO", os.path.join(LOGDIR, "mm_input.fifo"))
)
REPL_FIFO = Path(
    os.environ.get("ZELDA3D_MM_REPL", os.path.join(LOGDIR, "mm_repl.fifo"))
)
COMMAND_TIMEOUT = float(os.environ.get("ZELDA3D_MM_COMMAND_TIMEOUT", "5"))


def _request(fifo: Path, line: str) -> str:
    return FifoRpcClient(fifo, timeout=COMMAND_TIMEOUT).request(line)


def request_query(command: str) -> str:
    return _request(REPL_FIFO, command)


def request_input(command: str) -> str:
    return _request(INPUT_FIFO, command)


def query(cmd):
    reply = request_query(cmd)
    print(reply)
    return reply


def main(argv):
    if not argv:
        sys.exit(__doc__)
    cmd = argv[0]
    if cmd == "query":
        query(" ".join(argv[1:]))
    elif cmd == "input":
        print(request_input(" ".join(argv[1:])))
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
    elif cmd == "info":
        query("linkinfo")
    elif cmd == "form":
        if len(argv) != 2:
            sys.exit("usage: tools/mm_control.py form <human|deku|goron|zora|fd>")
        query("linkform " + argv[1])
    elif cmd == "equip":
        if len(argv) != 3:
            sys.exit(
                "usage: tools/mm_control.py equip <c-left|c-down|c-right> <ItemId 0x00..0xff>"
            )
        query("linkequip " + " ".join(argv[1:]))
    elif cmd == "item":
        if len(argv) != 2:
            sys.exit("usage: tools/mm_control.py item <ItemId 0x00..0xff>")
        query("linkitem " + argv[1])
    elif cmd == "trace":
        if len(argv) != 2:
            sys.exit("usage: tools/mm_control.py trace <modelId|off>")
        query("mptrace " + argv[1])
    elif cmd == "walk":
        secs = float(argv[1]) if len(argv) > 1 else 2.0
        x = argv[2] if len(argv) > 2 else "0"
        y = argv[3] if len(argv) > 3 else "72"
        _request(INPUT_FIFO, "enable 1")
        _request(INPUT_FIFO, f"stick {x} {y}")
        try:
            time.sleep(secs)
        finally:
            _request(INPUT_FIFO, "stick 0 0")
        print(f"walked {secs}s stick=({x},{y})")
    elif cmd == "press":
        mask = argv[1]
        ms = int(argv[2]) if len(argv) > 2 else 100
        _request(INPUT_FIFO, "enable 1")
        _request(INPUT_FIFO, f"btn {mask}")
        try:
            time.sleep(ms / 1000.0)
        finally:
            _request(INPUT_FIFO, "btn 0x0")
        print(f"pressed {mask} for {ms}ms")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (FifoRpcError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
