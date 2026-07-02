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
    ${_libretro_root}/citra_libretro.cpp
    $<TARGET_OBJECTS:azahar_libretro_common>
)

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
)
# soh_lib is BUILDABLE from this tree (via the Shipwright add_subdirectory
# in wire_in.cmake — enables `cmake --build . --target soh_lib`) but NOT
# yet linked into soh3d_harness. Linking soh_lib PUBLIC-transitively
# drags libultraship + its vendored deps, which collide with Azahar's own
# externals: system libglslang.a vs Azahar/externals/glslang, and
# libZAPDLib.a linkage produces libstdc++ ABI-mismatch undefined refs to
# std::vector<char>::resize. Reconciling the two dependency graphs is the
# next task — pick one glslang, gate ZAPD out of soh_lib's transitive
# closure (it's only needed at asset extraction, not runtime).
if(ENABLE_VULKAN)
    target_link_libraries(soh3d_harness PRIVATE sirit vulkan-headers vma)
endif()
if(ENABLE_OPENGL)
    target_link_libraries(soh3d_harness PRIVATE glad)
endif()

if (SSE42_COMPILE_OPTION)
    target_compile_definitions(soh3d_harness PRIVATE CITRA_HAS_SSE42)
endif()
