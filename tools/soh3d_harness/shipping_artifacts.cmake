# Locate one shipping core without assuming a platform suffix or target output
# directory. Multi-config trees prefer the harness configuration, while an
# unresolved duplicate is an error rather than an arbitrary Debug/Release pick.
function(zelda3d_find_shipping_artifact output build_root name prefix suffix configuration)
    file(GLOB_RECURSE candidates LIST_DIRECTORIES FALSE
        "${build_root}/${prefix}${name}${suffix}")
    list(FILTER candidates EXCLUDE REGEX "/CMakeFiles/")
    list(SORT candidates)

    if(configuration)
        set(config_candidates "")
        foreach(candidate IN LISTS candidates)
            if(candidate MATCHES "/${configuration}/${prefix}${name}${suffix}$")
                list(APPEND config_candidates "${candidate}")
            endif()
        endforeach()
        if(config_candidates)
            set(candidates ${config_candidates})
        endif()
    endif()

    list(LENGTH candidates count)
    if(count EQUAL 0)
        message(FATAL_ERROR
            "soh3d_harness needs ${prefix}${name}${suffix} under ${build_root}; "
            "build the shipping zelda3d_app target first or set "
            "ZELDA3D_SHIPPING_BUILD_DIR to its CMake build tree")
    endif()
    if(count GREATER 1)
        message(FATAL_ERROR
            "soh3d_harness found multiple ${prefix}${name}${suffix} artifacts under "
            "${build_root}: ${candidates}; select a single-config shipping build tree")
    endif()
    list(GET candidates 0 artifact)
    set(${output} "${artifact}" PARENT_SCOPE)
endfunction()
