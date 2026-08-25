include_guard(GLOBAL)

# CMake has no compiler-frontend-variant generator expression, and MSVC is
# true for both Microsoft's compiler and clang-cl, so branch on the
# configure-time fact instead. Source-file COMPILE_OPTIONS do not process
# SHELL: prefixes (target/directory level only), so the GCC-compatible arm
# passes "-include" and the header as two separate list items -- one argument
# each -- rather than one space-bearing string.
function(_zelda3d_c_forced_include_options_for_toolchain output compiler_id uses_msvc_frontend)
    set(options "")
    foreach(header IN LISTS ARGN)
        if(uses_msvc_frontend)
            list(APPEND options "/FI${header}")
        else()
            list(APPEND options "-include" "${header}")
        endif()
    endforeach()
    set(${output} "${options}" PARENT_SCOPE)
endfunction()

function(zelda3d_c_forced_include_options output)
    cmake_parse_arguments(OPTIONS "" "" "HEADERS" ${ARGN})
    if(OPTIONS_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "zelda3d_c_forced_include_options received unknown arguments: ${OPTIONS_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT OPTIONS_HEADERS)
        message(FATAL_ERROR "zelda3d_c_forced_include_options requires at least one HEADER")
    endif()

    _zelda3d_c_forced_include_options_for_toolchain(
        forced_include_options "${CMAKE_C_COMPILER_ID}" "${MSVC}" ${OPTIONS_HEADERS})
    set(${output} "${forced_include_options}" PARENT_SCOPE)
endfunction()

# Attach responsibility-owned C headers to a frozen imported C source without
# modifying that source's generated include boundary. Callers keep the mapping
# game-specific; this helper owns only compiler-portable forced-include wiring.
function(zelda3d_legacy_c_source_contract)
    cmake_parse_arguments(CONTRACT "" "SOURCE" "HEADERS" ${ARGN})

    if(CONTRACT_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "zelda3d_legacy_c_source_contract received unknown arguments: ${CONTRACT_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT CONTRACT_SOURCE)
        message(FATAL_ERROR "zelda3d_legacy_c_source_contract requires SOURCE")
    endif()
    if(NOT CONTRACT_HEADERS)
        message(FATAL_ERROR
            "zelda3d_legacy_c_source_contract requires at least one responsibility-owned HEADER")
    endif()

    get_filename_component(contract_source "${CONTRACT_SOURCE}" ABSOLUTE)
    if(NOT EXISTS "${contract_source}")
        message(FATAL_ERROR "legacy source contract names missing source: ${contract_source}")
    endif()

    get_property(existing_headers SOURCE "${contract_source}"
        PROPERTY ZELDA3D_LEGACY_CONTRACT_HEADERS)
    if(existing_headers)
        message(FATAL_ERROR "legacy source contract already exists for: ${contract_source}")
    endif()

    set(contract_headers "")
    foreach(contract_header IN LISTS CONTRACT_HEADERS)
        get_filename_component(contract_header "${contract_header}" ABSOLUTE)
        if(NOT EXISTS "${contract_header}")
            message(FATAL_ERROR "legacy source contract names missing header: ${contract_header}")
        endif()

        list(APPEND contract_headers "${contract_header}")
    endforeach()

    zelda3d_c_forced_include_options(contract_options HEADERS ${contract_headers})

    set_property(SOURCE "${contract_source}" APPEND
        PROPERTY COMPILE_OPTIONS ${contract_options})
    set_property(SOURCE "${contract_source}"
        PROPERTY ZELDA3D_LEGACY_CONTRACT_HEADERS "${contract_headers}")
    set_property(GLOBAL APPEND PROPERTY ZELDA3D_LEGACY_SOURCE_CONTRACTS
        "${contract_source}")
endfunction()
