if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "libultraship's Windows build requires the user-supplied vcpkg toolchain")
endif()
