# zelda3d_shared — game-agnostic policy shared between OoT (soh) and MM (2s2h) Link
# and (later) other player-adjacent behaviors.
#
# The rule: nothing in this library sees a game-specific type (Player, Actor, PlayState).
# Everything crosses the boundary as plain enums / PODs. Per-game adapters convert their
# native structs to those PODs and call in — one adapter per game, on the game side.
#
# See scratch/plans/mm_oot_link_unify.md for the full staged plan. Stage 2a lands this
# scaffold with the adult mesh-mask policy migrated (link_midmask). Stage 2b/c grow it.

file(GLOB_RECURSE zelda3d_shared_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/player/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/player/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp")

add_library(zelda3d_shared STATIC ${zelda3d_shared_sources})

# Consumers `#include "zelda3d_shared/player/link_gear.h"` etc.
target_include_directories(zelda3d_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# dr_libs: PUBLIC so both games keep resolving <dr_wav.h> for the declarations, while the
# implementation exists only in audio/dr_libs_impl.cpp here. Declared by the root CMakeLists.
target_include_directories(zelda3d_shared PUBLIC ${dr_libs_SOURCE_DIR})

# Upstream third-party code; its warnings are not ours to fix and would drown the build.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/audio/dr_libs_impl.cpp
    PROPERTIES COMPILE_OPTIONS "${WARNING_OVERRIDE}")

set_target_properties(zelda3d_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)
