include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/cvars.cmake)

add_library(libultraship SHARED)
set_target_properties(libultraship PROPERTIES PREFIX "")
set_property(TARGET libultraship PROPERTY CXX_STANDARD 20)
# Need to set C11 for libgfxd (_Alignas)
set_property(TARGET libultraship PROPERTY C_STANDARD 11)
set_target_properties(libultraship PROPERTIES ENABLE_EXPORTS TRUE)
set_target_properties(libultraship PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)
option(USE_OPENGLES "Enable GLES3" OFF)
option(GFX_DEBUG_DISASSEMBLER "Enable libgfxd" OFF)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")
endif()

#=================== Top-Level ===================

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ship/install_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/ship/install_config.h @ONLY)

if(ENABLE_SCRIPTING)
    file(GLOB SCRIPT_PUB_KEYS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../keys/script/*.pem")
    set(EMBEDDED_KEYS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/ship/DefaultKeys.h)
    set(GENERATE_KEYS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_keys_header.py)
    get_filename_component(EMBEDDED_KEYS_DIR ${EMBEDDED_KEYS_HEADER} DIRECTORY)

    add_custom_command(
        OUTPUT ${EMBEDDED_KEYS_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EMBEDDED_KEYS_DIR}
        COMMAND Python3::Interpreter ${GENERATE_KEYS_SCRIPT}
                ${EMBEDDED_KEYS_HEADER}
                ${SCRIPT_PUB_KEYS}
        DEPENDS ${GENERATE_KEYS_SCRIPT}
                ${SCRIPT_PUB_KEYS}
        COMMENT "Generating DefaultKeys.h from all keys in /keys/script/"
        VERBATIM
    )

    add_custom_target(GenerateScriptKeys DEPENDS ${EMBEDDED_KEYS_HEADER})
    add_dependencies(libultraship GenerateScriptKeys)
endif()

#=================== Fast ===================

add_subdirectory("fast")

#=================== LUS ===================

add_subdirectory("libultraship")

#=================== Ship ===================

add_subdirectory("ship")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ship/utils/AppleFolderManager.mm PROPERTIES COMPILE_FLAGS -fno-objc-arc)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ship/utils/macUtils.mm PROPERTIES COMPILE_FLAGS -fno-objc-arc)
endif()

#=================== Packages & Includes ===================

target_include_directories(libultraship
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${INCLUDE_DIR} ${ADDITIONAL_LIB_INCLUDES}
)

#=================== Linking ===================
if(INCLUDE_MPQ_SUPPORT)
    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(libultraship PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,storm>")
    else()
        target_link_libraries(libultraship PUBLIC storm)
    endif()
endif()

# Dear ImGui is a real library again (see cmake/dependencies/common.cmake), so the no-op stub that
# stood in for it is gone. PUBLIC because the games' own dev-tool windows include imgui.h and must
# inherit both the include dirs and IMGUI_USER_CONFIG.
target_link_libraries(libultraship
    PUBLIC SDL3::SDL3 ImGui
)

# OpenGL was formerly linked transitively via the (removed) ImGui target; a couple of renderer
# paths (e.g. gfx_sdl3.cpp frame-dump glReadPixels) still need it. find_package(OpenGL) is in
# common.cmake.
if(OPENGL_opengl_LIBRARY)
    target_link_libraries(libultraship PUBLIC ${OPENGL_opengl_LIBRARY})
elseif(OPENGL_gl_LIBRARY)
    target_link_libraries(libultraship PUBLIC ${OPENGL_gl_LIBRARY})
endif()

target_link_libraries(libultraship PUBLIC RmlUi::RmlUi)

target_link_libraries(libultraship PRIVATE stb)

if (GFX_DEBUG_DISASSEMBLER)
    target_link_libraries(libultraship PRIVATE libgfxd)
endif()

find_package(libzip REQUIRED)
# PUBLIC, not PRIVATE. As a STATIC library the distinction did not matter -- PRIVATE deps still
# reached the final executable link. As a SHARED library it does: soh.elf then failed with
#   libultraship.so: undefined reference to symbol 'zip_file_add'
#   /usr/lib64/libzip.so.5: DSO missing from command line
# because ld will not resolve a DSO's reference through a library the consumer never names.
target_link_libraries(libultraship PUBLIC libzip::zip)

find_package(nlohmann_json REQUIRED)
target_link_libraries(libultraship PUBLIC nlohmann_json::nlohmann_json)

# inja — the template engine the Zelda3D GLSL shaders are written as (see
# fast/zelda3d_sdl3gpu.cpp). Chosen over fmt, which is already available here but cannot
# template GLSL: fmt treats { and } as its own delimiters, so every brace in ~400 lines of
# shader body would need escaping as {{ }}. inja's delimiters are {{ }}, {% %} and {# #},
# none of which occur anywhere in the generated shaders (verified: zero occurrences of all
# four). Header-only; uses the nlohmann_json already linked above rather than its embedded copy.
#
# inja is header-only, so we take its headers and NOT its CMake: its install() rules reference a
# bare `nlohmann_json` target, which does not exist here because we consume the imported
# `nlohmann_json::nlohmann_json` above (configure fails outright otherwise). SOURCE_SUBDIR pointed
# at a directory with no CMakeLists.txt is the standard way to fetch a header-only project without
# adding it as a subproject.
include(FetchContent)
FetchContent_Declare(
        inja
        GIT_REPOSITORY https://github.com/pantor/inja.git
        GIT_TAG v3.4.0
        SOURCE_SUBDIR  no-cmake-please
)
FetchContent_MakeAvailable(inja)
target_include_directories(libultraship PUBLIC ${inja_SOURCE_DIR}/single_include)

find_package(tinyxml2 REQUIRED)
target_link_libraries(libultraship PUBLIC tinyxml2::tinyxml2)

find_package(spdlog REQUIRED)
target_link_libraries(libultraship PUBLIC spdlog::spdlog)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(libultraship PRIVATE Threads::Threads)
    find_library(COCOA Cocoa)
    target_link_libraries(libultraship PRIVATE ${COCOA})

endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_Library(OSX_FOUNDATION Foundation)
    find_Library(OSX_AVFOUNDATION AVFoundation)
    find_library(METAL Metal)
    find_library(QUARTZCORE QuartzCore)
    find_library(COREAUDIO CoreAudio)
    find_library(AUDIOTOOLBOX AudioToolbox)
    target_link_libraries(libultraship PRIVATE ${OSX_FOUNDATION} ${OSX_AVFOUNDATION} ${METAL} ${QUARTZCORE} ${COREAUDIO} ${AUDIOTOOLBOX} ${CMAKE_DL_LIBS})
endif()

if (USE_OPENGLES)
    if (CMAKE_SYSTEM_NAME STREQUAL "Android")
        find_library(OPENGLES_LIBRARY GLESv3)
        target_link_libraries(libultraship PRIVATE GLESv3)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
        target_link_libraries(libultraship PRIVATE OpenGL::GLES2)
    else()
        find_library(OPENGLES_LIBRARY GLESv2)
        target_link_libraries(libultraship PRIVATE GLESv2)
    endif()
endif()

# ----- SDL3 GPU backend (zelda3d single-renderer migration, P4: the ONLY renderer) -----
# The Fast3D backend renders through SDL3's GPU API (FAST3D_SDL_GPU). OpenGL/DX11/Metal and the
# custom Vulkan backend were all removed (P4). The per-combiner shaders are still generated as GLSL
# at runtime and compiled to SPIR-V with glslang (SDL3 GPU consumes SPIR-V on its Vulkan/Metal/D3D12
# driver) — so glslang stays a hard dependency; the Vulkan loader / MoltenVK are NOT needed. Desktop
# Linux / macOS only (not GLES/iOS/Android).
if (NOT USE_OPENGLES AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    # find_package(glslang) exposes the imported targets glslang::glslang / glslang::SPIRV /
    # the default resource limits used by the runtime GLSL->SPIR-V compile.
    find_package(glslang QUIET)
    if (glslang_FOUND)
        target_link_libraries(libultraship PRIVATE
            glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
        target_compile_definitions(libultraship PRIVATE ENABLE_SDL3GPU)
        message(STATUS "zelda3d: SDL3 GPU backend ENABLED (glslang SPIR-V + SDL3 GPU API)")
    else()
        message(FATAL_ERROR "zelda3d: glslang not found; the SDL3 GPU backend (the only renderer) cannot build")
    endif()
endif()

target_link_libraries(libultraship PUBLIC prism monocypher)

if(ENABLE_SCRIPTING)
    target_link_libraries(libultraship PUBLIC libtcc libtcc1)
    target_compile_definitions(libultraship PUBLIC ENABLE_SCRIPTING)
endif()

#=================== Compile Options & Defs ===================

target_compile_definitions(libultraship PRIVATE ${GBI_UCODE})

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32
        _CONSOLE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
endif()

set(SPDLOG_LEVEL_TRACE 0)
set(SPDLOG_LEVEL_DEBUG 1)
set(SPDLOG_LEVEL_INFO 2)
set(SPDLOG_LEVEL_WARN 3)
set(SPDLOG_LEVEL_ERROR 4)
set(SPDLOG_LEVEL_CRITICAL 5)
set(SPDLOG_LEVEL_OFF 6)
set(LOG_LEVELS "SPDLOG_LEVEL_TRACE;SPDLOG_LEVEL_DEBUG;SPDLOG_LEVEL_INFO;SPDLOG_LEVEL_WARN;SPDLOG_LEVEL_ERROR;SPDLOG_LEVEL_CRITICAL;SPDLOG_LEVEL_OFF")
set(SPDLOG_MIN_CUTOFF SPDLOG_LEVEL_TRACE CACHE STRING "The minimum log level that will be available to a build. Can be overridden in cmake or in InitLogging")
set_property(CACHE SPDLOG_MIN_CUTOFF PROPERTY STRINGS ${LOG_LEVELS})
if(NOT SPDLOG_MIN_CUTOFF IN_LIST LOG_LEVELS)
    message(FATAL_ERROR "SPDLOG_MIN_CUTOFF must be one of ${LOG_LEVELS}")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(libultraship PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        __IOS__
    )

    set_xcode_property(${PROJECT_NAME} PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID} All)

    if(NOT SIGN_LIBRARY)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )

        if(TARGET SDL2)
            set_target_properties(SDL2 PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            )
        endif()

        set_target_properties(zip PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )
    endif()
else()
    target_compile_definitions(libultraship PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        $<$<BOOL:${USE_OPENGLES}>:USE_OPENGLES>
        $<$<BOOL:${GFX_DEBUG_DISASSEMBLER}>:GFX_DEBUG_DISASSEMBLER>
    )
endif()

if(MSVC)
    target_compile_options(libultraship PRIVATE
        $<$<CONFIG:Debug>:
            /Od;
            /Oi-
        >
        $<$<CONFIG:Release>:
            /Oi;
            /Gy
        >
        /permissive-;
        /MP;
        /sdl;
        /W3;
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        ${DEFAULT_CXX_EXCEPTION_HANDLING};
    )
    target_link_options(libultraship PRIVATE
        $<$<CONFIG:Release>:
            /OPT:REF;
            /OPT:ICF
        >
        /SUBSYSTEM:CONSOLE
    )
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU|Clang")
	target_compile_options(libultraship PRIVATE
		-Wall
		-Wextra
		-Wno-unused-variable
		-Wno-unused-parameter
		-Wno-unused-function
		-Wno-parentheses
		-Wno-missing-field-initializers
        $<$<COMPILE_LANGUAGE:C,OBJC>:-Wno-implicit-function-declaration>
        $<$<COMPILE_LANGUAGE:C,OBJC>:-Wno-int-conversion>
	)
    if (MSVC)
    target_compile_options(libultraship PRIVATE
        -Wno-implicit-function-declaration
    )
    endif()
endif()

