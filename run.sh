#!/usr/bin/env bash
# Boot SoH3D into Kakariko Village (OoT3D geometry) on the current desktop.
#
# Run from a terminal in your graphical session (DISPLAY/XAUTHORITY are inherited):
#   ./run.sh
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

if [ ! -x "$SOH/soh.elf" ]; then
    echo "error: $SOH/soh.elf not built — run: cmake --build Shipwright/build-cmake --target soh" >&2
    exit 1
fi

export SOH3D=1                                  # render OoT3D assets
export SOH3D_WARP=1                             # auto-warp past title/file-select
export SOH3D_ENTRANCE="${SOH3D_ENTRANCE:-219}"  # 219 = Kakariko Village front gate

cd "$SOH"
exec ./soh.elf "$@"
