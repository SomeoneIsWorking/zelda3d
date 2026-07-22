#!/usr/bin/env bash
# soh3d_harness.sh — drive Azahar's libretro core headlessly for OoT3D.
#
# Configures Azahar/build-harness/ with ENABLE_LIBRETRO=ON and CMake's
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
# loads scratch/title_settled.state, its RE'd title-cs cursor (u32 @
# 0x0054CC3C) is read as the integer lock target, and it HOLDS there while
# SoH3D boots cold through the title cs; the first frame SoH's own cursor
# (Zelda3D_TitleCsFrame()) reaches that value the controller LOCKs and steps
# the oracle per-frame under a 0/1/2-step integer governor that keeps the
# modular cursor delta at 0 (wrap-safe — no content search, no resync; see
# tools/soh3d_harness/title_sync.h for the falsification history). Tools
# that drive their own scene via explicit `loadstate`/`soh_boot`
# (title_ab.py, oracle_cache.py, ...) are unaffected — they never call
# `step`, and title-sync auto-arm is skipped whenever manual
# loadstate/soh_boot already ran before the first `step` anyway. See
# debug_journal/2026-07-14-harness-title-sync.md.
#
# Usage:
#   tools/soh3d_harness.sh                       # rom from $ZELDA3D_OOT3D_ROM
#   tools/soh3d_harness.sh /path/to/oot3d.3ds    # explicit rom
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
azahar_root="${repo_root}/Azahar"
build_dir="${azahar_root}/build-harness"
harness_bin="${build_dir}/bin/Release/soh3d_harness"
wire_in="${repo_root}/tools/soh3d_harness/wire_in.cmake"

mkdir -p "${repo_root}/scratch/harness/system" "${repo_root}/scratch/harness/save" \
         "${repo_root}/scratch/logs"

if [[ ! -f "${build_dir}/build.ninja" ]]; then
    cmake -S "${azahar_root}" -B "${build_dir}" -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_LIBRETRO=ON \
        -DCMAKE_PROJECT_citra_INCLUDE="${wire_in}" \
        -DENABLE_QT=OFF -DENABLE_SDL2=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF \
        -DENABLE_TESTS=OFF -DENABLE_VULKAN=ON -DENABLE_OPENGL=ON \
        -DENABLE_SOFTWARE_RENDERER=ON -DENABLE_LTO=OFF -DUSE_SYSTEM_GLSLANG=ON \
        -DENABLE_BUILTIN_KEYBLOB=ON
fi
ninja -C "${build_dir}" soh3d_harness

# The embedded SoH side (`soh_boot`) resolves its archives next to the binary.
# Link them REPO-RELATIVELY — these used to be hand-made absolute symlinks and
# every one of them dangled the moment the repo was renamed soh3d -> zelda3d.
soh_build="${repo_root}/Shipwright/build-cmake/soh"
for _a in oot.o2r soh.o2r assets; do
    if [[ -e "${soh_build}/${_a}" ]]; then
        ln -sfn "${soh_build}/${_a}" "${build_dir}/bin/Release/${_a}"
    fi
done

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

# Preload a scalable allocator (jemalloc, else tcmalloc). The harness now drives
# Azahar's Vulkan HW renderer (citra_graphics_api=Vulkan, resolution_factor=2 —
# see tools/soh3d_harness/main.cpp + harness_vk.cpp), so the old software-
# rasterizer arena-lock livelock no longer applies on the default path. But this
# preload is kept as a cheap, general safety net: (a) the harness can still fall
# back to the SW rasterizer (g_use_vulkan=false), whose multi-threaded per-
# scanline worker pool allocates on one thread and frees on another — glibc's
# malloc arena lock serializes those cross-thread frees and each frame slows ~10x,
# tripping the 5s watchdog (diagnosed 2026-07-15 via gdb: SwRenderer threads all
# blocked in _int_free_chunk); and (b) SoH's own threaded subsystems benefit too.
# A per-thread-cache allocator removes the contention. Skip if the caller already
# set LD_PRELOAD, or if neither lib is present.
if [ -z "${LD_PRELOAD:-}" ]; then
    for _cand in \
        "$(ldconfig -p 2>/dev/null | grep -m1 'libjemalloc\.so\.2' | awk '{print $NF}')" \
        /usr/lib64/libjemalloc.so.2 /usr/lib/x86_64-linux-gnu/libjemalloc.so.2 \
        "$(ldconfig -p 2>/dev/null | grep -m1 'libtcmalloc_minimal\.so' | awk '{print $NF}')"; do
        if [ -n "${_cand}" ] && [ -e "${_cand}" ]; then
            export LD_PRELOAD="${_cand}"
            echo "harness: LD_PRELOAD=${_cand} (scalable allocator safety net)" >&2
            break
        fi
    done
fi

cd "${repo_root}"
exec "${harness_bin}" "$@"
