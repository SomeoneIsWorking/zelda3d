# SoH3D dlist render harness — drives the Fast3D interpreter headlessly over a
# generated CMB->F3DEX2 model dlist (see dlist_harness.cpp).
#
# The models under test are the generated (ROM-derived, gitignored) sources in
# soh/src/soh3d. Each is compiled directly into the harness (raw Vtx/Gfx/tex
# arrays, no ResourceManager). Whichever ones exist are linked and exposed via
# --model <name>; the harness needs at least one.

set(SOH3D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../soh/src/soh3d")

set(SOH3D_MODEL_SRCS "")
set(SOH3D_MODEL_DEFS "")
foreach(m kibako pot gs hintstone geldwoman childlink)
    if(EXISTS "${SOH3D_DIR}/soh3d_${m}_model.c")
        list(APPEND SOH3D_MODEL_SRCS "${SOH3D_DIR}/soh3d_${m}_model.c")
        string(TOUPPER "${m}" M_UP)
        list(APPEND SOH3D_MODEL_DEFS "HAVE_${M_UP}")
    endif()
endforeach()

if(NOT SOH3D_MODEL_SRCS)
    message(WARNING "soh3d_dlist_harness: no soh3d_*_model.c found in ${SOH3D_DIR} — "
                    "regenerate with tools/cmb_to_c.py. Skipping harness target.")
    return()
endif()

add_executable(soh3d_dlist_harness
    dlist_harness.cpp
    ${SOH3D_MODEL_SRCS}
    "${SOH3D_DIR}/asset/ctr_rom.cpp"
    "${SOH3D_DIR}/asset/zar.cpp"
    "${SOH3D_DIR}/asset/cmb.cpp"
    "${SOH3D_DIR}/asset/csab.cpp"
    "${SOH3D_DIR}/asset/pica_texture.cpp"
    "${SOH3D_DIR}/soh3d_model.cpp"
)
target_compile_definitions(soh3d_dlist_harness PRIVATE ${SOH3D_MODEL_DEFS})

target_include_directories(soh3d_dlist_harness PRIVATE "${SOH3D_DIR}")
set_property(TARGET soh3d_dlist_harness PROPERTY CXX_STANDARD 20)
# Both defines are set PRIVATE on libultraship, so they don't propagate to us:
#  - F3DEX_GBI_2: the gbi.h opcode macros are selected at compile time and must
#    encode F3DEX2, or the interpreter walks the dlist with the wrong opcodes/stride.
#  - ENABLE_OPENGL: gates the GfxRenderingAPIOGL class declaration in gfx_opengl.h
#    (needed by the --gl real-rasteriser mode) and selects the GL_GLEXT_PROTOTYPES
#    desktop-GL include path.
target_compile_definitions(soh3d_dlist_harness PRIVATE F3DEX_GBI_2 ENABLE_OPENGL)
target_link_libraries(soh3d_dlist_harness PRIVATE libultraship)

# --gl mode renders through the real OpenGL backend in an EGL *surfaceless*
# context (no window / no X server). Link GLVND OpenGL + EGL directly (libultraship
# pulls these in PRIVATEly, so the symbols aren't re-exported to this executable).
find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
target_link_libraries(soh3d_dlist_harness PRIVATE OpenGL::OpenGL OpenGL::EGL)
