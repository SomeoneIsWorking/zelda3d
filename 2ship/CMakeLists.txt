# Majora's Mask game module for Zelda3D / zelda3d.
#
# Second executable in the monorepo (sibling to `soh`), linking the SAME in-tree
# `libultraship` STATIC lib. Mirrors soh/CMakeLists.txt but with the MM decomp layout:
#   mm/2s2h  = the port shell (BenPort/GameInteractor/Enhancements/BenGui/... — analogous to soh/soh)
#   mm/src   = the MM decomp game core (analogous to soh/src)
# Globs + libultra prune follow 2 Ship 2 Harkinian's mm build (exclude ALL src/libultra; the PC
# replacements mixer.c / framebuffer_effects.c live in 2s2h, and gu_pc.c is shared with OoT from
# Shipwright/zelda3d_shared/port/). SDL3 + link
# environment follow zelda3d. Windows/Switch/CafeOS branches are intentionally dropped — this
# is a PC (Linux/macOS) port.

cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

project(mm LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_C_STANDARD 23 CACHE STRING "The C standard to use")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJCXX)
    set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

set(BUILD_SHARED_LIBS OFF CACHE STRING "install/link shared instead of static libs")

include(FetchContent)

################################################################################
# Where the engine lives.
#
# The root CMakeLists (../CMakeLists.txt) sets these; the fallbacks are for configuring this game
# standalone. They exist because MM used to locate the engine's assets as
# "${CMAKE_SOURCE_DIR}/libultraship/assets" — which only resolved while OoT's directory happened to
# be the configure root, i.e. MM silently depended on being built as OoT's guest.
################################################################################
if (NOT DEFINED ZELDA3D_ENGINE_DIR)
    set(ZELDA3D_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shipwright/libultraship")
endif()
if (NOT DEFINED ZELDA3D_ENGINE_ASSETS_DIR)
    set(ZELDA3D_ENGINE_ASSETS_DIR "${ZELDA3D_ENGINE_DIR}/assets")
endif()
if (NOT DEFINED ZELDA3D_SHARED_DIR)
    set(ZELDA3D_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shipwright/zelda3d_shared")
endif()

# Port glue shared with the other game as SOURCE rather than as a library: identical in both games,
# but it includes this game's headers (gu_pc.c includes "z64.h", which is a different file in each
# tree), so it must be compiled once per game. See zelda3d_shared/port/README.md and claim C064.
file(GLOB ZELDA3D_SHARED_PORT_SOURCES CONFIGURE_DEPENDS
    "${ZELDA3D_SHARED_DIR}/port/*.c" "${ZELDA3D_SHARED_DIR}/port/*.cpp" "${ZELDA3D_SHARED_DIR}/port/*.h")

# The ROM extractor, shared the same way and for the same reason: one source, compiled per game.
# Extract.cpp names no game type, but it calls this game's ZAPD entry point and reads its build
# version, so it belongs in the game target rather than the zelda3d_shared static library. The
# per-game half is the ROM version table in 2s2h/Extractor/RomVersions.cpp, which the 2s2h glob
# below already picks up.
file(GLOB ZELDA3D_SHARED_EXTRACTOR_SOURCES CONFIGURE_DEPENDS
    "${ZELDA3D_SHARED_DIR}/extractor/*.c" "${ZELDA3D_SHARED_DIR}/extractor/*.cpp"
    "${ZELDA3D_SHARED_DIR}/extractor/*.h")

# Shared GUI code. Compiled per game like port/ above -- see zelda3d_shared/gui/ui_theming.cpp for
# the one per-game value that keeps it out of the static library.
file(GLOB ZELDA3D_SHARED_GUI_SOURCES CONFIGURE_DEPENDS
    "${ZELDA3D_SHARED_DIR}/gui/*.cpp" "${ZELDA3D_SHARED_DIR}/gui/*.h")

################################################################################
# Shared sub-projects. The root CMakeLists already add_subdirectory'd these, so
# the NOT TARGET guards make this a no-op — mm links the same static libs as soh.
################################################################################
if (NOT TARGET libultraship)
    add_subdirectory(${ZELDA3D_ENGINE_DIR} ${CMAKE_BINARY_DIR}/libultraship)
endif()
if (NOT TARGET ZAPDLib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Shipwright/ZAPDTR/ZAPD ${CMAKE_BINARY_DIR}/ZAPD)
endif()

################################################################################
# Sources
################################################################################
# Generate build.c before the src glob picks it up.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/boot/build.c.in ${CMAKE_CURRENT_SOURCE_DIR}/src/boot/build.c @ONLY)

# include headers (drop the N64-only stdlib shims, per 2S2H)
file(GLOB Header_Files__include CONFIGURE_DEPENDS "include/*.h" "include/*.inc")
list(APPEND Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/libc/stdarg.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/bgm.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/math_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/stdbool_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/stddef_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/stdlib_n64.h)
list(REMOVE_ITEM Header_Files__include ${CMAKE_CURRENT_SOURCE_DIR}/include/ultra64.h)

# port shell (2s2h) — includes Extractor (desktop needs it for the in-app extract flow)
file(GLOB_RECURSE ship__ RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    "2s2h/*.c" "2s2h/*.cpp" "2s2h/*.h" "2s2h/*.hpp")
# NOTE: an early N2 attempt excluded the UI/feature shell (BenGui/DeveloperTools/Rando/
# Trackers) to shrink the port, but the "shell" is woven into the game CORE and kept
# enhancements (z_kaleido_mask.c calls HudEditor_*, enhancements call UIWidgets::*/Rando::*,
# SaveManager calls Notification::Emit) — excluding it left 563 undefined references.
# So we compile the WHOLE tree. (This used to add that the ImGui menu was inert because zelda3d's
# ImGui was a no-op shim — no longer true: real Dear ImGui is back as the developer-overlay stack,
# with RmlUi as the shipped game-facing UI. See docs/dusklight-adoption.md.)

file(GLOB_RECURSE libultra_headers RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "include/PR/*.h")

# MM decomp game core. Drop dmadata/elf_message and the ENTIRE libultra tree — the PC
# replacements live in 2s2h (mixer.c / framebuffer_effects.c), in zelda3d_shared/port/ (gu_pc.c,
# shared with OoT) and in libultraship.
file(GLOB_RECURSE src__ RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS "src/*.c" "src/*.h")
list(FILTER src__ EXCLUDE REGEX "src/dmadata/")
list(FILTER src__ EXCLUDE REGEX "src/elf_message/")
list(FILTER src__ EXCLUDE REGEX "src/libultra/")

include(cmake/MM3DLegacySourceContracts.cmake)
mm3d_apply_legacy_source_contracts("${CMAKE_CURRENT_SOURCE_DIR}")

set(ALL_FILES
    ${Header_Files__include}
    ${ship__}
    ${libultra_headers}
    ${src__}
    ${ZELDA3D_SHARED_PORT_SOURCES}
    ${ZELDA3D_SHARED_EXTRACTOR_SOURCES}
    ${ZELDA3D_SHARED_GUI_SOURCES}
)

################################################################################
# Target
################################################################################
# INTERFACE target carrying "how to compile MM source" — includes, definitions, options, PCH and
# library deps. Mirrors soh_settings, and for the same reason: the sources now feed three targets
# (the static lib, the loadable core, the exe) and the configuration must not be duplicated per
# target for them to stay identical.
add_library(mm_settings INTERFACE)

# ZELDA3D_IDENTITY_MTX -- the N64 identity matrix, whose decomp SYMBOL NAME differs per game.
#
# Shared port code under zelda3d_shared/port/ needs it (framebuffer_effects.c loads it as the
# modelview before drawing a full-screen quad), but OoT's decomp calls it gMtxClear and MM's calls it
# gIdentityMtx. They are the SAME matrix -- gMtxClear's raw words {65536,0,1,0, 0,65536,0,1, 0,...}
# unpack, in the N64 Mtx layout, to the identity with zero fractional part, which is what MM's
# gdSPDefMtx(1,0,0,0, ...) builds. Verified as claim C067; the OoT name is simply misleading.
#
# Parameterised here rather than #ifdef'd in the shared source: the difference is a name, and the
# build is where per-game names belong.
target_compile_definitions(mm_settings INTERFACE ZELDA3D_IDENTITY_MTX=gIdentityMtx)

# ZELDA3D_UI_INPUT_FRAME_PADDING_Y -- see the matching block in Shipwright/soh/CMakeLists.txt. MM's
# input fields use 8.0f of vertical frame padding where OoT's use 6.0f; that is the ONLY difference
# among the 26 shared ImGui theming functions, and both are preserved.
target_compile_definitions(mm_settings INTERFACE ZELDA3D_UI_INPUT_FRAME_PADDING_Y=8.0f)


# -fno-gnu-unique: the ONE symbol class RTLD_LOCAL does not privatise. Both cores need it or neither
# is protected -- a unique symbol binds process-wide to whichever core loaded FIRST, so leaving it off
# here would simply move which game wins. See the matching block in Shipwright/soh/CMakeLists.txt for
# the measurement (322 colliding unique symbols) and the crash that found it.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(mm_settings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fno-gnu-unique>)
endif()

# The game engine. It carries no int main(): the process entry point belongs to the launcher, and
# MM is only ever loaded as the core below.
add_library(mm_lib STATIC ${ALL_FILES})
target_link_libraries(mm_lib PUBLIC mm_settings)

# MM as a loadable game core: the same objects as a shared object the launcher dlopen's with
# RTLD_LOCAL, keeping MM's and OoT's colliding decomp symbols private (claim C050). TARGET_OBJECTS
# rather than a link against mm_lib, because a static archive contributes only what the linker finds
# REFERENCED and the decomp reaches its actors and overlays through tables, not from the entry point.
add_library(mm_core SHARED $<TARGET_OBJECTS:mm_lib>)
target_link_libraries(mm_core PRIVATE mm_settings)

# There is NO mm executable, for the same reason OoT has none: the project builds exactly one
# program, the zelda3d launcher, which dlopens whichever game was chosen. MM ships as the core
# above. ${PROJECT_NAME} from here on names that CORE.
set(PROJECT_NAME mm_core)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
endif()

# The MM decomp is pre-C23 C, where `void f()` means "unspecified args" (e.g.
# PadMgr_ThreadEntry called with &gPadMgr). zelda3d globally forces C23, which
# reinterprets `()` as zero-args and breaks such calls. Build mm's C at C17 —
# the standard 2S2H was validated against — rather than patching call sites.
# C_STANDARD is a target property, not an interface one, so it cannot live on mm_settings and is
# set on each target that compiles C.
set_target_properties(mm_lib ${PROJECT_NAME} PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)

################################################################################
# Include directories
################################################################################
find_package(SDL3 REQUIRED)

target_include_directories(mm_settings INTERFACE assets
    ${ZELDA3D_SHARED_DIR}/port
    ${CMAKE_CURRENT_SOURCE_DIR}/include/
    ${CMAKE_CURRENT_SOURCE_DIR}/include/PR
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
    ${CMAKE_CURRENT_SOURCE_DIR}/../Shipwright/libultraship/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Shipwright/ZAPDTR/ZAPD/resource/type
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/2s2h/
    ${SDL3_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/
    ${dr_libs_SOURCE_DIR}
    .
)

################################################################################
# Compile definitions (GNU/Clang/AppleClang — the PC toolchains)
################################################################################
target_compile_definitions(mm_settings INTERFACE
    "$<$<CONFIG:Debug>:_DEBUG;>"
    "$<$<CONFIG:Release>:NDEBUG;>"
    "F3DEX_GBI_2;"
    "_CONSOLE;"
    "_CRT_SECURE_NO_WARNINGS;"
    "ENABLE_OPENGL;"
    "UNICODE;"
    "_UNICODE;"
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
    LOG_LEVEL_GAME_PRINTS=${SPDLOG_LEVEL_OFF}
    "NON_MATCHING;"
    "NON_EQUIVALENT;"
)

################################################################################
# Compile / link options
################################################################################
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(CPU_OPTION -msse2 -mfpmath=sse)
endif()

target_compile_options(mm_settings INTERFACE
    -Wall -Wextra -Wno-error
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-missing-field-initializers
    -Wno-parentheses
    # STOPGAP: -Wno-narrowing here (banned per project policy) because MM's 2s2h
    # port has many vendored narrowing sites; a proper fix is a separate audit.
    -Wno-narrowing
    -Wno-missing-braces
    $<$<COMPILE_LANGUAGE:C>:
        -Wno-discarded-qualifiers
        -Wno-discarded-array-qualifiers
        -Wno-int-conversion
        -Wno-incompatible-pointer-types
    >
    # STOPGAP: -fpermissive (banned per project policy) because MM's BenGui port
    # has "declaration changes meaning" C++ conformance issues (methods named
    # the same as in-scope enum types across UIWidgets*Options). Real fix is a
    # rename refactor across the BenGui headers/callers — separate task.
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
    -pthread
    ${CPU_OPTION}
)

target_link_options(mm_settings INTERFACE
    -pthread
    -Wl,-export-dynamic
)

# mixer.c has SIMD helpers that are far slower unoptimized.
#
# SKIP_PRECOMPILE_HEADERS with it: this is the one file in the target whose OPTIMIZATION LEVEL is set
# per-source, and clang refuses a PCH whose level differs from the consuming TU ("OptimizationLevel
# differs in precompiled file"). That costs nothing in the default build, where the target is also
# -O2 and the levels happen to agree -- and it is a hard build failure the moment they do not, which
# is exactly what a sanitizer build does (-fsanitize=address forces -O1 tree-wide). A file that opts
# out of the target's optimization level has opted out of its precompiled header too.
set_source_files_properties(2s2h/mixer.c PROPERTIES COMPILE_FLAGS "-O2" SKIP_PRECOMPILE_HEADERS ON)

################################################################################
# Precompiled headers.
#
# The MM (2S2H) sources rely on PCH to pre-include the heavy C++/libultraship
# headers at the top of every TU. Many .cpp then do `extern "C" { #include
# "BenPort.h" }`; BenPort.h pulls <ship/Context.h> under __cplusplus, and
# __cplusplus stays defined inside extern "C", so without PCH those STL/Context
# templates get emitted in C linkage ("template with C linkage"). PCH satisfies
# their include guards first, making the later include a no-op. soh (OoT) doesn't
# need this; the MM tree was written against it, so it's a per-target PCH.
################################################################################
target_precompile_headers(mm_settings INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:<libultraship/bridge/consolevariablebridge.h$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<ship/Context.h$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<ship/resource/ResourceManager.h$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<ship/window/Window.h$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<spdlog/spdlog.h$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<map$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<unordered_map$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<filesystem$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<fstream$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<algorithm$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<functional$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<variant$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/2s2h/GameInteractor/GameInteractor.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/include/variables.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/include/functions.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/include/macros.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/include/z64.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/include/ultra64.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/include/global.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/include/z64.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/include/macros.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/include/functions.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/include/variables.h>"
)

################################################################################
# Dependencies + link
################################################################################
add_dependencies(mm_lib libultraship ZAPDLib)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Ogg REQUIRED)
find_package(Vorbis REQUIRED)
find_package(Opus REQUIRED)
find_package(OpusFile REQUIRED)

set(ADDITIONAL_LIBRARY_DEPENDENCIES
    "libultraship;"
    "ZAPDLib;"
    "cmb3d;"
    "zelda3d_shared;"
    "stb;"
    SDL3::SDL3
    "Ogg::ogg"
    "Vorbis::vorbis"
    "Vorbis::vorbisenc"
    "Vorbis::vorbisfile"
    "Opus::opus"
    "Opusfile::Opusfile"
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

target_link_libraries(mm_settings INTERFACE "${ADDITIONAL_LIBRARY_DEPENDENCIES}")

################################################################################
# Post-build: stage the asset xmls + RmlUi assets next to the executable
################################################################################
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMENT "Copying MM asset xmls..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_CURRENT_SOURCE_DIR}/assets/extractor $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_CURRENT_SOURCE_DIR}/assets/xml $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/xml
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/symbols
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${ZELDA3D_ENGINE_ASSETS_DIR}/rml $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/rml
)
