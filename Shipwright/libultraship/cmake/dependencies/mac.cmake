include(FetchContent)

#=================== spdlog ===================
# macports has issues with this because of fmt
# brew doesn't support building multiarch
find_package(spdlog QUIET)
if (NOT ${spdlog_FOUND})
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.16.0
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

#=================== Metal-cpp ===================
FetchContent_Declare(
    metalcpp
    GIT_REPOSITORY https://github.com/briaguya-ai/single-header-metal-cpp.git
    GIT_TAG macOS13_iOS16
)
FetchContent_MakeAvailable(metalcpp)
list(APPEND ADDITIONAL_LIB_INCLUDES ${metalcpp_SOURCE_DIR})

#=================== SDL3 ===================
# ImGui is now a header-only no-op shim (see common.cmake); the Metal ImGui backend and its GLEW
# dependency are gone with it. SDL3 was formerly carried transitively by the ImGui target; find it
# here so src/CMakeLists.txt can link it directly onto libultraship. (Linking SDL2 here previously
# pulled a second SDL into soh's link graph and broke configuration via SDL_VERSION mismatch — using
# SDL3 here finishes the SDL2 eradication on macOS.)
find_package(SDL3 REQUIRED)
