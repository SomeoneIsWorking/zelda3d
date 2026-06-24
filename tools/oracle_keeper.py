#!/usr/bin/env python3
"""oracle_keeper.py — keep the Azahar OoT3D oracle's Link alive (and optionally time-pinned)
so it survives coordinate-matched A/B parity sessions.

WHY: the oracle's save is a weak 3-heart child; warping it to dangerous spots (Lake Hylia deep
water → drown, Hyrule Field night → stalchildren) repeatedly GAME-OVERs Link mid-compare. The
azahar_repl `-c` invocations are one-shot processes, so a background health-freeze thread there
dies when the process exits. This is a STANDALONE long-running daemon (run backgrounded once) that
continuously re-fills Link's health over the RPC, making him effectively immortal for the duration
of a parity session. Damage still applies for a frame, but health never reaches 0 → no death.

Health (oot3d-decomp docs/ram_map.md, verified): gSaveContext @ 0x00587958, +0x42 = cap (u16),
+0x44 = current (u16). Keeper writes current := cap every tick.

Run:  python3 tools/oracle_keeper.py            # heals forever (~10 Hz), Ctrl-C / kill to stop
      python3 tools/oracle_keeper.py --hz 20    # faster tick
      python3 tools/oracle_keeper.py --daytime 0x6000   # also pin time-of-day each tick
Backgrounded (survives across azahar_repl -c calls):
      setsid nohup python3 tools/oracle_keeper.py >scratch/logs/oracle_keeper.log 2>&1 &
Stop:  the safe-kill skill, or pkill -f oracle_keeper.py (it has no self-match trap on its own name
       only if you avoid pgrep -f from inside; prefer `safekill oracle_keeper.py`).
"""
import os, sys, time, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

OOT3D_TID   = 0x0004000000033500
GSAVECONTEXT = 0x00587958
HP_CAP_OFF  = 0x42   # u16
HP_CUR_OFF  = 0x44   # u16
# time-of-day lives in PlayState.envCtx; we instead pin gSaveContext dayTime if requested.
DAYTIME_OFF = 0x0C   # gSaveContext.dayTime (u16) — see ram_map.md (unverified for write; optional)


def select_oot3d(rpc):
    for pid, tid, _ in rpc.processes():
        if tid == OOT3D_TID:
            rpc.select(pid)
            return pid
    return None


def main():
    hz = 10.0
    daytime = None
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--hz":
            hz = float(args[i + 1]); i += 2
        elif args[i] == "--daytime":
            daytime = int(args[i + 1], 0); i += 2
        else:
            i += 1

    rpc = A.Rpc()
    pid = select_oot3d(rpc)
    if pid is None:
        sys.exit("oracle_keeper: OoT3D process not found on Azahar RPC (:45987)")
    print(f"oracle_keeper: alive-pinning OoT3D pid={pid} at {hz:.0f} Hz"
          + (f", dayTime pinned to {daytime:#06x}" if daytime is not None else ""))
    period = 1.0 / hz
    ticks = 0
    errs = 0
    while True:
        try:
            cap = struct.unpack("<H", rpc.read(GSAVECONTEXT + HP_CAP_OFF, 2))[0]
            cur = struct.unpack("<H", rpc.read(GSAVECONTEXT + HP_CUR_OFF, 2))[0]
            if 0 < cap <= 320 and cur < cap:
                rpc.write(GSAVECONTEXT + HP_CUR_OFF, struct.pack("<H", cap))
            if daytime is not None:
                rpc.write(GSAVECONTEXT + DAYTIME_OFF, struct.pack("<H", daytime & 0xFFFF))
            errs = 0
        except Exception:
            errs += 1
            if errs > 50:
                print("oracle_keeper: too many RPC errors; is Azahar still up? continuing…")
                errs = 0
        ticks += 1
        if ticks % (int(hz) * 30 or 1) == 0:
            print(f"oracle_keeper: {ticks} ticks, last cap={cap if 'cap' in dir() else '?'}")
        time.sleep(period)


if __name__ == "__main__":
    main()
