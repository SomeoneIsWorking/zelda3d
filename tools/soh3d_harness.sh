#!/usr/bin/env bash
# soh3d_harness.sh — drive Azahar's libretro core headlessly for OoT3D.
#
# Configures Azahar/build-libretro/ with ENABLE_LIBRETRO=ON and CMake's
# CMAKE_PROJECT_citra_INCLUDE hook pointing at tools/soh3d_harness/wire_in.cmake,
# which registers the soh3d_harness target inside Azahar's build without
# needing to touch the vendored Azahar/src/CMakeLists.txt. Builds and runs.
#
# Headless by default: the harness's own SBS window is disabled
# (SOH3D_HARNESS_HEADLESS=1) AND we run on a private Xvfb display so the
# embedded SoH3D's SDL3-GPU Vulkan window (which does NOT honor libultraship's
# SOH_HEADLESS knob — see tools/zelda3d_game.sh's setup_headless comment) also
# stays off the user's Wayland desktop. Override with ZELDA3D_HEADLESS=0 to get
# the harness's SBS window on :0 (debugging only).
#
# Usage:
#   tools/soh3d_harness.sh                       # rom from $ZELDA3D_OOT3D_ROM
#   tools/soh3d_harness.sh /path/to/oot3d.3ds    # explicit rom
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
azahar_root="${repo_root}/Azahar"
build_dir="${azahar_root}/build-libretro"
harness_bin="${build_dir}/bin/Release/soh3d_harness"
wire_in="${repo_root}/tools/soh3d_harness/wire_in.cmake"

mkdir -p "${repo_root}/scratch/harness/system" "${repo_root}/scratch/harness/save" \
         "${repo_root}/scratch/logs"

if [[ ! -f "${build_dir}/build.ninja" ]]; then
    cmake -S "${azahar_root}" -B "${build_dir}" -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_LIBRETRO=ON \
        -DCMAKE_PROJECT_citra_INCLUDE="${wire_in}"
fi
ninja -C "${build_dir}" soh3d_harness

# Headless: run on a private Xvfb display so neither the harness's SBS window
# NOR the embedded SoH3D's Vulkan window shows up on the user's Wayland
# desktop. This mirrors tools/zelda3d_game.sh's setup_headless — required on a
# Wayland host because SDL3-GPU's window creation ignores SOH_HEADLESS.
setup_headless() {
    [ "${ZELDA3D_HEADLESS:-1}" = "1" ] || return 0
    local disp="${ZELDA3D_HEADLESS_DISPLAY:-:99}"
    if ! DISPLAY="$disp" xdpyinfo >/dev/null 2>&1; then
        echo "headless: starting Xvfb on $disp" >&2
        setsid Xvfb "$disp" -screen 0 1920x1080x24 \
            >"${repo_root}/scratch/logs/xvfb_harness.log" 2>&1 &
        local up=
        for _ in $(seq 1 20); do
            DISPLAY="$disp" xdpyinfo >/dev/null 2>&1 && { up=1; break; }
            sleep 0.5
        done
        [ -n "$up" ] || { echo "headless: Xvfb failed on $disp" >&2; return 1; }
    fi
    export DISPLAY="$disp" XAUTHORITY=/dev/null SDL_VIDEODRIVER=x11
    export SDL_AUDIODRIVER=dummy
    unset WAYLAND_DISPLAY
    # Disable the harness's own SBS compositor window (separate from the
    # embedded SoH3D window handled by the Xvfb above) — we only ever read
    # frames via the snapshot command, never by looking at the SBS window.
    export SOH3D_HARNESS_HEADLESS=1
    echo "headless: on $disp (Xvfb, SDL x11, audio=dummy, SBS window off)" >&2
}
setup_headless

cd "${repo_root}"
exec "${harness_bin}" "$@"
