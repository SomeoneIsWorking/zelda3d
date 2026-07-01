#!/usr/bin/env python3
"""Render smoke test (TDD oracle for the "3DS invisible / 0 draws" bug).

Launches the game headless (direct run on Xvfb :99, the launch path that does not lose the
pre-existing SKYBUG scene-load race), waits for the REPL, asks `geomscan`, and asserts the Zelda3D
3DS-model draw count is at least MIN_DRAWS. The Kokiri Forest entrance (238) populated with
auto-replaced OoT3D models historically submitted ~47 draws; the regression under test reports 0.

Exit 0 = PASS (>= MIN_DRAWS), 1 = FAIL, 2 = harness/launch error.

Usage: tools/render_smoke_test.py [entrance] [time] [min_draws]
"""
import os, re, subprocess, sys, time, signal, pathlib

REPO = pathlib.Path(__file__).resolve().parent.parent
SOH = REPO / "Shipwright/build-cmake/soh/soh.elf"
FIFO = REPO / "scratch/zelda3d.ctl"
REPL = REPO / "tools/zelda3d_repl.py"

entrance = sys.argv[1] if len(sys.argv) > 1 else "238"
daytime = sys.argv[2] if len(sys.argv) > 2 else "0x8000"
min_draws = int(sys.argv[3]) if len(sys.argv) > 3 else 1


def ensure_xvfb():
    if subprocess.run(["xdpyinfo"], env={**os.environ, "DISPLAY": ":99"},
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        subprocess.Popen(["Xvfb", ":99", "-screen", "0", "1920x1080x24"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(2)


def provision_roms():
    """Resolve ZELDA3D_3DS_ROM / ZELDA3D_N64_ROM exactly like tools/zelda3d_game.sh does (env -> .env
    -> drop-in ROM), via the shared rom_provision.sh. Without the 3DS ROM the model provider has
    no OoT3D assets to hand the renderer, so EVERY model fails to load and geomscan reports 0
    draws — i.e. the harness would manufacture the very failure it is meant to detect."""
    sohdir = str(SOH.parent)
    script = (f'. "{REPO}/tools/rom_provision.sh"; '
              f'zelda3d_provision_roms "{REPO}" "{sohdir}"; '
              f'printf "%s\\n%s\\n" "${{ZELDA3D_3DS_ROM:-}}" "${{ZELDA3D_N64_ROM:-}}"')
    out = subprocess.run(["bash", "-c", script], capture_output=True, text=True).stdout.splitlines()
    roms = {}
    if len(out) >= 1 and out[0].strip():
        roms["ZELDA3D_3DS_ROM"] = out[0].strip()
    if len(out) >= 2 and out[1].strip():
        roms["ZELDA3D_N64_ROM"] = out[1].strip()
    return roms


def main():
    ensure_xvfb()
    for f in (FIFO, pathlib.Path(str(FIFO) + ".out")):
        f.unlink(missing_ok=True)
    roms = provision_roms()
    if "ZELDA3D_3DS_ROM" not in roms:
        print("FAIL: no OoT3D .3ds found — set ZELDA3D_3DS_ROM, add ./.env, or drop a *.3ds in the repo "
              "(the renderer cannot load any OoT3D model without it)", file=sys.stderr)
        return 2
    env = {**os.environ, "DISPLAY": ":99", "XAUTHORITY": "/dev/null", "SDL_VIDEODRIVER": "x11",
           "SDL_AUDIODRIVER": "dummy", "SOH3D": "1", "ZELDA3D_WARP": "1", "ZELDA3D_AUTO": "1",
           "ZELDA3D_N64ANIM": "1", "ZELDA3D_ENTRANCE": entrance, "ZELDA3D_TIME": daytime,
           "ZELDA3D_REPL": str(FIFO), **roms}
    env.pop("WAYLAND_DISPLAY", None)
    log = open(REPO / "scratch/logs/render_smoke.log", "w")
    proc = subprocess.Popen([str(SOH)], env=env, stdout=log, stderr=log, start_new_session=True)
    try:
        outp = pathlib.Path(str(FIFO) + ".out")
        for _ in range(40):
            if outp.exists():
                break
            if proc.poll() is not None:
                print(f"FAIL: game exited during boot (rc={proc.returncode})", file=sys.stderr)
                return 2
            time.sleep(1)
        else:
            print("FAIL: REPL never became ready", file=sys.stderr)
            return 2
        # Poll geomscan HARD from the instant the REPL is ready (no warm-up sleep). The OoT3D asset
        # path triggers a pre-existing, timing-dependent SKYBUG crash on the headless software-Vulkan
        # (lavapipe) async submit thread, which kills the process within a second of the first rendered
        # frame. geomscan's data (g_geomLast) is published at BeginPass BEFORE that async GPU submit,
        # so a fast read catches a frame's draw count regardless of the later crash. (SKYBUG is a
        # separate, pre-existing bug — see handoff; not the cause of the 0-draws under test.)
        out = ""
        draws = None
        for _ in range(40):
            if proc.poll() is not None:
                break
            out = subprocess.run([sys.executable, str(REPL), "cmd", "geomscan"],
                                 capture_output=True, text=True, cwd=str(REPO), timeout=3).stdout
            m = re.search(r"\((\d+) Zelda3D draws this frame\)", out)
            if m:
                draws = int(m.group(1))
                break
        if draws is None:
            crashed = proc.poll() is not None
            print(f"FAIL: could not read geomscan ({'process exited' if crashed else 'no parseable output'}):\n{out}",
                  file=sys.stderr)
            return 2
        ok = draws >= min_draws
        print(f"{'PASS' if ok else 'FAIL'}: geomscan Zelda3D draws = {draws} (need >= {min_draws}) "
              f"[entrance={entrance} time={daytime}]")
        return 0 if ok else 1
    finally:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except ProcessLookupError:
            pass


if __name__ == "__main__":
    sys.exit(main())
