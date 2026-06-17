#!/usr/bin/env bash
# Boot SoH3D into Kakariko Village (OoT3D geometry) on the current desktop.
#
# Run from a terminal in your graphical session (DISPLAY/XAUTHORITY are inherited):
#   ./run.sh              # the game
#   ./run.sh tool         # the N64-vs-3DS character compare tool (charcompare)
#   ./run.sh tool /actor/zelda_zl4.zar   # ...starting on a specific character
#
# The decrypted OoT3D .3ds is provided via env SOH3D_3DS_ROM (never hardcoded — repo
# rule). Either export it, drop a `.env` next to this script setting it, or drop the
# ROM in as ./oot3d.3ds. Override the warp target with SOH3D_ENTRANCE=<decimal>.
set -eu
REPO="$(cd "$(dirname "$0")" && pwd)"
SOH="$REPO/Shipwright/build-cmake/soh"

# ROM provisioning: env -> gitignored .env -> drop-in ./oot3d.3ds
[ -f "$REPO/.env" ] && . "$REPO/.env"
if [ -z "${SOH3D_3DS_ROM:-}" ] && [ -f "$REPO/oot3d.3ds" ]; then
    SOH3D_3DS_ROM="$REPO/oot3d.3ds"
fi
if [ -z "${SOH3D_3DS_ROM:-}" ]; then
    echo "error: set SOH3D_3DS_ROM to your decrypted OoT3D .3ds (or add ./.env or ./oot3d.3ds)" >&2
    exit 1
fi
export SOH3D_3DS_ROM

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
    if [ ! -x "$CC" ]; then
        echo "error: $CC not built — run: cmake --build Shipwright/build-cmake --target charcompare" >&2
        exit 1
    fi
    cd "$SOH"
    exec ./charcompare/charcompare "$@"
fi

if [ ! -x "$SOH/soh.elf" ]; then
    echo "error: $SOH/soh.elf not built — run: cmake --build Shipwright/build-cmake --target soh" >&2
    exit 1
fi

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
