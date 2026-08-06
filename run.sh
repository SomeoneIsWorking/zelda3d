#!/usr/bin/env bash
# Boot SoH3D into Kakariko Village (OoT3D geometry) on the current desktop.
#
# Run from a terminal in your graphical session (DISPLAY/XAUTHORITY are inherited):
#   ./run.sh              # the game
#
# The decrypted OoT3D .3ds is provided via env ZELDA3D_OOT3D_ROM (never hardcoded — repo
# rule). Either export it, drop a `.env` next to this script setting it, or just drop
# the ROM file (any name) into the repo dir. The N64 .z64 (for first-run extraction)
# is picked up the same way. Override the warp target with ZELDA3D_ENTRANCE=<decimal>.
#
# The engine (Shipwright + libultraship + ZAPDTR + OTRExporter) is vendored directly in this repo
# (the old submodule chain was flattened into a single repo), so `git clone <soh3d> && ./run.sh` is
# all you need — no submodule init/update, no --recursive. run.sh just configures the build dir on
# first run and builds the target.
set -eu
REPO="$(cd "$(dirname "$0")" && pwd)"
BUILD="$REPO/Shipwright/build-cmake"
SOH="$BUILD/soh"
# The soh target's output name is set per-platform in soh/CMakeLists.txt.
case "$(uname)" in
    Darwin) SOH_BIN="soh-macos" ;;
    *)      SOH_BIN="soh.elf" ;;
esac
# Parallel job count — nproc is GNU-only (absent on macOS); fall back to sysctl, then 4.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# The engine (Shipwright + libultraship + ZAPDTR + OTRExporter) is now VENDORED directly into this
# repo as plain directories — the former submodule chain was flattened into a single repo (one
# `main`, one push) to kill the multi-repo friction. So there is no submodule sync/checkout to do:
# the sources are always present in-tree. (History of the old forks remains on their remotes.)

# Bring the engine checkout in line with the commits THIS repo pins (Shipwright gitlink, then the
# libultraship gitlink it records): re-clone a broken/unregistered submodule, otherwise update it
# to its pin. Reports each decision; never hard-resets, so local engine work is preserved.
# The engine sources are vendored in-tree (see above) — just verify they're present.
ensure_sources() {
    if [ -f "$REPO/CMakeLists.txt" ] && [ -f "$REPO/Shipwright/libultraship/CMakeLists.txt" ]; then
        return 0
    fi
    echo "error: engine sources or the root CMakeLists are missing — is the checkout complete?" >&2
    exit 1
}

# Ensure the engine is cloned, updated, configured, and the target is built+current.
# The build step always runs (ninja is a near-instant no-op when nothing changed), so an engine
# update is actually compiled. Pass: <target> <output-binary>.
ensure_built() {
    local target="$1" binary="$2"
    ensure_sources
    # Configure if the build dir has no generator file yet. A leftover CMakeCache.txt from a
    # FAILED earlier configure (e.g. -G Ninja before ninja was installed) counts as not-configured:
    # wipe the stale cache and configure cleanly, otherwise the build step finds no build.ninja.
    if [ ! -f "$BUILD/build.ninja" ]; then
        if [ -f "$BUILD/CMakeCache.txt" ]; then
            echo "build dir half-configured — wiping stale cache and reconfiguring…" >&2
            rm -f "$BUILD/CMakeCache.txt"; rm -rf "$BUILD/CMakeFiles"
        else
            echo "configuring build dir (first run)…" >&2
        fi
        cmake -S "$REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >&2 || {
            echo "error: cmake configure failed" >&2; exit 1; }
    fi
    echo "building '$target' (this can take a while the first time)…" >&2
    cmake --build "$BUILD" --target "$target" -j"$NPROC" >&2 || {
        echo "error: build of '$target' failed" >&2; exit 1; }
    [ -x "$binary" ] || { echo "error: '$binary' still missing after build" >&2; exit 1; }
}

# The engine refuses to start without its runtime asset archive `soh.o2r` (custom fonts, UI
# textures, GL/Metal shaders) sitting next to the binary — on macOS it pops a blocking
# "Missing soh.o2r … Exiting" dialog. That archive is GENERATED from the in-tree custom assets
# (NOT from a game ROM), via the CMake `GenerateSohOtr` target (extract_assets.py --norom), which
# emits it to $BUILD/soh/soh.o2r — exactly where we cd before launching, so the binary finds it
# (Context::LocateFileAcrossAppDirs searches cwd / app dir). We always launch with cwd=$SOH, so
# placing it there satisfies both macOS and Linux. Never commit it: it's a build product.
# Idempotent: skip if already present (the target itself isn't dependency-tracked for staleness,
# so don't rebuild it every run — only when the file is actually missing).
ensure_soh_o2r() {
    [ -f "$SOH/soh.o2r" ] && return 0
    echo "soh.o2r missing — generating runtime asset archive (GenerateSohOtr)…" >&2
    cmake --build "$BUILD" --target GenerateSohOtr -j"$NPROC" >&2 || {
        echo "error: failed to generate soh.o2r (GenerateSohOtr)" >&2; exit 1; }
    [ -f "$SOH/soh.o2r" ] || { echo "error: soh.o2r still missing after GenerateSohOtr" >&2; exit 1; }
}

# ROM provisioning: env -> gitignored .env -> any *.3ds / *.z64 dropped in the repo dir.
. "$REPO/tools/rom_provision.sh"
zelda3d_provision_roms "$REPO" "$SOH"
if [ -z "${ZELDA3D_OOT3D_ROM:-}" ]; then
    echo "error: no OoT3D .3ds found — set ZELDA3D_OOT3D_ROM, add ./.env, or drop a *.3ds into $REPO" >&2
    exit 1
fi

# If no graphical session is inherited (e.g. launched over SSH/headless terminal), fall back
# to the primary local X display. Linux/X11 only — on macOS the window backend is Cocoa and
# forcing DISPLAY=:0 would wrongly push SDL onto an X11 path.
if [ "$(uname)" = "Linux" ] && [ -z "${DISPLAY:-}" ]; then
    export DISPLAY=:0
    [ -z "${XAUTHORITY:-}" ] && for x in /run/user/$(id -u)/xauth_*; do
        [ -f "$x" ] && export XAUTHORITY="$x" && break
    done
fi

ensure_built soh "$SOH/$SOH_BIN"
ensure_soh_o2r              # game hard-exits with a "Missing soh.o2r" dialog without it

export SOH3D=1                                  # render OoT3D assets
# Cold boot to the TITLE SCREEN: a plain `./run.sh` powers on like a real console — title screen,
# file select, New Game, the real intro, normal save + normal day/night clock. The dev-warp shortcuts
# (skip title and teleport straight into a scene, with an optional clean save / pinned time) are
# OPT-IN, off by default — set them on the command line when you want to test a specific scene:
#   ZELDA3D_WARP=1 ZELDA3D_ENTRANCE=177 ZELDA3D_TIME=0x8000 ZELDA3D_COLDBOOT=1 ./run.sh   # warp to Market, noon, fresh save
export ZELDA3D_WARP="${ZELDA3D_WARP:-}"              # set 1 to auto-warp past title/file-select (dev)
export ZELDA3D_COLDBOOT="${ZELDA3D_COLDBOOT:-}"      # set 1 (with WARP) to start the warp save as a clean NEW game
export ZELDA3D_ENTRANCE="${ZELDA3D_ENTRANCE:-219}"  # warp target when ZELDA3D_WARP=1 (219 = Kakariko front gate)
export ZELDA3D_TIME="${ZELDA3D_TIME:-}"             # set e.g. 0x8000 to PIN time-of-day (else the game clock runs)
export ZELDA3D_AUTO="${ZELDA3D_AUTO:-1}"            # 1 = auto-replace non-table actors with OoT3D models
export ZELDA3D_N64ANIM="${ZELDA3D_N64ANIM:-1}"      # drive OoT3D skeletons from live N64 joints (req. for characters)
export ZELDA3D_VULKAN="${ZELDA3D_VULKAN:-1}"         # 1 = single Vulkan Fast3D backend (default); 0 = legacy OpenGL
# Skybox/HUD stripe corruption hunting: launch with ZELDA3D_GL_STATECHECK=1 ./run.sh — every SoH3D
# render pass then logs (stderr) any GL state it failed to restore. Off by default (per-frame glGet).
export ZELDA3D_GL_STATECHECK="${ZELDA3D_GL_STATECHECK:-}"

cd "$SOH"
exec "./$SOH_BIN" "$@"
