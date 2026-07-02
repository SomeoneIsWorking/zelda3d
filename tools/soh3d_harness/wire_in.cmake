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

    # Pull in Shipwright so soh_lib (and its transitive deps: libultraship,
    # cmb3d, zelda3d_shared, ZAPDTR, OTRExporter) becomes a target the
    # deferred harness.cmake can link against. This runs while Azahar is
    # the top-level project — ${CMAKE_SOURCE_DIR} inside Shipwright's
    # CMakeLists will resolve to Azahar's root, which is fine as long as
    # we only build harness-reachable targets (the asset-copy / install
    # / packaging commands that reference CMAKE_SOURCE_DIR are all attached
    # to soh.elf or top-level install() and won't fire when we build the
    # harness target).
    set(_shipwright_root "${CMAKE_CURRENT_LIST_DIR}/../../Shipwright")
    if(EXISTS "${_shipwright_root}/CMakeLists.txt")
        add_subdirectory("${_shipwright_root}" "${CMAKE_BINARY_DIR}/shipwright" EXCLUDE_FROM_ALL)
    endif()
endif()
