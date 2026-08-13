# Hook file passed to Azahar via CMAKE_PROJECT_citra_INCLUDE. CMake includes
# it right after `project(citra ...)` in Azahar/CMakeLists.txt — which is
# BEFORE Azahar's find_package(Threads) and add_subdirectory(src). We defer
# an include() of harness.cmake to the end of the top-level CMakeLists so
# every target the harness links against (Threads::Threads, citra_common,
# citra_core, azahar_libretro_common, ...) is already defined by the time
# our target is registered.
#
# This lets us wire the harness into Azahar's build without editing
# Azahar/src/CMakeLists.txt (Azahar/ is gitignored in this repo).
if(ENABLE_LIBRETRO)
    # Turn off LTO for the harness build. Azahar defaults ENABLE_LTO=ON in
    # release, which compiles citra_core / citra_common / video_core with
    # `-flto=auto -fno-fat-lto-objects` (LTO-only object files). Mixing
    # those with Shipwright's non-LTO static libraries triggers a linker
    # bug where COMDAT weak function definitions (e.g. the local
    # instantiation of std::vector<char>::resize inside ZAPDLib
    # MemoryStream.cpp.o) fail to resolve calls in the SAME .o. LTO is a
    # release-only knob anyway — for a dev harness that just needs to
    # boot, off is fine and much faster to iterate on.
    set(ENABLE_LTO OFF CACHE BOOL "harness: LTO mixed with non-LTO libs breaks link" FORCE)

    # Force both projects onto the SAME glslang. Azahar defaults to its
    # own externals/glslang subdirectory build, but Shipwright/libultraship
    # uses find_package(glslang) which picks up /usr/lib64/libglslang.a
    # (system Fedora libglslang-devel). Setting USE_SYSTEM_GLSLANG here —
    # before Azahar's externals/ subdirectory is processed — makes Azahar
    # also route through the system glslang, so only one copy flows into
    # the harness link line.
    set(USE_SYSTEM_GLSLANG ON CACHE BOOL "harness: share glslang with Shipwright" FORCE)

    # Bake the path into the deferred call — variables in DEFER args expand
    # at fire time, when CMAKE_CURRENT_LIST_DIR would refer to Azahar's own
    # top-level CMakeLists.txt, not this hook.
    set(_soh3d_harness_include "${CMAKE_CURRENT_LIST_DIR}/harness.cmake")
    cmake_language(EVAL CODE "
        cmake_language(DEFER
            DIRECTORY \"${CMAKE_SOURCE_DIR}\"
            CALL include [[${_soh3d_harness_include}]]
        )
    ")

    # The harness consumes the already-built shipping OoT core rather than compiling a second
    # private copy. harness.cmake declares that shared artifact as an imported target after every
    # Azahar dependency exists. This keeps the embedded SoH side byte-identical to the user-facing
    # build and avoids a duplicate multi-thousand-unit superbuild.
endif()
