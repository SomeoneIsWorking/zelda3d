cmake_minimum_required(VERSION 3.26)

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${repo_root}/Shipwright/zelda3d_shared/cmake/LegacySourceContracts.cmake")

set(headers "/contract/first.h" "/contract/second.h")

function(assert_forced_include_contract compiler_id uses_msvc_frontend)
    _zelda3d_c_forced_include_options_for_toolchain(
        actual "${compiler_id}" "${uses_msvc_frontend}" ${headers})

    if(uses_msvc_frontend)
        set(expected
            "$<$<BOOL:TRUE>:/FI/contract/first.h>"
            "$<$<NOT:$<BOOL:TRUE>>:SHELL:-include /contract/first.h>"
            "$<$<BOOL:TRUE>:/FI/contract/second.h>"
            "$<$<NOT:$<BOOL:TRUE>>:SHELL:-include /contract/second.h>")
    else()
        set(expected
            "$<$<BOOL:FALSE>:/FI/contract/first.h>"
            "$<$<NOT:$<BOOL:FALSE>>:SHELL:-include /contract/first.h>"
            "$<$<BOOL:FALSE>:/FI/contract/second.h>"
            "$<$<NOT:$<BOOL:FALSE>>:SHELL:-include /contract/second.h>")
    endif()

    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "${compiler_id}/${uses_msvc_frontend} forced-include contract mismatch: ${actual}")
    endif()
endfunction()

assert_forced_include_contract("MSVC" TRUE)
assert_forced_include_contract("GNU" FALSE)
assert_forced_include_contract("Clang" FALSE)
assert_forced_include_contract("Clang" TRUE)
assert_forced_include_contract("AppleClang" FALSE)

set(ZELDA3D_SHARED_DIR "${repo_root}/Shipwright/zelda3d_shared")
include("${repo_root}/Shipwright/soh/CMake/Zelda3DLegacySourceContracts.cmake")
include("${repo_root}/2ship/cmake/MM3DLegacySourceContracts.cmake")
soh_apply_zelda3d_legacy_source_contracts("${repo_root}/Shipwright/soh")
mm3d_apply_legacy_source_contracts("${repo_root}/2ship")

get_property(contract_sources GLOBAL PROPERTY ZELDA3D_LEGACY_SOURCE_CONTRACTS)
list(LENGTH contract_sources contract_source_count)
if(NOT contract_source_count EQUAL 66)
    message(FATAL_ERROR
        "legacy source contract inventory changed without updating its test: ${contract_source_count}")
endif()

foreach(contract_source IN LISTS contract_sources)
    get_property(contract_headers SOURCE "${contract_source}"
        PROPERTY ZELDA3D_LEGACY_CONTRACT_HEADERS)
    get_property(contract_options SOURCE "${contract_source}" PROPERTY COMPILE_OPTIONS)
    foreach(contract_header IN LISTS contract_headers)
        string(FIND "${contract_options}" "${contract_header}" contract_header_index)
        if(contract_header_index EQUAL -1)
            message(FATAL_ERROR
                "${contract_source} does not force its declared header: ${contract_header}")
        endif()
    endforeach()
endforeach()

message(STATUS "legacy source compiler contracts passed")
