# Zelda3D — the project root.
#
# One runtime, one engine, two peer games. Zelda3D owns this layering directly:
#
#   engine      Shipwright/libultraship   window/renderer/input/resources — knows no game
#               Shipwright/cmb3d          3DS asset formats (CMB/CSAB/ZAR/ZSI/…)
#   shared      Shipwright/zelda3d_shared port code common to both games
#   games       Shipwright/soh (OoT)      decomp core + its port shell + its zelda3d layer
#               2ship          (MM)       ditto
#   launcher    Shipwright/zelda3d_app    the one binary; dlopens a game core, holds no game code
#
# WHY THIS FILE EXISTS. The configure root used to be Shipwright/CMakeLists.txt — that is, OoT's own
# directory — and it pulled the other game in from outside its own tree with
#
#     add_subdirectory(${CMAKE_SOURCE_DIR}/../2ship ${CMAKE_BINARY_DIR}/mm)
#
# so OoT was structurally the host and MM the guest. That is not a cosmetic complaint: because
# CMAKE_SOURCE_DIR happened to BE Shipwright/, 2ship/CMakeLists.txt could reach the engine's assets
# as "${CMAKE_SOURCE_DIR}/libultraship/assets/rml" — MM depending, silently, on OoT's directory
# being the root. The two games are peers here, and the named ZELDA3D_*_DIR variables below mean no
# component has to know which directory the build was configured from.
#
# Configure from the REPO ROOT:  cmake -S . -B Shipwright/build-cmake -G Ninja

cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export commands for Clang verification" FORCE)
set(CMAKE_SYSTEM_VERSION 10.0 CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_C_STANDARD 23 CACHE STRING "The C standard to use")

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

# Named "Ship" with VERSION 9.2.3 still: Shipwright/soh/CMakeLists.txt has no project() call of its
# own and reads PROJECT_VERSION_* from here (soh/CMakeLists.txt:807), as does CMake/Packaging.cmake.
project(Ship VERSION 9.2.3 LANGUAGES C CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(zelda3d_verify_clang
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_clang.py"
            --compile-commands "${CMAKE_BINARY_DIR}/compile_commands.json"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking changed first-party C/C++ with Clang and the source-size ratchet"
    VERBATIM)

################################################################################
# Where each layer lives.
#
# Every path below is derived from THIS directory, so moving a component means editing one line
# here rather than hunting ${CMAKE_SOURCE_DIR}-relative guesses across four CMakeLists.
################################################################################
set(ZELDA3D_ROOT             "${CMAKE_CURRENT_SOURCE_DIR}")
set(ZELDA3D_SHIPWRIGHT_DIR   "${ZELDA3D_ROOT}/Shipwright")
set(ZELDA3D_ENGINE_DIR       "${ZELDA3D_SHIPWRIGHT_DIR}/libultraship")
set(ZELDA3D_ENGINE_ASSETS_DIR "${ZELDA3D_ENGINE_DIR}/assets")
set(ZELDA3D_SHARED_DIR       "${ZELDA3D_SHIPWRIGHT_DIR}/zelda3d_shared")
set(ZELDA3D_OOT_DIR          "${ZELDA3D_SHIPWRIGHT_DIR}/soh")
set(ZELDA3D_MM_DIR           "${ZELDA3D_ROOT}/2ship")

include(${ZELDA3D_SHIPWRIGHT_DIR}/CMake/soh-cvars.cmake)
include(${ZELDA3D_SHIPWRIGHT_DIR}/CMake/lus-cvars.cmake)
set(SPDLOG_LEVEL_TRACE 0)
set(SPDLOG_LEVEL_OFF 6)
set(SPDLOG_MIN_CUTOFF SPDLOG_LEVEL_TRACE CACHE STRING "cutoff at trace")

# AddressSanitizer, off by default. Built in rather than hand-patched because locating a heap
# corruption here has no cheaper instrument: valgrind is not installed, glibc malloc checking does
# not move the detection point, and the release build is -O2/-DNDEBUG with no line info, so
# addr2line resolves to "??:?" and a backtrace cannot name the writer. See docs/issues/0009.
#
#   cmake -S . -B scratch/build-asan -G Ninja -DZELDA3D_SANITIZE=address \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#
# The clang override is not required, but it is the tested path here. If a gcc ASAN link ever fails
# with "cannot find /usr/lib64/libasan.so.<v>", the libasan RUNTIME package is missing rather than
# anything being wrong with the tree -- gcc ships the driver support without it. (That happened on
# this machine on 2026-08-11 and was fixed by installing it.) Worth knowing because the failure
# arrives at LINK time, after the whole tree has compiled, and reads like a code fault.
#
# -fno-omit-frame-pointer keeps the stacks readable; -O1 keeps it from taking all day. Applied to
# both compile and link (ASAN needs its runtime on the link line) and to every target in the tree,
# because the corruption is not known to live in any one of them.
#
# -fsanitize-recover is on because a sanitizer that halts on the FIRST error can only ever show you
# the first bug. Chasing `mm,oot,mm` cost several full ASAN runs to learn this: an out-of-bounds
# animation read in the SECOND game core aborted every run, and the crash actually being investigated
# -- in the THIRD core -- was never reached even once. With recovery compiled in, a run can be told
# to keep going and report everything:
#
#   ASAN_OPTIONS="halt_on_error=0:detect_odr_violation=0:log_path=<dir>/asan" ...
#
# halt_on_error=0 does NOT work without this flag; it is silently ignored, which reads as "there was
# only one error". Default behaviour is unchanged: without halt_on_error the run still stops at the
# first report.
set(ZELDA3D_SANITIZE "" CACHE STRING "Sanitizer to build with (e.g. address, undefined); empty = none")
if(NOT ZELDA3D_SANITIZE STREQUAL "")
    message(STATUS "Building with -fsanitize=${ZELDA3D_SANITIZE} (recoverable)")
    add_compile_options(-fsanitize=${ZELDA3D_SANITIZE} -fsanitize-recover=${ZELDA3D_SANITIZE}
                        -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=${ZELDA3D_SANITIZE} -fsanitize-recover=${ZELDA3D_SANITIZE})
endif()

################################################################################
# ccache, when it is installed.
#
# This tree is ~7,000 translation units across two decomps, and a structural change (moving the
# configure root, extracting a shared library) invalidates the build directory rather than a few
# objects. Without a compile cache every such change costs a full rebuild on a machine that can
# only afford -j4. Opt-out with -DZELDA3D_CCACHE=OFF.
################################################################################
option(ZELDA3D_CCACHE "Use ccache as the compiler launcher when available" ON)
if(ZELDA3D_CCACHE AND NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(ZELDA3D_CCACHE_PROGRAM ccache)
    if(ZELDA3D_CCACHE_PROGRAM)
        message(STATUS "ccache: ${ZELDA3D_CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER   "${ZELDA3D_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${ZELDA3D_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
    else()
        message(STATUS "ccache: not found, compiling without a cache")
    endif()
endif()

option(SUPPRESS_WARNINGS "Suppress warnings in LUS and src (decomp)" ON)
if(SUPPRESS_WARNINGS)
    MESSAGE("Suppressing warnings in LUS and src")
    if(MSVC)
        set(WARNING_OVERRIDE /w)
    else()
        set(WARNING_OVERRIDE -w)
    endif()
else()
    MESSAGE("Skipping warning suppression")
endif()

set(NATO_PHONETIC_ALPHABET
  "Alfa" "Bravo" "Charlie" "Delta" "Echo" "Foxtrot" "Golf" "Hotel"
  "India" "Juliett" "Kilo" "Lima" "Mike" "November" "Oscar" "Papa"
  "Quebec" "Romeo" "Sierra" "Tango" "Uniform" "Victor" "Whiskey"
  "Xray" "Yankee" "Zulu"
)

# Get the patch version number from the project version
math(EXPR PATCH_INDEX "${PROJECT_VERSION_PATCH}")

# Use the patch number to select the correct word
list(GET NATO_PHONETIC_ALPHABET ${PATCH_INDEX} PROJECT_PATCH_WORD)

set(PROJECT_BUILD_NAME "Ackbar ${PROJECT_PATCH_WORD}" CACHE STRING "" FORCE)
set(PROJECT_TEAM "github.com/harbourmasters" CACHE STRING "" FORCE)

execute_process(
    COMMAND git branch --show-current
    WORKING_DIRECTORY ${ZELDA3D_ROOT}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_PROJECT_GIT_BRANCH "${GIT_BRANCH}" CACHE STRING "Git branch" FORCE)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${ZELDA3D_ROOT}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get only the first 7 characters of the hash
string(SUBSTRING "${GIT_COMMIT_HASH}" 0 7 SHORT_COMMIT_HASH)

set(CMAKE_PROJECT_GIT_COMMIT_HASH "${SHORT_COMMIT_HASH}" CACHE STRING "Git commit hash" FORCE)

execute_process(
    COMMAND git describe --tags --abbrev=0 --exact-match HEAD
    WORKING_DIRECTORY ${ZELDA3D_ROOT}
    OUTPUT_VARIABLE GIT_COMMIT_TAG
    RESULT_VARIABLE GIT_COMMIT_TAG_RESULT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT GIT_COMMIT_TAG_RESULT EQUAL 0)
    set(GIT_COMMIT_TAG "")
endif()

set(CMAKE_PROJECT_GIT_COMMIT_TAG "${GIT_COMMIT_TAG}" CACHE STRING "Git commit tag" FORCE)

set_property(DIRECTORY ${ZELDA3D_ROOT} PROPERTY VS_STARTUP_PROJECT zelda3d_app)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        if(VCPKG_TARGET_TRIPLET MATCHES "^arm64-")
            set(CMAKE_VS_PLATFORM_NAME "arm64")
        else()
            set(CMAKE_VS_PLATFORM_NAME "x64")
        endif()
    endif()

    if("${CMAKE_VS_PLATFORM_NAME}" MATCHES "^[Aa][Rr][Mm]64$")
        set(SOH_WINDOWS_ARM64 TRUE)
    else()
        set(SOH_WINDOWS_ARM64 FALSE)
    endif()

    if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        message(FATAL_ERROR
            "Windows builds require a user-owned vcpkg toolchain. "
            "Run ./run.sh --bootstrap-check for the exact install command, set VCPKG_ROOT, "
            "and configure through ./run.sh; Zelda3D never downloads or installs packages silently.")
    endif()
    message(STATUS "Windows vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")
    if (CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache|sccache")
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    endif()
endif()

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"
        OR SOH_WINDOWS_ARM64))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

################################################################################
# Global configuration types
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
set(CMAKE_C_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
else()
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()
# IEEE 754 compliant floating-point rounding on arm64 macOS
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_compile_options(-Xarch_arm64 -ffp-contract=off)
endif()

if(NOT CMAKE_BUILD_TYPE )
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

################################################################################
# Common utils
################################################################################
include(${ZELDA3D_SHIPWRIGHT_DIR}/CMake/Utils.cmake)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    get_linux_lsb_release_information()
    message(STATUS "Linux ${LSB_RELEASE_ID_SHORT} ${LSB_RELEASE_VERSION_SHORT} ${LSB_RELEASE_CODENAME_SHORT}")
else()
    message(STATUS ${CMAKE_SYSTEM_NAME})
endif()

################################################################################
# Additional Global Settings(add specific info there)
################################################################################
include(${ZELDA3D_SHIPWRIGHT_DIR}/CMake/GlobalSettingsInclude.cmake OPTIONAL)

################################################################################
# Use solution folders feature
################################################################################
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Set LUS vars
################################################################################

# Enable the Gfx debugger in LUS to use libgfxd from ZAPDTR
set(GFX_DEBUG_DISASSEMBLER ON)

# Tell LUS we're using F3DEX_GBI_2 (in a way that doesn't break libgfxd)
set(GBI_UCODE F3DEX_GBI_2)
list(APPEND CMAKE_MODULE_PATH "${ZELDA3D_SHIPWRIGHT_DIR}/CMake")

# Enable MPQ and OTR support
set(INCLUDE_MPQ_SUPPORT ON)

################################################################################
# Set CONTROLLERBUTTONS_T
################################################################################
add_compile_definitions(CONTROLLERBUTTONS_T=uint32_t)

################################################################################
# Position-independent code EVERYWHERE.
#
# Required because libultraship is a SHARED library. One binary running both Ocarina of Time and
# Majora's Mask needs each game core dlopen'd with RTLD_LOCAL so their colliding decomp symbols stay
# private (claim C050) -- but RTLD_LOCAL alone would also give each core its own copy of every
# libultraship global, i.e. two windows and two renderers (claim C054). A single shared
# libultraship is what makes the engine state single-instance, and a shared object can only link
# static dependencies that were compiled -fPIC. Without this the link fails on StormLib:
#   relocation R_X86_64_32S against `.rodata' can not be used when making a shared object
################################################################################
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

################################################################################
# dr_libs — single-header audio decoders (wav/mp3/flac), used by BOTH games.
#
# Declared here, once, because zelda3d_shared now compiles the one and only copy of their
# implementation (zelda3d_shared/audio/dr_libs_impl.cpp). It used to be declared separately by soh
# and by 2ship, each of which then baked its own private copy into its game core.
################################################################################
include(FetchContent)
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG da35f9d6c7374a95353fd1df1d394d44ab66cf01
)
FetchContent_MakeAvailable(dr_libs)

################################################################################
# ENGINE — knows nothing about either game.
################################################################################
add_subdirectory(${ZELDA3D_ENGINE_DIR} ${CMAKE_BINARY_DIR}/libultraship)
target_compile_options(libultraship PRIVATE "${WARNING_OVERRIDE}")
target_compile_definitions(libultraship PUBLIC INCLUDE_MPQ_SUPPORT)

add_subdirectory(${ZELDA3D_SHIPWRIGHT_DIR}/ZAPDTR/ZAPD ${CMAKE_BINARY_DIR}/ZAPD)
add_subdirectory(${ZELDA3D_SHIPWRIGHT_DIR}/OTRExporter ${CMAKE_BINARY_DIR}/OTRExporter)

# cmb3d — shared 3DS asset layer (CMB/CSAB/ZAR/ZSI/CTR-ROM/…), linked by BOTH games.
set(ZELDA3D_LUCENT_SOURCE "${ZELDA3D_ROOT}/build/deps/lucent-source")
if(NOT EXISTS "${ZELDA3D_LUCENT_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Missing pinned Lucent source: ${ZELDA3D_LUCENT_SOURCE}. Run the canonical Python source provisioning before configuring.")
endif()
set(LUCENT_BUILD_ZIP ON CACHE BOOL "Build shared bounded ZIP support" FORCE)
set(LUCENT_BUILD_TESTS OFF CACHE BOOL "Lucent tests belong to its own repository" FORCE)
add_subdirectory("${ZELDA3D_LUCENT_SOURCE}" "${CMAKE_BINARY_DIR}/lucent")
add_subdirectory(${ZELDA3D_SHIPWRIGHT_DIR}/cmb3d ${CMAKE_BINARY_DIR}/cmb3d)

################################################################################
# SHARED PORT CODE — game-agnostic, linked by BOTH games.
################################################################################
add_subdirectory(${ZELDA3D_SHARED_DIR} ${CMAKE_BINARY_DIR}/zelda3d_shared)

################################################################################
# GAMES — peers. Neither hosts the other.
################################################################################
add_subdirectory(${ZELDA3D_OOT_DIR} ${CMAKE_BINARY_DIR}/soh)
add_subdirectory(${ZELDA3D_MM_DIR} ${CMAKE_BINARY_DIR}/mm)

if(LUS_BUILD_TESTS)
    enable_testing()
    find_package(SDL3 REQUIRED)

    add_executable(n64_rom_validation_test Shipwright/zelda3d_shared/extractor/tests/n64_rom_validation_test.cpp)
    target_link_libraries(n64_rom_validation_test PRIVATE zelda3d_shared)
    add_test(NAME n64_rom_validation_test COMMAND n64_rom_validation_test
        "${CMAKE_BINARY_DIR}/scratch/n64-rom-validation")
    add_executable(ctr_rom_test Shipwright/cmb3d/tests/ctr_rom_test.cpp)
    target_link_libraries(ctr_rom_test PRIVATE cmb3d lucent::lucent)
    add_test(NAME ctr_rom_test COMMAND ctr_rom_test "${CMAKE_BINARY_DIR}/scratch/ctr-rom-validation")
    add_executable(mm_gfx_print_test 2ship/2s2h/host/gfx_print.cpp 2ship/tests/gfx_print_test.cpp)
    target_include_directories(mm_gfx_print_test PRIVATE 2ship/2s2h)
    target_link_libraries(mm_gfx_print_test PRIVATE SDL3::SDL3)
    add_test(NAME mm_gfx_print_test COMMAND mm_gfx_print_test)

    add_executable(game_interactor_multicast_test tools/game_interactor_multicast_test.cpp)
    # Exercise the shipping template with the core's header contract, without
    # linking the game/exporter implementation into this asset-free unit test.
    target_include_directories(game_interactor_multicast_test PRIVATE
        $<TARGET_PROPERTY:soh_settings,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(game_interactor_multicast_test PRIVATE
        $<TARGET_PROPERTY:soh_settings,INTERFACE_COMPILE_DEFINITIONS>)
    target_link_libraries(game_interactor_multicast_test PRIVATE libultraship)
    add_test(NAME game_interactor_multicast_test COMMAND game_interactor_multicast_test)

    add_executable(boss_fd2_animation_policy_test
        tools/boss_fd2_animation_policy_test.cpp
        Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd2_animation_policy.cpp
    )
    target_include_directories(boss_fd2_animation_policy_test PRIVATE
        Shipwright/soh/src/zelda3d/behaviors/actor
    )
    add_test(NAME boss_fd2_animation_policy_test COMMAND boss_fd2_animation_policy_test)

    add_executable(boss_fd2_material_controller_test
        tools/boss_fd2_material_controller_test.cpp
    )
    target_include_directories(boss_fd2_material_controller_test PRIVATE
        Shipwright/soh/src/zelda3d/behaviors/actor
    )
    add_test(NAME boss_fd2_material_controller_test COMMAND boss_fd2_material_controller_test)

    add_executable(mm3d_player_animation_policy_test
        tools/mm3d_player_animation_policy_test.cpp
        2ship/2s2h/zelda3d/mm3d_player_animation_policy.cpp
    )
    target_include_directories(mm3d_player_animation_policy_test PRIVATE ${ZELDA3D_MM_DIR})
    add_test(NAME mm3d_player_animation_policy_test COMMAND mm3d_player_animation_policy_test)
endif()

################################################################################
# LAUNCHER — the one binary. Must come after both games, since it depends on
# their core targets. Holds no game code itself — see zelda3d_app/zelda3d_main.cpp.
################################################################################
add_subdirectory(${ZELDA3D_SHIPWRIGHT_DIR}/zelda3d_app ${CMAKE_BINARY_DIR}/zelda3d)

################################################################################
# Packaging and asset extraction. Linux releases use tools/package_appimage.py, which stages the
# launcher, both cores and both redistributable port archives while refusing ROM-derived archives.
################################################################################
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
install(DIRECTORY "${ZELDA3D_OOT_DIR}/assets/extractor/" DESTINATION ./assets/ COMPONENT ship)
install(DIRECTORY "${ZELDA3D_OOT_DIR}/assets/xml/" DESTINATION ./assets/xml COMPONENT ship)
endif()

include(${ZELDA3D_ROOT}/cmake/Zelda3DAssetExtraction.cmake)
include(${ZELDA3D_ROOT}/cmake/Zelda3DRuntimeArchives.cmake)
add_dependencies(zelda3d_app zelda3d_runtime_archives)

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")

install(TARGETS ZAPD DESTINATION ${CMAKE_BINARY_DIR}/assets)

set(PROGRAM_PERMISSIONS_EXECUTE OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)

install(DIRECTORY "${ZELDA3D_OOT_DIR}/assets/extractor/" DESTINATION ./assets/)
install(DIRECTORY "${ZELDA3D_OOT_DIR}/assets/xml/" DESTINATION ./assets/xml)

# Rename the installed soh binary to drop the macos suffix
INSTALL(CODE "FILE(RENAME \${CMAKE_INSTALL_PREFIX}/../MacOS/soh-macos \${CMAKE_INSTALL_PREFIX}/../MacOS/soh)")

install(CODE "
    include(BundleUtilities)
    fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/../MacOS/soh\" \"\" \"${dirs}\")
    ")

endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows|NintendoSwitch|CafeOS")
install(FILES ${ZELDA3D_ROOT}/README.md DESTINATION . COMPONENT ship RENAME readme.txt )
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows|NintendoSwitch|CafeOS")
    set(CPACK_GENERATOR "ZIP")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(CPACK_GENERATOR "Bundle")
endif()

if(NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CPACK_PROJECT_CONFIG_FILE ${ZELDA3D_SHIPWRIGHT_DIR}/CMake/Packaging-2.cmake)
    include(${ZELDA3D_SHIPWRIGHT_DIR}/CMake/Packaging.cmake)
endif()
