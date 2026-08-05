# The launcher executable. Links libultraship only -- for the core ABI header and so the engine is
# already mapped when a core loads -- and never a game target: the whole point is that game code
# arrives by dlopen(RTLD_LOCAL), not by linking. A target_link_libraries line naming soh_lib or
# mm_lib here would defeat the design silently, by making one game's symbols global again.
add_executable(zelda3d_app zelda3d_main.cpp)

set_target_properties(zelda3d_app PROPERTIES OUTPUT_NAME "zelda3d")

target_link_libraries(zelda3d_app PRIVATE libultraship ${CMAKE_DL_LIBS})

# The cores are found relative to this binary (../soh/, ../mm/), so it has to be built one level
# above them rather than into the top of the build tree.
set_target_properties(zelda3d_app PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/zelda3d)

# Not a link dependency, but building the launcher alone and finding no cores is a useless artifact.
add_dependencies(zelda3d_app soh_core mm_core)
