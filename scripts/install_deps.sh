#!/usr/bin/env bash
# Installs build dependencies for the SoH3D project on Fedora.
# Run with: sudo ./scripts/install_deps.sh
# (You said no unattended sudo -- run this yourself.)
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "Re-run with sudo:  sudo $0" >&2
  exit 1
fi

# Ship of Harkinian build deps (from Shipwright/docs/BUILDING.md, Fedora section).
# NOTE: corrected for Fedora 44, which renamed two packages:
#   SDL2-devel        -> sdl2-compat-devel  (SDL2 API implemented on SDL3)
#   nlohmann-json-devel -> json-devel
SOH_DEPS=(
  gcc gcc-c++ git cmake ninja-build lsb_release
  sdl2-compat-devel SDL2_net-devel libpng-devel
  libzip-devel libzip-tools json-devel
  tinyxml2-devel spdlog-devel opusfile-devel libvorbis-devel
)

# Helpful extras for our 3DS asset tooling / oracle work.
EXTRA_DEPS=(
  python3 python3-pip python3-virtualenv
  mesa-libGL mesa-dri-drivers   # for any GL-based offscreen rendering
)

echo "==> Installing SoH build dependencies..."
dnf install -y --skip-unavailable "${SOH_DEPS[@]}" "${EXTRA_DEPS[@]}"

echo
echo "==> Done. Verifying key tools:"
for b in gcc g++ cmake ninja git; do
  printf '  %-8s ' "$b"; command -v "$b" || echo MISSING
done
echo
echo "Note: Azahar (3DS emulator, for the oracle) is NOT in Fedora repos."
echo "Grab a prebuilt AppImage from https://azahar-emu.org/ and drop it in 3ds/."
