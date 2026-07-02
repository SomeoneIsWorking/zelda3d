# Target definitions for the soh3d_harness executable. Included (deferred)
# from wire_in.cmake so every dep — Threads::Threads, citra_common,
# azahar_libretro_common, ... — is already defined by Azahar's own build by
# the time this file runs.
#
# See wire_in.cmake for the CMAKE_PROJECT_citra_INCLUDE plumbing, and
# main.cpp for the milestone and the direction this scaffold serves.
set(_libretro_root ${CMAKE_SOURCE_DIR}/src/citra_libretro)
set(_harness_root  ${CMAKE_CURRENT_LIST_DIR})

add_executable(soh3d_harness
    ${_harness_root}/main.cpp
    ${_harness_root}/soh_state.cpp
    ${_libretro_root}/citra_libretro.cpp
    $<TARGET_OBJECTS:azahar_libretro_common>
)

# soh_state.cpp includes SoH's z64.h / z64actor.h so it can read struct
# fields through the 64-bit C++ layout (raw offsets from the N64 header
# comments are wrong on a 64-bit build). It needs soh_settings' compile
# flags — inherited transitively from soh_lib PRIVATE (soh_settings is
# linked PUBLIC to soh_lib), so no extra wiring needed here.

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
)
target_include_directories(soh3d_harness PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(soh3d_harness PRIVATE
    citra_common citra_core
    Boost::boost dds-ktx libretro tsl::robin_map
    ${PLATFORM_LIBRARIES} Threads::Threads
    soh_lib
)

# soh_lib pulls in the whole Shipwright side (ZAPDLib, libultraship,
# OTRExporter, cmb3d, zelda3d_shared, ...). OTRExporter is added via
# `-Wl,--whole-archive OTRExporter --no-whole-archive` from ZAPDLib's
# PUBLIC deps: those .o files reference ::BinaryWriter::Write DEFINED in
# ZAPDLib but placed EARLIER in the link line. Single-pass ld.bfd can't
# resolve into archives that came before.
#
# Fix: force ZAPDLib ALSO to be whole-archived, so all its .o (including
# BinaryWriter.cpp.o) are unconditionally present before OTRExporter's
# whole-archive .o files land.
target_link_libraries(soh3d_harness PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,ZAPDLib>")
if(ENABLE_VULKAN)
    target_link_libraries(soh3d_harness PRIVATE sirit vulkan-headers vma)
endif()
if(ENABLE_OPENGL)
    target_link_libraries(soh3d_harness PRIVATE glad)
endif()

if (SSE42_COMPILE_OPTION)
    target_compile_definitions(soh3d_harness PRIVATE CITRA_HAS_SSE42)
endif()
