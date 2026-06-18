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
# The binary is built automatically if missing (the build dir is configured on first
# run too). Force a rebuild with SOH3D_BUILD=1 ./run.sh.
set -eu
REPO="$(cd "$(dirname "$0")" && pwd)"
BUILD="$REPO/Shipwright/build-cmake"
SOH="$BUILD/soh"

# Build a CMake target if its output is missing (or SOH3D_BUILD=1 forces a rebuild).
# Configures the build dir first if it has never been configured. Pass: <target> <output-binary>.
ensure_built() {
    local target="$1" binary="$2"
    if [ -x "$binary" ] && [ -z "${SOH3D_BUILD:-}" ]; then
        return 0
    fi
    if [ ! -f "$BUILD/CMakeCache.txt" ]; then
        echo "configuring build dir (first run)…" >&2
        cmake -S "$REPO/Shipwright" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >&2 || {
            echo "error: cmake configure failed" >&2; exit 1; }
    fi
    echo "building '$target' (this can take a while the first time)…" >&2
    cmake --build "$BUILD" --target "$target" -j"$(nproc)" >&2 || {
        echo "error: build of '$target' failed" >&2; exit 1; }
    [ -x "$binary" ] || { echo "error: '$binary' still missing after build" >&2; exit 1; }
}

# ROM provisioning: env -> gitignored .env -> any *.3ds / *.z64 dropped in the repo dir.
. "$REPO/tools/rom_provision.sh"
soh3d_provision_roms "$REPO" "$SOH"
if [ -z "${SOH3D_3DS_ROM:-}" ]; then
    echo "error: no OoT3D .3ds found — set SOH3D_3DS_ROM, add ./.env, or drop a *.3ds into $REPO" >&2
    exit 1
fi

# If no graphical session is inherited (e.g. launched over SSH/headless terminal),
# fall back to the primary local display so the window actually appears.
if [ -z "${DISPLAY:-}" ]; then
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
    cd "$SOH"
    exec ./charcompare/charcompare "$@"
fi

ensure_built soh "$SOH/soh.elf"

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
exec ./soh.elf "$@"
