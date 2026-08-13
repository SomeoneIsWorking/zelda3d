# Target definitions for the soh3d_harness executable. Included (deferred)
# from wire_in.cmake so every dep — Threads::Threads, citra_common,
# azahar_libretro_common, ... — is already defined by Azahar's own build by
# the time this file runs.
#
# See wire_in.cmake for the CMAKE_PROJECT_citra_INCLUDE plumbing, and
# main.cpp for the milestone and the direction this scaffold serves.
set(_libretro_root ${CMAKE_SOURCE_DIR}/src/citra_libretro)
set(_harness_root  ${CMAKE_CURRENT_LIST_DIR})
set(_zelda3d_root  ${_harness_root}/../..)
set(_soh_core      ${_zelda3d_root}/Shipwright/build-cmake/soh/libsoh_core.so)
set(_lus_core      ${_zelda3d_root}/Shipwright/build-cmake/libultraship/src/libultraship.so)
if(NOT EXISTS ${_soh_core} OR NOT EXISTS ${_lus_core})
    message(FATAL_ERROR "soh3d_harness needs the shipping OoT build; run cmake --build Shipwright/build-cmake --target zelda3d_app -j4 first")
endif()
add_library(zelda3d_harness_soh_core SHARED IMPORTED)
set_target_properties(zelda3d_harness_soh_core PROPERTIES IMPORTED_LOCATION ${_soh_core})
add_library(zelda3d_harness_lus_core SHARED IMPORTED)
set_target_properties(zelda3d_harness_lus_core PROPERTIES IMPORTED_LOCATION ${_lus_core})
find_package(SDL3 REQUIRED)

add_executable(soh3d_harness
    ${_harness_root}/main.cpp
    ${_harness_root}/soh_state.cpp
    ${_harness_root}/watchhook.cpp
    ${_harness_root}/title_sync.cpp
    ${_harness_root}/harness_vk.cpp
    ${_libretro_root}/citra_libretro.cpp
    $<TARGET_OBJECTS:azahar_libretro_common>
)

# soh_state.cpp includes SoH's z64.h / z64actor.h so it can read fields through
# the 64-bit C++ layout (raw offsets from N64 header comments are wrong there).
# Direct include roots and ABI definitions below must match the shipping core.

# These -D switches gate the renderer surface in common/settings.h and
# citra_libretro.cpp. Azahar adds them via add_compile_definitions() in
# src/CMakeLists.txt, which only applies to targets in src/ and its
# children. Our target lives in the top-level scope (deferred-included),
# so we must add the same -Ds explicitly.
target_compile_definitions(soh3d_harness PRIVATE
    HAVE_LIBRETRO
    $<$<BOOL:${ENABLE_SOFTWARE_RENDERER}>:ENABLE_SOFTWARE_RENDERER>
    $<$<BOOL:${ENABLE_OPENGL}>:ENABLE_OPENGL>
    $<$<BOOL:${ENABLE_VULKAN}>:ENABLE_VULKAN>
    # The root CMakeLists.txt sets this via `add_compile_definitions()`
    # at the top-level scope, which applies to targets in that scope and
    # under — but soh3d_harness lives in tools/ (this file is deferred-
    # included from wire_in.cmake) so it does NOT inherit. Without it,
    # soh_state.cpp's `OSContPad` struct has button as u16 while soh_lib
    # code (z_player.c etc.) has it as u32; the resulting 2-byte layout
    # skew means a scripted-input write from the harness lands 2 bytes
    # off the field Player_Update reads, and Link never sees any injection.
    CONTROLLERBUTTONS_T=uint32_t
)
target_include_directories(soh3d_harness PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    # The harness directly includes asset/texpack.h. Linking cmb3d does not propagate its include
    # directory in the Azahar superbuild because the imported Shipwright target is consumed across
    # the deferred top-level boundary, so declare the direct source dependency explicitly here.
    ${_harness_root}/../../Shipwright/cmb3d
    ${_harness_root}/../../Shipwright/soh
    ${_harness_root}/../../Shipwright/soh/include
    ${_harness_root}/../../Shipwright/soh/src
    ${_harness_root}/../../Shipwright/libultraship/include
)

# -rdynamic so backtrace(3) can resolve our function names in the watchdog
# stack dump. Cheap; only affects the harness executable's symbol table.
target_link_options(soh3d_harness PRIVATE -rdynamic)

target_link_libraries(soh3d_harness PRIVATE
    citra_common citra_core
    Boost::boost dds-ktx libretro tsl::robin_map
    ${PLATFORM_LIBRARIES} Threads::Threads
    zelda3d_harness_soh_core zelda3d_harness_lus_core SDL3::SDL3
)

if(ENABLE_VULKAN)
    target_link_libraries(soh3d_harness PRIVATE sirit vulkan-headers vma)
    # harness_vk.cpp is a real libretro Vulkan HW-render FRONTEND: it creates
    # its own VkInstance/VkDevice and calls vkCreateInstance / vkCmd* directly,
    # so it needs the Vulkan loader at link time (vulkan-headers is headers-
    # only). Azahar's own renderer loads Vulkan dynamically, hence no existing
    # link dep to inherit.
    find_library(HARNESS_VULKAN_LOADER NAMES vulkan vulkan-1)
    if(HARNESS_VULKAN_LOADER)
        target_link_libraries(soh3d_harness PRIVATE ${HARNESS_VULKAN_LOADER})
    else()
        target_link_libraries(soh3d_harness PRIVATE vulkan)
    endif()
endif()
if(ENABLE_OPENGL)
    target_link_libraries(soh3d_harness PRIVATE glad)
endif()

if (SSE42_COMPILE_OPTION)
    target_compile_definitions(soh3d_harness PRIVATE CITRA_HAS_SSE42)
endif()
