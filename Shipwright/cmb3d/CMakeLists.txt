# cmb3d — the shared OoT3D/MM3D (3DS "engine") asset layer.
#
# Game-agnostic parsers for the Nintendo 3DS Zelda asset formats (CMB models, CSAB
# animations, ZAR/ZSI archives+scenes, CTR ROM/RomFS, CTXB/PICA textures, ZCOL
# collision), plus the CityHash + hi-res texture-pack helpers. Both games link this:
#   - soh  (OoT)  substitutes OoT3D models for N64 OoT
#   - mm   (MM)   substitutes MM3D  models for N64 MM
# so it lives in ONE place instead of being duplicated per game. It has NO dependency
# on either decomp — only the C++ stdlib and stb_image (via the shared `stb` target).
#
# Consumers keep the historical `#include "asset/<fmt>.h"` spelling; the PUBLIC include
# dir below (the parent of asset/) makes that resolve without touching a single caller.

file(GLOB_RECURSE cmb3d_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/asset/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/asset/*.h")

add_library(cmb3d STATIC ${cmb3d_sources})

target_include_directories(cmb3d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# texpack.cpp decodes PNGs via stb_image; `stb` (defined in libultraship) carries the
# header on its PUBLIC include interface.
target_link_libraries(cmb3d PRIVATE stb)

set_target_properties(cmb3d PROPERTIES POSITION_INDEPENDENT_CODE ON)
