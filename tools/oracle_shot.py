#!/usr/bin/env python3
"""oracle_shot.py — capture a VERIFIED gameplay screenshot from the embedded-Azahar oracle.

The visual half of the oracle A/B: drives the embedded harness, so a matched Zelda3D-vs-OoT3D
frame can be produced with nothing else launched.

WHY THIS EXISTS (the bug it fixes): the harness renders real frames — `snapshot <base>` writes
`<base>.az.ppm` from a Vulkan readback of the actual 3DS output — but `OracleSession.boot()` reports
`ok=True` as soon as the core is up, BEFORE the title/file-select has been driven through to
gameplay. Snapshotting there yields OoT3D's title screen, which is a plain sky gradient and is easy
to mistake for "the harness cannot render" — it renders fine. Every capture here is therefore gated
on a POSITIVE gameplay check, and the tool fails loudly rather than writing a title-screen frame.

SOLVED (2026-07-22): two causes, both now fixed at the source rather than here.
  1. `playstate` reports ok AT THE TITLE (it falls back to TITLE_PLAYSTATE_PTR_VA 0x00539F98), so
     every driver that used it as a readiness check accepted the title. The harness now exposes
     `gameplay` -> ok yes|no (gPlayState populated AND not TitleActive) — gate on that.
  2. OoT3D's warp (nextEntranceIndex + transitionTrigger, identical to SoH's) cannot work from the
     title: no save is loaded, so nothing spawns. `warp` used to write into the title PlayState and
     print ok; it now errors. Reaching a loaded save is a ONE-TIME cost — harness_gameplay.boot_to_gameplay
     captures a contract-versioned `scratch/gameplay_settled.<contract>.state` on first run; every
     later session is loadstate+warp
     with no input driving at all.

Usage:
  tools/oracle_shot.py --entrance 0xEE --out scratch/screenshots/oracle_kokiri.png
  tools/oracle_shot.py --entrance 0xEE --settle 240 --out ... --keep-ppm
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)


def _pos_of(h) -> str:
    """Gameplay probe: a non-empty reply proves the Player actor resolved.

    Uses `az_linkanim` — the probe link_sweep drives its whole state matrix with, so a capture
    and a sweep agree on what "in gameplay" means. (`az_playerpos` also resolves correctly once
    boot_to_gameplay has run; it used to answer "no Player actor" only because callers were on
    the title.)
    """
    r = (h.send("az_linkanim") or "").strip()
    return "" if (not r or not r.startswith("ok")) else r


def capture(entrance: int, out_png: str, settle_frames: int, keep_ppm: bool,
            daytime: int = None) -> int:
    from harness_gameplay import boot_to_gameplay, set_time_of_day
    from harness_process import spawn
    import link_sweep as LS  # for SAVE_STATE (the cold-boot title state)

    h = spawn(save_state=LS.SAVE_STATE)
    # Drive to REAL gameplay and warp — one shared, deterministic path
    # (harness_gameplay.boot_to_gameplay: loadstate the cached gameplay state, else
    # capture it once, then warp). `warp` now fails loudly off the title instead
    # of reporting ok, so a title frame can no longer be captured as an oracle
    # screenshot.
    if not boot_to_gameplay(h, entrance=entrance, settle_frames=settle_frames):
        print("oracle_shot: never reached gameplay — refusing to write a "
              "title-screen frame.", file=sys.stderr)
        return 3
    # Time-of-day must match the SoH3D side or a comparison is measuring the clock, not the renderer.
    # Set it AFTER the settle, never before: the clock keeps running, so a request made ahead of
    # several hundred settle frames lands at a completely different sun position. That is exactly how
    # a --daytime 0x6000 capture came out as a low-sun frame and got compared against our midday one
    # (instrument I001, 2026-07-28). set_time_of_day now verifies and raises rather than drifting
    # silently, so a bad clock fails the capture instead of poisoning a measurement.
    if daytime is not None:
        try:
            set_time_of_day(h, daytime)
        except RuntimeError as e:
            print(f"oracle_shot: {e}", file=sys.stderr)
            return 4
    live = _pos_of(h) or "gameplay"

    base = os.path.splitext(out_png)[0]
    # `snapshot` is a STREAMING reply (ok snapshot / az line / soh line / ok end);
    # send() would leave 3 lines in the pipe and desync any command sent after it.
    lines = h.send_multiline(f"snapshot {base}")
    reply = (lines[0] if lines else "").strip()
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
    ap.add_argument("--daytime", type=lambda s: int(s, 0), default=None,
                    help="force gSaveContext dayTime (e.g. 0x6000) so the oracle and "
                         "Zelda3D sides share a clock")
    ap.add_argument("--keep-ppm", action="store_true")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    return capture(a.entrance, a.out, a.settle, a.keep_ppm, a.daytime)


if __name__ == "__main__":
    raise SystemExit(main())
