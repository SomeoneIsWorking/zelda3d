# Zelda3D dlist render harness — drives the Fast3D interpreter headlessly over a
# generated CMB->F3DEX2 model dlist (see dlist_harness.cpp).
#
# The models under test are the generated (ROM-derived, gitignored) sources in
# soh/src/zelda3d. Each is compiled directly into the harness (raw Vtx/Gfx/tex
# arrays, no ResourceManager). Whichever ones exist are linked and exposed via
# --model <name>; the harness needs at least one.

set(ZELDA3D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../soh/src/zelda3d")

set(ZELDA3D_MODEL_SRCS "")
set(ZELDA3D_MODEL_DEFS "")
foreach(m kibako pot gs hintstone geldwoman childlink)
    if(EXISTS "${ZELDA3D_DIR}/zelda3d_${m}_model.c")
        list(APPEND ZELDA3D_MODEL_SRCS "${ZELDA3D_DIR}/zelda3d_${m}_model.c")
        string(TOUPPER "${m}" M_UP)
        list(APPEND ZELDA3D_MODEL_DEFS "HAVE_${M_UP}")
    endif()
endforeach()

if(NOT ZELDA3D_MODEL_SRCS)
    message(STATUS "zelda3d_dlist_harness: no generated zelda3d_*_model.c fixtures found; "
                   "building the ROM-backed --zelda3d mode only")
endif()

add_executable(zelda3d_dlist_harness
    dlist_harness.cpp
    generic_dlist_fixture.cpp
    harness_options.cpp
    headless_window_backend.cpp
    ppm_output.cpp
    recording_rendering_api.cpp
    sdl3gpu_headless_environment.cpp
    zelda3d_dlist_fixture.cpp
    ${ZELDA3D_MODEL_SRCS}
    "${ZELDA3D_DIR}/model/zelda3d_model.cpp"
)
target_compile_definitions(zelda3d_dlist_harness PRIVATE ${ZELDA3D_MODEL_DEFS})

target_include_directories(zelda3d_dlist_harness PRIVATE "${ZELDA3D_DIR}")
set_property(TARGET zelda3d_dlist_harness PROPERTY CXX_STANDARD 20)
# Both defines are set PRIVATE on libultraship, so they don't propagate to us:
#  - F3DEX_GBI_2: the gbi.h opcode macros are selected at compile time and must
#    encode F3DEX2, or the interpreter walks the dlist with the wrong opcodes/stride.
#  - ENABLE_SDL3GPU: exposes the sole shipping renderer used by --gpu.
target_compile_definitions(zelda3d_dlist_harness PRIVATE F3DEX_GBI_2 ENABLE_SDL3GPU)
target_link_libraries(zelda3d_dlist_harness PRIVATE libultraship cmb3d)
