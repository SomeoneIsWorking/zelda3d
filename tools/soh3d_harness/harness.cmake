# Target definitions for the soh3d_harness executable. Included (deferred)
# from wire_in.cmake so every dep — Threads::Threads, citra_common,
# azahar_libretro_common, ... — is already defined by Azahar's own build by
# the time this file runs.
#
# See wire_in.cmake for the CMAKE_PROJECT_citra_INCLUDE plumbing and main.cpp
# for the composition entry point.
set(_libretro_root ${CMAKE_SOURCE_DIR}/src/citra_libretro)
set(_harness_root  ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(_zelda3d_root "${_harness_root}/../.." ABSOLUTE)

set(ZELDA3D_SHIPPING_BUILD_DIR "${_zelda3d_root}/Shipwright/build-cmake" CACHE PATH
    "Configured Zelda3D build tree whose shipping cores the harness imports")
include(${_harness_root}/shipping_artifacts.cmake)

zelda3d_find_shipping_artifact(_soh_core "${ZELDA3D_SHIPPING_BUILD_DIR}" soh_core
    "${CMAKE_SHARED_LIBRARY_PREFIX}" "${CMAKE_SHARED_LIBRARY_SUFFIX}" "${CMAKE_BUILD_TYPE}")
zelda3d_find_shipping_artifact(_lus_core "${ZELDA3D_SHIPPING_BUILD_DIR}" ultraship
    "${CMAKE_SHARED_LIBRARY_PREFIX}" "${CMAKE_SHARED_LIBRARY_SUFFIX}" "${CMAKE_BUILD_TYPE}")
add_library(zelda3d_harness_soh_core SHARED IMPORTED)
set_target_properties(zelda3d_harness_soh_core PROPERTIES IMPORTED_LOCATION ${_soh_core})
add_library(zelda3d_harness_lus_core SHARED IMPORTED)
set_target_properties(zelda3d_harness_lus_core PROPERTIES IMPORTED_LOCATION ${_lus_core})
if(WIN32)
    zelda3d_find_shipping_artifact(_soh_core_import "${ZELDA3D_SHIPPING_BUILD_DIR}" soh_core
        "${CMAKE_IMPORT_LIBRARY_PREFIX}" "${CMAKE_IMPORT_LIBRARY_SUFFIX}" "${CMAKE_BUILD_TYPE}")
    zelda3d_find_shipping_artifact(_lus_core_import "${ZELDA3D_SHIPPING_BUILD_DIR}" ultraship
        "${CMAKE_IMPORT_LIBRARY_PREFIX}" "${CMAKE_IMPORT_LIBRARY_SUFFIX}" "${CMAKE_BUILD_TYPE}")
    set_target_properties(zelda3d_harness_soh_core PROPERTIES IMPORTED_IMPLIB ${_soh_core_import})
    set_target_properties(zelda3d_harness_lus_core PROPERTIES IMPORTED_IMPLIB ${_lus_core_import})
endif()
find_package(SDL3 REQUIRED)

add_executable(soh3d_harness
    ${_harness_root}/main.cpp
    ${_harness_root}/actor_compare.cpp
    ${_harness_root}/binary_file.cpp
    ${_harness_root}/boss_fd_control.cpp
    ${_harness_root}/boss_fd_comparison_policy.cpp
    ${_harness_root}/boss_fd_compare.cpp
    ${_harness_root}/boss_fd_oracle.cpp
    ${_harness_root}/boss_fd_profile_validation.cpp
    ${_harness_root}/framebuffer_snapshot.cpp
    ${_harness_root}/comparison_commands.cpp
    ${_harness_root}/environment_probe_commands.cpp
    ${_harness_root}/first_div_actor_compare.cpp
    ${_harness_root}/first_div_compare.cpp
    ${_harness_root}/first_div_gameplay_camera_compare.cpp
    ${_harness_root}/first_div_gameplay_compare.cpp
    ${_harness_root}/first_div_policy.cpp
    ${_harness_root}/first_div_player_compare.cpp
    ${_harness_root}/first_div_reporter.cpp
    ${_harness_root}/first_div_state_capture.cpp
    ${_harness_root}/first_div_title_camera_compare.cpp
    ${_harness_root}/first_div_title_pose_compare.cpp
    ${_harness_root}/frontend_diagnostics.cpp
    ${_harness_root}/frontend_input.cpp
    ${_harness_root}/frontend_input_commands.cpp
    ${_harness_root}/frontend_presentation.cpp
    ${_harness_root}/frontend_timing.cpp
    ${_harness_root}/frame_watchdog.cpp
    ${_harness_root}/harness_memory.cpp
    ${_harness_root}/harness_repl.cpp
    ${_harness_root}/libretro_frontend.cpp
    ${_harness_root}/libretro_callbacks.cpp
    ${_harness_root}/lockstep_runner.cpp
    ${_harness_root}/memory_dump.cpp
    ${_harness_root}/memory_search.cpp
    ${_harness_root}/oracle_control_commands.cpp
    ${_harness_root}/oracle_camera_compare.cpp
    ${_harness_root}/oracle_actor_commands.cpp
    ${_harness_root}/oracle_scene_commands.cpp
    ${_harness_root}/oracle_lighting_compare.cpp
    ${_harness_root}/oracle_player_compare.cpp
    ${_harness_root}/oracle_player_animation_commands.cpp
    ${_harness_root}/oracle_player_locator.cpp
    ${_harness_root}/oracle_player_state_commands.cpp
    ${_harness_root}/oracle_state.cpp
    ${_harness_root}/oracle_scene_compare.cpp
    ${_harness_root}/oracle_skeleton_compare.cpp
    ${_harness_root}/paired_camera_control.cpp
    ${_harness_root}/oracle_state_storage.cpp
    ${_harness_root}/oracle_title_state.cpp
    ${_harness_root}/oracle_title_actor_compare.cpp
    ${_harness_root}/player_probe_commands.cpp
    ${_harness_root}/process_environment.cpp
    ${_harness_root}/render_debug_commands.cpp
    ${_harness_root}/repl_help.cpp
    ${_harness_root}/repl_protocol.cpp
    ${_harness_root}/soh_boss_fd_state.cpp
    ${_harness_root}/soh_actor_state.cpp
    ${_harness_root}/soh_animation_state.cpp
    ${_harness_root}/soh_camera_state.cpp
    ${_harness_root}/soh_environment_state.cpp
    ${_harness_root}/soh_input_state.cpp
    ${_harness_root}/soh_lighting_state.cpp
    ${_harness_root}/soh_play_state.cpp
    ${_harness_root}/soh_player_state.cpp
    ${_harness_root}/soh_player_control_commands.cpp
    ${_harness_root}/soh_player_state_commands.cpp
    ${_harness_root}/soh_runtime.cpp
    ${_harness_root}/soh_warp_state.cpp
    ${_harness_root}/title_sync.cpp
    ${_harness_root}/title_sync_runtime.cpp
    ${_harness_root}/title_sweep.cpp
    ${_harness_root}/title_probe_commands.cpp
    ${_harness_root}/texpack_setup.cpp
    ${_harness_root}/harness_vk.cpp
    ${_harness_root}/watch_commands.cpp
    ${_harness_root}/watchhook.cpp
    ${_libretro_root}/citra_libretro.cpp
    $<TARGET_OBJECTS:azahar_libretro_common>
)

# The focused soh_*_state.cpp owners include SoH's typed headers so they read
# fields through the 64-bit C++ layout. Direct include roots and ABI definitions
# below must match the shipping core.

# These -D switches gate the renderer surface in common/settings.h and
# citra_libretro.cpp. Azahar adds them via add_compile_definitions() in
# src/CMakeLists.txt, which only applies to targets in src/ and its
# children. Our target lives in the top-level scope (deferred-included),
# so we must add the same -Ds explicitly.
target_compile_definitions(soh3d_harness PRIVATE
    HAVE_LIBRETRO
    ZELDA3D_HARNESS_REPO_ROOT="${_zelda3d_root}"
    $<$<BOOL:${ENABLE_SOFTWARE_RENDERER}>:ENABLE_SOFTWARE_RENDERER>
    $<$<BOOL:${ENABLE_OPENGL}>:ENABLE_OPENGL>
    $<$<BOOL:${ENABLE_VULKAN}>:ENABLE_VULKAN>
    # The root CMakeLists.txt sets this via `add_compile_definitions()`
    # at the top-level scope, which applies to targets in that scope and
    # under — but soh3d_harness lives in tools/ (this file is deferred-
    # included from wire_in.cmake) so it does NOT inherit. Without it,
    # soh_input_state.cpp's `OSContPad` struct has button as u16 while soh_lib
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
    ${_zelda3d_root}/Shipwright/cmb3d
    ${_zelda3d_root}/Shipwright/soh
    ${_zelda3d_root}/Shipwright/soh/include
    ${_zelda3d_root}/Shipwright/soh/src
    ${_zelda3d_root}/Shipwright/libultraship/include
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
