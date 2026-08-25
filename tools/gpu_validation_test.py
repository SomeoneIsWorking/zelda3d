#!/usr/bin/env python3
"""GPU regression test for the SDL3-GPU skinned-draw crash (TDD oracle).

Root cause this guards against: the Zelda3D skinned-model vertex shader
(`zelda3d_sdl3gpu_shaders.cpp` `kVertTemplate`)
declares TWO vertex uniform buffers — set=1 binding=0 `UBO` (common) and set=1 binding=1 `UBOBones`
(bone matrices) — but the shader was created with `num_uniform_buffers = 1`. The pipeline layout then
had no descriptor slot for `bones`, so every skinned draw bound an unbacked descriptor. At
draw-execution time the GPU dereferences it — a stale read that lavapipe-serial tolerates but
multi-threaded lavapipe (headless crash) and MoltenVK (the macOS BindFragmentSamplers crash) fault on.
The Vulkan validation layer flags the cause deterministically:
    VUID-VkGraphicsPipelineCreateInfo-layout-07988   (pipeline layout inconsistent with shader stages)
    VUID-vkCmdDraw-None-08114  "[Set 1, Binding 1, variable \"bones\"] is invalid"

Two phases, run against Kokiri Forest (entrance 238 — Saria/Mido/Kokiri are skinned actors):

  PHASE 1 (hard gate, default renderer): launch, run several seconds, assert the process did NOT crash
  AND geomscan still reports Zelda3D draws. Before the fix the skinned draw faulted the GPU within ~1s of
  the first frame; after, the scene renders ~47 draws and stays up. This is the deterministic, reliable
  crash-regression signal (the default multi-threaded software-Vulkan path is where the race bites).

  PHASE 2 (ZELDA3D_SDL3GPU_DEBUG=1 -> Vulkan validation layer): scan for the exact descriptor/
  pipeline-layout VUIDs above — the precise oracle that names the bug even on a driver that wouldn't
  crash. Validation runs at command-RECORD time, independent of GPU execution threading, so this phase
  forces serial software rasterization (LP_NUM_THREADS=0 on lavapipe) to take the async multi-thread
  execution race out of the picture and read the validation result cleanly. If captured -> FAIL. Only
  skipped (INCONCLUSIVE) when the Vulkan validation layer is genuinely not installed.

Exit 0 = PASS, 1 = regression (crash / lost draws / validation error), 2 = harness/launch error.

Usage: tools/gpu_validation_test.py [entrance] [time]
"""
import os, re, subprocess, sys, time, signal, pathlib

from rom_provision import provision_n64_extraction_rom, resolve_rom_environment

REPO = pathlib.Path(__file__).resolve().parent.parent
# ONE program binary now runs both games; this test always runs OoT (`zelda3d oot`). SOH_DIR is
# the OoT core's own asset directory -- the launcher resolves it as its build-tree sibling, same as
# tools/zelda3d_game.py, so ROM provisioning (done client-side here, not by the launcher) targets
# the right place. We still cd there before exec, matching the other launch scripts' convention.
ZELDA3D_BIN = pathlib.Path(os.environ.get("ZELDA3D_SOH", str(REPO / "Shipwright/build-cmake/zelda3d/zelda3d")))
SOH_DIR = REPO / "Shipwright/build-cmake/soh"
FIFO = REPO / "scratch/zelda3d.ctl"
REPL = REPO / "tools/zelda3d_repl.py"
LOGDIR = REPO / "scratch/logs"

# Validation messages that ARE the bug-under-test (and its descriptor/layout kin). Substring match.
FATAL_VALIDATION = [
    "VUID-VkGraphicsPipelineCreateInfo-layout-07988",  # pipeline layout vs shader stages mismatch
    "VUID-vkCmdDraw-None-08114",                        # bound descriptor set is invalid
    "is invalid.",                                      # "[Set N, Binding M, variable ...] is invalid"
    "VUID-vkCmdDrawIndexed-None-08114",
]
# Benign validation noise we ignore. 08737: glslang emits SPIR-V 1.3 while the device targets Vulkan
# 1.0 semantics — cosmetic, shaders run fine. Its presence also proves the validation layer is active.
BENIGN_VALIDATION_MARKER = "VUID-VkShaderModuleCreateInfo-pCode-08737"
CRASH_RE = re.compile(r"\[critical\] Signal:|received signal SIG|FATAL signal")


def ensure_xvfb():
    if subprocess.run(["xdpyinfo"], env={**os.environ, "DISPLAY": ":99"},
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        subprocess.Popen(["Xvfb", ":99", "-screen", "0", "1920x1080x24"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(2)


def provision_roms():
    """Resolve ROMs through the shipping Python policy used by every launcher."""
    environment = resolve_rom_environment(REPO, os.environ)
    provision_n64_extraction_rom(SOH_DIR, environment)
    return {
        name: environment[name]
        for name in ("ZELDA3D_OOT3D_ROM", "ZELDA3D_OOT_ROM")
        if environment.get(name)
    }


def read_geomscan():
    try:
        out = subprocess.run([sys.executable, str(REPL), "cmd", "geomscan"],
                             capture_output=True, text=True, cwd=str(REPO), timeout=3).stdout
    except subprocess.TimeoutExpired:
        return None
    m = re.search(r"\((\d+) Zelda3D draws this frame\)", out)
    return int(m.group(1)) if m else None


def launch(roms, extra_env, logname, entrance, daytime):
    for f in (FIFO, pathlib.Path(str(FIFO) + ".out")):
        f.unlink(missing_ok=True)
    env = {**os.environ, "DISPLAY": ":99", "XAUTHORITY": "/dev/null", "SDL_VIDEODRIVER": "x11",
           "SDL_AUDIODRIVER": "dummy", "SOH3D": "1", "ZELDA3D_WARP": "1", "ZELDA3D_AUTO": "1",
           "ZELDA3D_N64ANIM": "1", "ZELDA3D_ENTRANCE": entrance, "ZELDA3D_TIME": daytime,
           "ZELDA3D_REPL": str(FIFO), **roms, **extra_env}
    env.pop("WAYLAND_DISPLAY", None)
    LOGDIR.mkdir(parents=True, exist_ok=True)
    log = open(LOGDIR / logname, "w")
    proc = subprocess.Popen([str(ZELDA3D_BIN), "oot"], env=env, stdout=log, stderr=log,
                            cwd=str(SOH_DIR), start_new_session=True)
    return proc, log


def wait_repl(proc, timeout=60):
    outp = pathlib.Path(str(FIFO) + ".out")
    for _ in range(timeout):
        if outp.exists():
            return True
        if proc.poll() is not None:
            return False
        time.sleep(1)
    return False


def kill(proc):
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except ProcessLookupError:
        pass


def phase1_stability(roms, entrance, daytime):
    """Hard gate: default renderer must survive sustained skinned-model rendering and keep drawing."""
    proc, log = launch(
        roms, {}, "gpu_regression_phase1.log", entrance, daytime
    )
    draws = 0
    try:
        if not wait_repl(proc):
            log.close()
            return 2, "FAIL: game exited during boot (phase 1) — see scratch/logs/gpu_regression_phase1.log"
        # Render for ~8s; the skinned-draw fault hit within ~1s of the first frame pre-fix.
        deadline = 8
        for _ in range(deadline):
            if proc.poll() is not None:
                break
            d = read_geomscan()
            if d is not None:
                draws = max(draws, d)
            time.sleep(1)
        alive = proc.poll() is None
        final = read_geomscan() if alive else None
    finally:
        kill(proc)
        log.close()
    text = (LOGDIR / "gpu_regression_phase1.log").read_text(errors="replace")
    if CRASH_RE.search(text) or not alive:
        return 1, "FAIL (phase 1): renderer CRASHED during skinned-model rendering — see " \
                  "scratch/logs/gpu_regression_phase1.log"
    if draws <= 0:
        return 2, f"FAIL (phase 1): no Zelda3D draws ever recorded (max={draws}); scene never populated, " \
                  "test is vacuous — see scratch/logs/gpu_regression_phase1.log"
    if final is not None and final <= 0:
        return 1, f"FAIL (phase 1): Zelda3D draws dropped to 0 after rendering (peaked at {draws}) — the " \
                  "draw path stopped, possible silent GPU failure."
    return 0, f"phase 1 OK: no crash, {draws} Zelda3D draws sustained"


def phase2_validation(roms, entrance, daytime):
    """Vulkan validation layer names the descriptor/layout bug explicitly. Forces serial software
    rasterization (LP_NUM_THREADS=0) so the async multi-thread execution race can't crash the run
    before validation (a record-time check) is read. Returns (fail: bool, message)."""
    proc, log = launch(
        roms,
        {"ZELDA3D_SDL3GPU_DEBUG": "1", "LP_NUM_THREADS": "0"},
        "gpu_regression_phase2.log",
        entrance,
        daytime,
    )
    draws = 0
    try:
        if wait_repl(proc):
            for _ in range(40):
                if proc.poll() is not None:
                    break
                d = read_geomscan()
                if d is not None:
                    draws = max(draws, d)
                    if draws > 0:
                        break
                time.sleep(1)
            time.sleep(1)
    finally:
        kill(proc)
        log.close()
    text = (LOGDIR / "gpu_regression_phase2.log").read_text(errors="replace")
    hits = sorted({ln.strip() for ln in text.splitlines() for p in FATAL_VALIDATION if p in ln})
    if hits:
        msg = "FAIL (phase 2): GPU validation flagged the skinned-draw descriptor/layout bug:\n  " + \
              "\n  ".join(hits[:6])
        return True, msg
    if BENIGN_VALIDATION_MARKER not in text:
        # The validation layer never ran (not installed on this host) — can't assert anything from it.
        # This is a genuine "oracle unavailable", not an accepted in-game fault.
        return False, "phase 2 SKIPPED: Vulkan validation layer not installed on this host"
    if draws <= 0:
        return False, "phase 2 SKIPPED: validation layer active but the scene never reported draws " \
                      f"(geomscan={draws}); inconclusive — phase 1 already gated stability"
    return False, f"phase 2 OK: validation clean across {draws} draws"


def main(arguments=None):
    args = list(sys.argv[1:] if arguments is None else arguments)
    if len(args) > 2:
        raise ValueError("usage: gpu_validation_test.py [entrance] [time]")
    entrance = args[0] if args else "238"
    daytime = args[1] if len(args) > 1 else "0x8000"
    ensure_xvfb()
    roms = provision_roms()
    if "ZELDA3D_OOT3D_ROM" not in roms:
        print("FAIL: no OoT3D .3ds found — set ZELDA3D_OOT3D_ROM, add ./.env, or drop a *.3ds in the repo",
              file=sys.stderr)
        return 2

    code, msg = phase1_stability(roms, entrance, daytime)
    print(msg, file=sys.stderr if code else sys.stdout)
    if code:
        return code

    fail, vmsg = phase2_validation(roms, entrance, daytime)
    print(vmsg, file=sys.stderr if fail else sys.stdout)
    if fail:
        return 1

    print(f"PASS: skinned-draw GPU regression test [entrance={entrance} time={daytime}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
