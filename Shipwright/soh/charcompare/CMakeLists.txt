# charcompare — standalone N64-vs-3DS character comparison tool.
#
# Embeds libultraship (Fast3D) for the N64 side and compiles the self-contained
# zelda3d asset/ parsers (no engine deps) for the 3DS side. Added via
# add_subdirectory(charcompare) at the END of soh/CMakeLists.txt, so the SDL2 /
# Threads imported targets that soh's find_package() created are already visible.
#
# Build:  cmake --build Shipwright/build-cmake --target charcompare
# (New files need a reconfigure: cmake Shipwright/build-cmake)

set(SOH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(charcompare
    main.cpp
    cc_3ds.cpp
    cc_n64.cpp
    cc_index.cpp                         # generated character index (charcompare_index.inc)
    cc_skeleton_stub.cpp                 # trimmed SOH::Skeleton (no game-coupled SkeletonPatcher)
    ${SOH_DIR}/soh/resource/importer/SkeletonFactory.cpp
    ${SOH_DIR}/soh/resource/importer/SkeletonLimbFactory.cpp
    ${SOH_DIR}/soh/resource/importer/AnimationFactory.cpp
    ${SOH_DIR}/soh/resource/importer/ArrayFactory.cpp
    ${SOH_DIR}/soh/resource/type/SkeletonLimb.cpp
    ${SOH_DIR}/soh/resource/type/Animation.cpp
    ${SOH_DIR}/soh/resource/type/Array.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/zelda3d/zelda3d_model.cpp
    # asset/ parsers now come from the shared cmb3d static lib (linked below).
)

target_include_directories(charcompare PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/zelda3d   # so zelda3d_model.cpp's local headers resolve
    ${CMAKE_CURRENT_SOURCE_DIR}/..             # so "soh/resource/..." resolves in the SOH factories
    ${CMAKE_CURRENT_SOURCE_DIR}/../include     # z64animation.h / z64math.h / libc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libultraship/include
)

# libultraship is PUBLIC-linked against ImGui + the GL backend, so linking it
# transitively gives us <imgui.h> and the SDL/GL headers/libs we need.
target_link_libraries(charcompare PRIVATE
    libultraship
    cmb3d                  # shared 3DS asset parsers (CMB/CSAB/ZAR/ZSI/CTR-ROM/…)
    SDL3::SDL3 # SDL3-MIGRATION
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# ENABLE_OPENGL guards the GL code paths we reuse. F3DEX_GBI_2 selects the gbi.h
# opcode macros to match the interpreter's loaded ucode (else gsSP* emit F3D-era
# opcodes the F3DEX2 interpreter rejects, runs off the end, and crashes).
target_compile_definitions(charcompare PRIVATE ENABLE_OPENGL F3DEX_GBI_2)

add_dependencies(charcompare libultraship)
