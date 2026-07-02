#!/usr/bin/env bash
# soh3d_harness.sh — drive Azahar's libretro core headlessly for OoT3D.
#
# Configures Azahar/build-libretro/ with ENABLE_LIBRETRO=ON and CMake's
# CMAKE_PROJECT_citra_INCLUDE hook pointing at tools/soh3d_harness/wire_in.cmake,
# which registers the soh3d_harness target inside Azahar's build without
# needing to touch the vendored Azahar/src/CMakeLists.txt. Builds and runs.
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

mkdir -p "${repo_root}/scratch/harness/system" "${repo_root}/scratch/harness/save"

if [[ ! -f "${build_dir}/build.ninja" ]]; then
    cmake -S "${azahar_root}" -B "${build_dir}" -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_LIBRETRO=ON \
        -DCMAKE_PROJECT_citra_INCLUDE="${wire_in}"
fi
ninja -C "${build_dir}" soh3d_harness

cd "${repo_root}"
exec "${harness_bin}" "$@"
