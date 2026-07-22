#!/usr/bin/env python3
"""oracle_shot.py — capture a VERIFIED gameplay screenshot from the embedded-Azahar oracle.

The visual half of the oracle A/B. `tools/parity_ab.py` drives the EXTERNAL Azahar over UDP RPC,
which needs a GUI instance running; this drives the EMBEDDED harness instead, so a matched
Zelda3D-vs-OoT3D frame can be produced with nothing else launched.

WHY THIS EXISTS (the bug it fixes): the harness renders real frames — `snapshot <base>` writes
`<base>.az.ppm` from a Vulkan readback of the actual 3DS output — but `OracleSession.boot()` reports
`ok=True` as soon as the core is up, BEFORE the title/file-select has been driven through to
gameplay. Snapshotting there yields OoT3D's title screen, which is a plain sky gradient and is easy
to mistake for "the harness cannot render" — it renders fine. Every capture here is therefore gated
on a POSITIVE gameplay check, and the tool fails loudly rather than writing a title-screen frame.

STATUS (2026-07-22): the verification half WORKS — it correctly refuses to emit a title frame. The
drive-to-gameplay half does NOT yet: booting standalone, neither `az_playerpos` nor `az_linkanim`
resolves, even though `link_sweep`'s own flow reaches gameplay reliably (it drove all 25 states this
session). So the remaining work is to reuse link_sweep's EXACT working sequence rather than
re-implementing boot+warp here — read how `OracleSession` + `parity_state_sweep.soh_reach` get a live
Link, and call that, instead of hand-rolling taps. Do not conclude the harness cannot render.

Usage:
  tools/oracle_shot.py --entrance 0xEE --out scratch/screenshots/oracle_kokiri.png
  tools/oracle_shot.py --entrance 0xEE --settle 240 --out ... --keep-ppm
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)


def _pos_of(h) -> str:
    """Gameplay probe: a non-empty reply proves the Player actor resolved.

    Uses `az_linkanim`, NOT `az_playerpos`. az_playerpos answers "no Player actor" even when the
    oracle is demonstrably in gameplay (it walks a different anchor and has never resolved in this
    project's runs), whereas az_linkanim is the probe link_sweep drives its whole state matrix with.
    Picking the wrong probe here makes a working oracle look broken.
    """
    r = (h.send("az_linkanim") or "").strip()
    return "" if (not r or not r.startswith("ok")) else r


def capture(entrance: int, out_png: str, settle_frames: int, keep_ppm: bool,
            attempts: int = 3) -> int:
    import harness_ctl as HC
    import link_sweep as LS

    o = LS.OracleSession()
    o.boot()
    if not o.ok:
        print(f"oracle_shot: boot failed: {o.fail_reason}", file=sys.stderr)
        return 2
    h = o.h

    # boot() only guarantees the core is up. Drive to real gameplay and PROVE it: the Player actor
    # must resolve. Without this check a title-screen frame (a sky gradient) captures happily.
    live = ""
    for attempt in range(attempts):
        h.send(f"warp 0x{entrance:x}")
        for _ in range(max(1, settle_frames // 60)):
            h.send("run 60")
        live = _pos_of(h)
        if live:
            break
        # Not in gameplay — the title/file-select is probably still up. Tap through and retry.
        for btn in ([HC.BTN_START] * 3) + ([HC.BTN_A] * 20):
            HC.tap(h, btn, hold=30, release=60)
            if HC.poll_playstate(h):
                break
    if not live:
        print("oracle_shot: never reached gameplay (probe never resolved) — refusing to write a "
              "title-screen frame. See STATUS in this file's docstring: reuse link_sweep's drive "
              "sequence rather than the tap loop below.", file=sys.stderr)
        return 3

    base = os.path.splitext(out_png)[0]
    reply = (h.send(f"snapshot {base}") or "").strip()
    ppm = base + ".az.ppm"
    if not reply.startswith("ok") or not os.path.exists(ppm):
        print(f"oracle_shot: snapshot failed: {reply}", file=sys.stderr)
        return 4

    try:
        from PIL import Image
        Image.open(ppm).convert("RGB").save(out_png)
    except Exception as e:  # Pillow absent or decode failure — the ppm is still usable
        print(f"oracle_shot: wrote {ppm} (PNG convert skipped: {e})")
        return 0
    if not keep_ppm:
        for p in (ppm, base + ".soh.ppm"):
            if os.path.exists(p):
                os.remove(p)
    print(f"oracle_shot: {out_png}  [verified gameplay: {live}]")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--entrance", type=lambda s: int(s, 0), default=0xEE,
                    help="OoT3D/SoH entrance index (default 0xEE = Kokiri Forest)")
    ap.add_argument("--out", default=os.path.join(REPO, "scratch", "screenshots", "oracle.png"))
    ap.add_argument("--settle", type=int, default=180, help="frames to run after warping")
    ap.add_argument("--keep-ppm", action="store_true")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    return capture(a.entrance, a.out, a.settle, a.keep_ppm)


if __name__ == "__main__":
    raise SystemExit(main())
