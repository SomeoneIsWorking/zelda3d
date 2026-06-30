#=================== SDL3 ===================
# SDL3 was formerly pulled into libultraship transitively via the ImGui target. ImGui is now a
# header-only no-op shim (see common.cmake), so find SDL3 here and link it directly onto the
# libultraship target (done in src/CMakeLists.txt).
find_package(SDL3 REQUIRED)
