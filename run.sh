#!/usr/bin/env bash
# Boot SoH3D into Kakariko Village (OoT3D geometry) on the current desktop.
#
# Run from a terminal in your graphical session (DISPLAY/XAUTHORITY are inherited):
#   ./run.sh              # the game
#   ./run.sh tool         # the N64-vs-3DS character compare tool (charcompare)
#   ./run.sh tool /actor/zelda_zl4.zar   # ...starting on a specific character
#
# The decrypted OoT3D .3ds is provided via env SOH3D_3DS_ROM (never hardcoded — repo
# rule). Either export it, drop a `.env` next to this script setting it, or just drop
# the ROM file (any name) into the repo dir. The N64 .z64 (for first-run extraction)
# is picked up the same way. Override the warp target with SOH3D_ENTRANCE=<decimal>.
#
# On a fresh machine this clones the Shipwright engine fork + its submodules and configures the
# build dir. On every run it fast-forwards the engine to the latest fork commit (only if the
# clone is clean with no local commits — never clobbers local work) and rebuilds if anything
# changed, so `git pull && ./run.sh` stays current. Skip the engine update with SOH3D_NOUPDATE=1.
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

# The engine lives in a side-by-side clone of our Shipwright fork (gitignored here, NOT a
# submodule of this repo), with libultraship as a submodule pointing at OUR fork's commit.
SHIPWRIGHT_FORK="https://github.com/SomeoneIsWorking/Shipwright.git"
SHIPWRIGHT_BRANCH="develop"
LIBULTRA_FORK="https://github.com/SomeoneIsWorking/libultraship.git"

# Fast-forward the engine clone to the latest fork commit, but ONLY when it's a clean consumer
# clone — never disturb a machine with local engine work (mine). Skips if: SOH3D_NOUPDATE set,
# working tree dirty, local commits not on the remote, or offline. When it does fast-forward, it
# re-syncs the submodules to the new gitlink. Safe-by-default: any doubt -> leave the clone alone.
update_engine_if_safe() {
    local sw="$REPO/Shipwright"
    [ -n "${SOH3D_NOUPDATE:-}" ] && return 0
    git -C "$sw" diff --quiet 2>/dev/null && git -C "$sw" diff --cached --quiet 2>/dev/null || return 0
    local up
    up="$(git -C "$sw" rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null)" || return 0
    git -C "$sw" fetch --quiet "${up%%/*}" 2>/dev/null || return 0          # offline -> skip
    [ "$(git -C "$sw" rev-list --count '@{upstream}..HEAD' 2>/dev/null)" = 0 ] || return 0  # ahead -> skip
    [ "$(git -C "$sw" rev-list --count 'HEAD..@{upstream}' 2>/dev/null)" != 0 ] || return 0 # up to date
    echo "updating engine to latest fork commit…" >&2
    git -C "$sw" merge --ff-only '@{upstream}' >&2 || return 0
    git -C "$sw" config submodule.libultraship.url "$LIBULTRA_FORK"
    git -C "$sw" submodule update --init --recursive >&2 || { echo "error: submodule update failed" >&2; exit 1; }
}

# Clone the Shipwright fork + init its submodules if the engine sources aren't present.
# A plain submodule init won't do: the recorded libultraship commit only exists in our fork,
# while Shipwright's .gitmodules still points libultraship at upstream — so override that URL.
ensure_sources() {
    if [ -f "$REPO/Shipwright/CMakeLists.txt" ] && [ -f "$REPO/Shipwright/libultraship/CMakeLists.txt" ]; then
        update_engine_if_safe
        return 0
    fi
    if [ -e "$REPO/Shipwright" ] && [ ! -d "$REPO/Shipwright/.git" ]; then
        echo "error: $REPO/Shipwright exists but isn't a git clone — move it aside and retry" >&2
        exit 1
    fi
    if [ ! -d "$REPO/Shipwright/.git" ]; then
        # Clone into a temp dir and move into place, so an interrupted clone never lands a
        # broken half-checkout at Shipwright/ that later runs would mistake for a real clone.
        echo "cloning Shipwright fork ($SHIPWRIGHT_BRANCH)…" >&2
        local tmp="$REPO/Shipwright.cloning.$$"
        rm -rf "$tmp"
        git clone -o fork --branch "$SHIPWRIGHT_BRANCH" "$SHIPWRIGHT_FORK" "$tmp" >&2 || {
            rm -rf "$tmp"; echo "error: failed to clone Shipwright" >&2; exit 1; }
        mv "$tmp" "$REPO/Shipwright"
    fi
    echo "initialising engine submodules (libultraship from our fork)…" >&2
    git -C "$REPO/Shipwright" submodule init >&2 || { echo "error: submodule init failed" >&2; exit 1; }
    git -C "$REPO/Shipwright" config submodule.libultraship.url "$LIBULTRA_FORK"
    git -C "$REPO/Shipwright" submodule update --init --recursive >&2 || {
        echo "error: submodule update failed" >&2; exit 1; }
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
        cmake -S "$REPO/Shipwright" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >&2 || {
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
soh3d_provision_roms "$REPO" "$SOH"
if [ -z "${SOH3D_3DS_ROM:-}" ]; then
    echo "error: no OoT3D .3ds found — set SOH3D_3DS_ROM, add ./.env, or drop a *.3ds into $REPO" >&2
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

# Subcommand: `./run.sh tool [args]` -> the charcompare character-compare tool.
if [ "${1:-}" = "tool" ]; then
    shift
    CC="$SOH/charcompare/charcompare"
    ensure_built charcompare "$CC"
    ensure_soh_o2r          # tool warns (GUI font / GL shaders) without it
    cd "$SOH"
    exec ./charcompare/charcompare "$@"
fi

ensure_built soh "$SOH/$SOH_BIN"
ensure_soh_o2r              # game hard-exits with a "Missing soh.o2r" dialog without it

export SOH3D=1                                  # render OoT3D assets
export SOH3D_WARP=1                             # auto-warp past title/file-select
export SOH3D_ENTRANCE="${SOH3D_ENTRANCE:-219}"  # 219 = Kakariko Village front gate
export SOH3D_TIME="${SOH3D_TIME:-0x8000}"       # 0x8000 = noon (force DAY; Kakariko else loads at night)
export SOH3D_AUTO="${SOH3D_AUTO:-1}"            # 1 = auto-replace non-table actors with OoT3D models
export SOH3D_N64ANIM="${SOH3D_N64ANIM:-1}"      # drive OoT3D skeletons from live N64 joints (req. for characters)
# Skybox/HUD stripe corruption hunting: launch with SOH3D_GL_STATECHECK=1 ./run.sh — every SoH3D
# render pass then logs (stderr) any GL state it failed to restore. Off by default (per-frame glGet).
export SOH3D_GL_STATECHECK="${SOH3D_GL_STATECHECK:-}"

cd "$SOH"
exec "./$SOH_BIN" "$@"
