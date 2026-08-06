include(FetchContent)

find_package(OpenGL QUIET)

#=================== Dear ImGui ===================
# ImGui is the DEVELOPER-OVERLAY stack: console, save editor, heap/camera/process overlays, actor
# spawner. The shipped, game-facing UI is RmlUi (below). Two stacks on purpose -- see
# docs/dusklight-adoption.md; Dusklight, a mature port of the same shape, made the same split.
#
# This replaces a no-op header shim. The shim vendored these exact headers and linked a stub whose
# every function did nothing, which was fine under a dev-tool window and actively harmful anywhere a
# LIVE predicate read ImGui state: the stubs returned zeroed, never-null storage, so the predicate
# silently became a constant. Four shipped bugs had that one shape (mouse input blocked every frame,
# mouse buttons unbindable, rumble suppressed, floating windows never drawn) and none of them logged
# anything.
#
# The DOCKING branch, not master: SoH's dev UI is built around DockSpace()/viewports, and the master
# headers have neither. The tag below was verified byte-identical to the headers the shim vendored,
# so this is a fetch rather than a port -- nothing in-tree was compiled against a patched ImGui.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Upstream ships no CMakeLists, so the target is ours. Core + the C++ std::string helpers, plus the
# two backends matching this build: SDL3 for platform/input and SDL3-GPU for rendering, which is the
# only rendering backend this port has. imgui_demo.cpp is deliberately omitted.
add_library(ImGui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(ImGui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_config
)
# PUBLIC: every translation unit that includes imgui.h must see the same ImTextureID, or the ODR
# violation shows up as a link error in whichever file happened to be compiled differently.
target_compile_definitions(ImGui PUBLIC IMGUI_USER_CONFIG="zelda3d_imconfig.h")
target_link_libraries(ImGui PUBLIC SDL3::SDL3)

list(APPEND ADDITIONAL_LIB_INCLUDES
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_config
)

#=================== RmlUi ===================
# RmlUi provides the HTML/CSS-driven menu (Dusklight-style port). It needs a font
# engine; the default is FreeType, located via find_package(Freetype) -> the
# Freetype::Freetype target. On Linux/macOS we rely on the system FreeType
# (present here as freetype2). Windows/other platforms will need FreeType vendored
# before RmlUi can configure there -- deferred until the GL/Linux path lands.
#
# RmlUi defaults to a shared library (BUILD_SHARED_LIBS ON) and builds samples;
# force a static lib (single-binary deploy like the rest of libultraship's deps)
# and disable samples. We pull only Core + Debugger; the GL3/SDL backend sources
# under ${rmlui_SOURCE_DIR}/Backends are compiled into our own integration layer
# rather than by RmlUi itself.
set(RMLUI_SAMPLES OFF)
set(RMLUI_FONT_ENGINE "freetype")
set(RMLUI_LUA_BINDINGS OFF)
# The <svg> element. RmlUi 6.x ships an SVG plugin compiled into rmlui_core when this is ON; it is
# backed by lunasvg, which RmlUi's own CMake resolves (vcpkg/find_package, else FetchContent).
# Needed by the OoT/MM launcher document, whose per-game background art is vector — see
# ATTRIBUTION.md. Nothing else in the tree uses <svg>, so if lunasvg ever becomes a problem on a
# platform, the fallback is to rasterise the art to PNG and use <img> instead.
set(RMLUI_SVG_PLUGIN ON)
# RmlUi 6.2 does NOT vendor or fetch lunasvg -- it only find_package()s it and hard-errors if the
# lunasvg::lunasvg target is absent. So fetch it here, BEFORE RmlUi is made available, which leaves
# the target defined in the same directory scope for RmlUi's check to find. Pinned to the 2.x line
# because RmlUi 6.2 calls Document::boundingBox(), which is the 3.x API (2.x named it box()).
FetchContent_Declare(
    lunasvg
    GIT_REPOSITORY https://github.com/sammycage/lunasvg.git
    GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(lunasvg)
# Force RmlUi's sub-targets static regardless of the parent's BUILD_SHARED_LIBS.
set(rmlui_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
    RmlUi
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG 6.2
)
FetchContent_MakeAvailable(RmlUi)

set(BUILD_SHARED_LIBS "${rmlui_saved_build_shared_libs}")
list(APPEND ADDITIONAL_LIB_INCLUDES ${rmlui_SOURCE_DIR}/Include)

# ========= StormLib =============
if(INCLUDE_MPQ_SUPPORT)
    # StormLib is vendored in-tree (extern/StormLib) at upstream v9.25 with the
    # "bring back optimizations" change already applied to the source (a free-space
    # write cursor: lastFreeSpaceEntry). This replaces the former fetch-and-patch
    # via cmake/dependencies/patches/stormlib-optimizations.patch -- no download,
    # no fragile build-time git apply.
    FetchContent_Declare(
        StormLib
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/StormLib
    )
    FetchContent_MakeAvailable(StormLib)
    list(APPEND ADDITIONAL_LIB_INCLUDES ${stormlib_SOURCE_DIR}/src)
endif()

#=================== STB ===================
set(STB_DIR ${CMAKE_BINARY_DIR}/_deps/stb)
file(DOWNLOAD "https://github.com/nothings/stb/raw/0bc88af4de5fb022db643c2d8e549a0927749354/stb_image.h" "${STB_DIR}/stb_image.h")
file(WRITE "${STB_DIR}/stb_impl.c" "#define STB_IMAGE_IMPLEMENTATION\n#include \"stb_image.h\"")

add_library(stb STATIC)

target_sources(stb PRIVATE
    ${STB_DIR}/stb_image.h
    ${STB_DIR}/stb_impl.c
)

target_include_directories(stb PUBLIC ${STB_DIR})
list(APPEND ADDITIONAL_LIB_INCLUDES ${STB_DIR})

#=================== libgfxd ===================
if (GFX_DEBUG_DISASSEMBLER)
    FetchContent_Declare(
        libgfxd
        GIT_REPOSITORY https://github.com/glankk/libgfxd.git
        GIT_TAG 008f73dca8ebc9151b205959b17773a19c5bd0da
    )
    FetchContent_MakeAvailable(libgfxd)

    add_library(libgfxd STATIC)
    set_property(TARGET libgfxd PROPERTY C_STANDARD 11)

    target_sources(libgfxd PRIVATE
        ${libgfxd_SOURCE_DIR}/gbi.h
        ${libgfxd_SOURCE_DIR}/gfxd.h
        ${libgfxd_SOURCE_DIR}/priv.h
        ${libgfxd_SOURCE_DIR}/gfxd.c
        ${libgfxd_SOURCE_DIR}/uc.c
        ${libgfxd_SOURCE_DIR}/uc_f3d.c
        ${libgfxd_SOURCE_DIR}/uc_f3db.c
        ${libgfxd_SOURCE_DIR}/uc_f3dex.c
        ${libgfxd_SOURCE_DIR}/uc_f3dex2.c
        ${libgfxd_SOURCE_DIR}/uc_f3dexb.c
    )

    target_include_directories(libgfxd PUBLIC ${libgfxd_SOURCE_DIR})
endif()

#======== thread-pool ========
FetchContent_Declare(
    ThreadPool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG v4.1.0
)
FetchContent_MakeAvailable(ThreadPool)

list(APPEND ADDITIONAL_LIB_INCLUDES ${threadpool_SOURCE_DIR}/include)

#=========== prism ===========
option(PRISM_STANDALONE "Build prism as a standalone library" OFF)
FetchContent_Declare(
    prism
    GIT_REPOSITORY https://github.com/KiritoDv/prism-processor.git
    GIT_TAG 1de054450e7b3c5f777d2e3dfcb228ad120c329d
)
FetchContent_MakeAvailable(prism)

# prism's CMakeLists.txt calls cmake_minimum_required(VERSION <3.10), which causes
# CMake to explicitly set CMP0141=OLD in prism's scope (cmake_policy(VERSION) disables
# all policies newer than the specified version).  With CMP0141=OLD the compile
# template always appends "/Fd <target>.pdb /FS" to every compile command.  sccache
# parses the command line, sees "/Fd prism.pdb", and expects that file to exist after
# compilation.  When our /Z7 override wins (no PDB written) sccache aborts with
# "failed to open file prism.pdb".
#
# The simplest fix is to compile prism without sccache at all (clear the launcher).
# With the launcher cleared, cl.exe compiles prism directly; /Z7 (embedded debug info)
# is still applied so no PDB file is written and no race over a shared .pdb occurs.
if(MSVC AND TARGET prism)
    set_target_properties(prism PROPERTIES
        C_COMPILER_LAUNCHER   ""
        CXX_COMPILER_LAUNCHER ""
    )
    target_compile_options(prism PRIVATE $<$<CONFIG:Debug>:/Z7>)
endif()

#=========== monocypher ===========
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG 0d85f98c9d9b0227e42cf795cb527dff372b40a4
)
FetchContent_MakeAvailable(monocypher)

add_library(monocypher STATIC)
set_property(TARGET monocypher PROPERTY C_STANDARD 11)

target_sources(monocypher PRIVATE
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c
)

target_include_directories(monocypher PUBLIC 
    ${monocypher_SOURCE_DIR}/src
    ${monocypher_SOURCE_DIR}/src/optional
)

#=========== libtcc ===========
if(ENABLE_SCRIPTING)

FetchContent_Declare(
    tinycc
    GIT_REPOSITORY https://github.com/TinyCC/tinycc.git
    GIT_TAG        mob
)

FetchContent_MakeAvailable(tinycc)
if(NOT TARGET libtcc)
    if(NOT EXISTS "${tinycc_SOURCE_DIR}/config.h")
        message(STATUS "Configuring TinyCC to generate config.h...")
        if(WIN32)
            execute_process(
                COMMAND cmd /c build-tcc.bat -c cl
                WORKING_DIRECTORY "${tinycc_SOURCE_DIR}/win32"
                RESULT_VARIABLE tcc_config_result
            )
        else()
            execute_process(
                COMMAND ./configure
                WORKING_DIRECTORY "${tinycc_SOURCE_DIR}"
                RESULT_VARIABLE tcc_config_result
            )
        endif()

        if(NOT tcc_config_result EQUAL 0)
            message(WARNING "TinyCC configuration script returned non-zero. The build might fail.")
        endif()

        if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            message(STATUS "iOS target detected: Disabling CONFIG_CODESIGN...")
            file(APPEND "${tinycc_SOURCE_DIR}/config.h" "\n/* Force disable code signing for iOS cross-compilation */\n#undef CONFIG_CODESIGN\n")
        endif()
    endif()

    if(CMAKE_CROSSCOMPILING)
        find_program(HOST_C_COMPILER NAMES cc clang gcc REQUIRED)
        set(C2STR_EXE "${tinycc_BINARY_DIR}/tcc_c2str_host")
        if(CMAKE_HOST_WIN32)
            set(C2STR_EXE "${C2STR_EXE}.exe")
        endif()

        set(SIGN_COMMAND "")
        if(CMAKE_HOST_APPLE)
            set(SIGN_COMMAND COMMAND codesign -f -s - "${C2STR_EXE}")
        endif()

        add_custom_command(
            OUTPUT "${C2STR_EXE}"
            COMMAND ${CMAKE_COMMAND} -E env --unset=SDKROOT --unset=IPHONEOS_DEPLOYMENT_TARGET --unset=TVOS_DEPLOYMENT_TARGET
                    ${HOST_C_COMPILER} -DC2STR -o "${C2STR_EXE}" "${tinycc_SOURCE_DIR}/conftest.c"
            ${SIGN_COMMAND}
            DEPENDS "${tinycc_SOURCE_DIR}/conftest.c"
            COMMENT "Compiling host tool c2str natively..."
        )

        add_custom_command(
            OUTPUT "${tinycc_BINARY_DIR}/tccdefs_.h"
            COMMAND "${C2STR_EXE}" "${tinycc_SOURCE_DIR}/include/tccdefs.h" "${tinycc_BINARY_DIR}/tccdefs_.h"
            DEPENDS "${tinycc_SOURCE_DIR}/include/tccdefs.h" "${C2STR_EXE}"
            COMMENT "Generating tccdefs_.h for TinyCC (Cross-compiling)..."
        )
    else()
        add_executable(tcc_c2str "${tinycc_SOURCE_DIR}/conftest.c")
        target_compile_definitions(tcc_c2str PRIVATE C2STR)
        target_include_directories(tcc_c2str PRIVATE "${tinycc_SOURCE_DIR}")

        if(APPLE)
            set_target_properties(tcc_c2str PROPERTIES
                CODE_SIGNING_ALLOWED NO
                CODE_SIGNING_REQUIRED NO
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
                XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
            )
        endif()

        add_custom_command(
            OUTPUT "${tinycc_BINARY_DIR}/tccdefs_.h"
            COMMAND tcc_c2str "${tinycc_SOURCE_DIR}/include/tccdefs.h" "${tinycc_BINARY_DIR}/tccdefs_.h"
            DEPENDS "${tinycc_SOURCE_DIR}/include/tccdefs.h" tcc_c2str
            COMMENT "Generating tccdefs_.h for TinyCC..."
        )
    endif()

    # libtcc is LGPL; keep it as a shared library so consumers link against it
    # dynamically rather than incorporating it into their binary.
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    add_library(libtcc SHARED
        "${tinycc_SOURCE_DIR}/libtcc.c"
        "${tinycc_BINARY_DIR}/tccdefs_.h"
    )

    add_library(libtcc1 STATIC
        "${tinycc_SOURCE_DIR}/lib/libtcc1.c"
    )
    
    target_include_directories(libtcc1 PRIVATE 
        "${tinycc_SOURCE_DIR}"
        "${tinycc_BINARY_DIR}"
    )

    if(MSVC)
        if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            target_compile_definitions(libtcc1 PRIVATE __aarch64__ _WIN64)
            target_compile_definitions(libtcc  PRIVATE __aarch64__ TCC_TARGET_ARM64 _WIN64)
        else()
            target_compile_definitions(libtcc1 PRIVATE __x86_64__ _WIN64)
            target_compile_definitions(libtcc  PRIVATE __x86_64__ TCC_TARGET_X86_64 _WIN64)
        endif()
        target_compile_definitions(libtcc1 PRIVATE "__faststorefence=__faststorefence_tcc_unused")
        # MSVC's <assert.h> defines `__assert`, which collides with TCC's internal
        # `__assert` symbol. Rename TCC's use the same way `__faststorefence` is above.
        target_compile_definitions(libtcc PRIVATE "__assert=__assert_tcc_unused")
    endif()

    set(TCC_SAFE_INCLUDE_DIR "${tinycc_BINARY_DIR}/safe_include")
    configure_file(
        "${tinycc_SOURCE_DIR}/libtcc.h"
        "${TCC_SAFE_INCLUDE_DIR}/libtcc.h"
        COPYONLY
    )

    target_include_directories(libtcc PRIVATE
        "${tinycc_SOURCE_DIR}"
        "${tinycc_BINARY_DIR}"
    )
    target_include_directories(libtcc PUBLIC
        $<BUILD_INTERFACE:${TCC_SAFE_INCLUDE_DIR}>
    )

    if(ANDROID)
        target_link_libraries(libtcc PRIVATE dl m)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(libtcc PRIVATE dl m pthread)
    endif()
    
    set_target_properties(libtcc  PROPERTIES OUTPUT_NAME "tcc")
    set_target_properties(libtcc1 PROPERTIES OUTPUT_NAME "tcc1")

    if(APPLE)
        set_target_properties(libtcc libtcc1 PROPERTIES
            CODE_SIGNING_ALLOWED NO
            CODE_SIGNING_REQUIRED NO
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
        )
    endif()
endif()

endif() # ENABLE_SCRIPTING