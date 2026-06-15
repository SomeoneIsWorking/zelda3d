#!/usr/bin/env bash
# Installs the EXTRA dependencies needed to build Azahar (3DS emulator oracle)
# on top of what install_deps.sh already provides.
# Run with: sudo ./scripts/install_azahar_deps.sh
#
# Azahar's only desktop frontend is Qt6 (it has no standalone SDL frontend), so
# Qt6 is required. Almost everything else (Boost, SoundTouch, SDL2, fmt,
# cryptopp, glslang, SPIRV, dynarmic, ...) is vendored in Azahar/externals/.
set -euo pipefail
if [[ $EUID -ne 0 ]]; then echo "Re-run with sudo:  sudo $0" >&2; exit 1; fi

AZAHAR_DEPS=(
  # Qt6 frontend (components: Core Gui Widgets Multimedia Concurrent DBus GuiPrivate)
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtmultimedia-devel
  # Renderer / window-system bits used by the GL & Vulkan backends
  vulkan-loader-devel vulkan-headers mesa-libGL-devel
)

echo "==> Installing Azahar (Qt6) build dependencies..."
dnf install -y --skip-unavailable "${AZAHAR_DEPS[@]}"
echo "==> Done."
