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
    "${CMAKE_CURRENT_SOURCE_DIR}/object/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/object/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp")

add_library(zelda3d_shared STATIC ${zelda3d_shared_sources})

# Consumers include RELATIVE TO THIS DIR -- `#include "player/link_gear.h"`, `#include
# "extractor/Extract.h"`. (This comment used to claim a `zelda3d_shared/` prefix, which never
# worked: the include root below is this directory, not its parent.)
target_include_directories(zelda3d_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# dr_libs: PUBLIC so both games keep resolving <dr_wav.h> for the declarations, while the
# implementation exists only in audio/dr_libs_impl.cpp here. Declared by the root CMakeLists.
target_include_directories(zelda3d_shared PUBLIC ${dr_libs_SOURCE_DIR})

# Upstream third-party code; its warnings are not ours to fix and would drown the build.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/audio/dr_libs_impl.cpp
    PROPERTIES COMPILE_OPTIONS "${WARNING_OVERRIDE}")

# object/ — ObjectExtension, which attaches arbitrary typed data to a pointer. It honours this
# library's contract for free: it names no game type at all, keying off `const void*` and storing
# std::any, so it compiles once here rather than once per game. Both games had a copy, and the two
# had drifted: SoH's declared the C entry point under `#else // __cplusplus`, so C++ callers never
# saw `ObjectExtension_Free` declared, and it used `assert` while relying on <libultraship/
# libultraship.h> to drag in <cassert>. MM's copy was correct on both counts and is the one kept.
# Consumers include it as "object/ObjectExtension.h" via the PUBLIC include dir above.

# thirdparty/ adds no sources — it exists so that code BYTE-IDENTICAL in both games has exactly one
# copy. portable-file-dialogs.h (1,770 lines) was vendored twice, and a duplicate is not a neutral
# cost: it is two files to patch, two to audit, and two that can silently drift apart. Consumers
# include it as "thirdparty/portable-file-dialogs.h" via the PUBLIC include dir above.
#
# extractor/ holds Extract.h AND Extract.cpp / FastCrc32C.c, but adds no sources HERE: those two are
# globbed into each game's own target instead, the same mechanism port/ uses. They name no
# game-specific type, so this library's contract would allow them — but they call the game's ZAPD
# entry point and read its gBuildVersion* symbols, which this library does not link. The per-game
# half is a RomVersionTable each game supplies from its own Extractor/RomVersions.cpp. Claim C070.

set_target_properties(zelda3d_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)
