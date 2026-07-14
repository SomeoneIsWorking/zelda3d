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
# Title-cs frame sync (DEFAULT, no flag needed): the harness's `step <N>`
# REPL command (the combined Az+SoH driver used for live/SBS viewing) auto-
# arms TitleSyncController on its first call in a fresh process — the oracle
# loads scratch/title_settled.state and HOLDS there while SoH3D boots cold
# through the title cs; once SoH's raw frame count passes 408, a native
# content search (grayscale structure match, same formula as
# tools/title_ab.py) locks the oracle onto SoH's current frame, then 1:1
# stepping keeps both halves showing the same title-cs instant —
# recalibrating via the same search on every title-cs loop wrap (1:1
# stepping alone was measured to drift over a full ~2400-frame loop, see
# tools/soh3d_harness/title_sync.h). Tools that drive their own scene via
# explicit `loadstate`/`soh_boot` (title_ab.py, oracle_cache.py, ...) are
# unaffected — they never call `step`, and title-sync auto-arm is skipped
# whenever manual loadstate/soh_boot already ran before the first `step`
# anyway. See debug_journal/2026-07-14-harness-title-sync.md.
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
