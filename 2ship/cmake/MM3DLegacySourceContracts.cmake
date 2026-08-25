include_guard(GLOBAL)

include("${ZELDA3D_SHARED_DIR}/cmake/LegacySourceContracts.cmake")

# Keep MM's imported decomp sources unchanged while checking the reset-function
# definitions against the focused lifecycle interfaces consumed by authored code.
function(mm3d_apply_legacy_source_contracts source_root)
    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/graph.c"
        HEADERS "${source_root}/src/code/graph_lifecycle.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/z_eventmgr.c"
        HEADERS "${source_root}/src/code/cutscene_manager_lifecycle.h")
endfunction()
