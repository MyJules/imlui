# Third-party code vendored into the project. Everything here is plain C/C++
# compiled as ordinary libraries; the rest of the project consumes it only
# through the C++20 module wrappers in src/wrap.
#
# Target names. imlui is often a subproject (add_subdirectory, FetchContent), so its vendored libraries must not take
# the names an app's own copies would use. Each library is a target named imlui_ext_<name>, reached through the alias
# ext::<name>; nothing else in imlui names them.
#   - Lua, ImGui, stb_image, zstd, spdlog and GoogleTest are built here from their sources (not through their own
#     CMakeLists.txt, which would define the targets `spdlog`, `gtest`, ...), so they never clash, whatever the app
#     adds before or after imlui.
#   - spdlog and GoogleTest: when the app already has them (the targets spdlog::spdlog, GTest::gtest_main exist when
#     imlui is added), imlui uses the app's copy instead of building its own, so an executable links one copy.
#   - GLFW is the exception: its CMakeLists.txt does a lot of platform work (X11 / Wayland protocol generation, Cocoa),
#     so it's added with add_subdirectory and keeps its upstream target name `glfw`. When the app already has a target
#     `glfw` (its own copy, or find_package(glfw3)), imlui uses it; an app that adds its own GLFW after imlui must
#     check `if(NOT TARGET glfw)` first. An executable can only hold one GLFW anyway (its C symbols).
# A library taken from the app can't be part of an installed imlui package: IMLUI_INSTALL=ON then stops the configure.
#
# The libraries the modules need (their headers are in the wrappers' global module fragments) are part of an installed
# package (cmake/ImluiInstall.cmake): exported as imlui::ext_<name>, their headers installed under
# ${IMLUI_INSTALL_EXT_INCLUDEDIR}/<name> (the INSTALL_INTERFACE include folders below), so an app that imports the
# installed modules rebuilds their BMIs with the same headers and definitions.
set(ext_include ${IMLUI_INSTALL_EXT_INCLUDEDIR})
find_package(Threads REQUIRED)

# imlui_ext_<name>: an INTERFACE target for the app's own copy of a library (<app_target>), its headers as system
# headers (our warning flags must not judge them; an IMPORTED target's already are, an app's own target's aren't).
function(_imlui_use_app_library name app_target)
    if(IMLUI_INSTALL AND NOT name MATCHES "^gtest") # GoogleTest is only for the tests: nothing installed needs it
        message(FATAL_ERROR "imlui: IMLUI_INSTALL=ON, but the project already has ${app_target}, which imlui would "
                            "use instead of its own copy, and an installed package can't carry the project's copy. "
                            "Install imlui from its own build.")
    endif()
    get_target_property(real ${app_target} ALIASED_TARGET)
    if(NOT real)
        set(real ${app_target})
    endif()
    add_library(imlui_ext_${name} INTERFACE)
    target_link_libraries(imlui_ext_${name} INTERFACE ${real})
    target_include_directories(imlui_ext_${name} SYSTEM INTERFACE
                               $<TARGET_PROPERTY:${real},INTERFACE_INCLUDE_DIRECTORIES>)
    add_library(ext::${name} ALIAS imlui_ext_${name})
    message(STATUS "imlui: using the project's ${app_target}")
endfunction()

# ---------------------------------------------------------------- Lua 5.4 (as C++)
# Compiled as C++ in one translation unit (lua_cxx/imlui_lua.cpp includes Lua's onelua.c): a Lua error is a C++
# exception (imlui_lua_error, lua_cxx/imlui_lua_error.h) instead of a longjmp, so the C++ frames it unwinds run their
# destructors. The Lua API therefore has C++ linkage: its headers are included without extern "C" (src/wrap/lua.cppm).
add_library(imlui_ext_lua STATIC ${CMAKE_CURRENT_SOURCE_DIR}/lua_cxx/imlui_lua.cpp)
target_include_directories(imlui_ext_lua PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lua;${CMAKE_CURRENT_SOURCE_DIR}/lua_cxx>"
    $<INSTALL_INTERFACE:${ext_include}/lua>)
# SYSTEM: our targets see the vendored headers as system headers, so our warning flags
# (cmake/ImluiWarnings.cmake) don't fire inside them. Same for every ext target below.
set_target_properties(imlui_ext_lua PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON EXPORT_NAME ext_lua)
if(WIN32)
    target_compile_definitions(imlui_ext_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
# Lua relies on signed integer arithmetic wrapping (e.g. a hex float's exponent, `0x1p99999999999`, in
# lua_strx2number), which -fwrapv makes defined; without it, UBSan (clang-asan, clang-fuzz) aborts on such source.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imlui_ext_lua PRIVATE -fwrapv)
endif()
add_library(ext::lua ALIAS imlui_ext_lua)

# ---------------------------------------------------------------- GLFW 3.4 (`glfw`, see the top of this file)
if(TARGET glfw)
    _imlui_use_app_library(glfw glfw)
else()
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory(glfw EXCLUDE_FROM_ALL)
    # Its own install rules are off; ours install its headers under the private folder instead of include/.
    set_target_properties(glfw PROPERTIES SYSTEM ON EXPORT_NAME ext_glfw)
    set_property(TARGET glfw PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/glfw/include> $<INSTALL_INTERFACE:${ext_include}/glfw>)
    add_library(ext::glfw ALIAS glfw)
endif()

# ---------------------------------------------------------------- SDL 3.4.20 (`SDL3-static`, see the top of this file)
# Windowing, input and events (src/wrap/window.cppm). Trimmed to what its CMake build needs: no test/, examples/,
# docs/, build-scripts/ or IDE projects. Only the static library, and only the parts imlui uses: video (with OpenGL)
# and events. The other subsystems (audio, GPU, the 2D renderer, camera, joystick and gamepad, haptic, HIDAPI, power,
# sensor, dialogs) are off; their functions still exist and fail (imgui_impl_sdl3 asks for gamepads and gets none).
if(TARGET SDL3::SDL3)
    _imlui_use_app_library(sdl3 SDL3::SDL3)
else()
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    foreach(option SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_INSTALL_DOCS
                   SDL_AUDIO SDL_GPU SDL_RENDER SDL_CAMERA SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR
                   SDL_DIALOG SDL_VULKAN SDL_XINPUT)
        set(${option} OFF CACHE BOOL "" FORCE)
    endforeach()
    # A GitHub archive has no REVISION.txt: without a revision SDL would ask git, which would describe imlui's checkout.
    set(SDL_REVISION "SDL-release-3.4.20" CACHE STRING "" FORCE)
    add_subdirectory(SDL EXCLUDE_FROM_ALL)
    # Its own install rules are off; ours install the library and its headers under the private folder instead.
    set_target_properties(SDL3-static PROPERTIES SYSTEM ON EXPORT_NAME ext_sdl3)
    if(WIN32) # its fallbacks to the CRT's itoa family, which the CRT headers mark deprecated
        target_compile_definitions(SDL3-static PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    endif()
    set_target_properties(SDL3_Headers PROPERTIES SYSTEM ON EXPORT_NAME ext_sdl3_headers)
    # The generated SDL_revision.h first, as SDL's own target has it.
    set(sdl_build_include ${CMAKE_CURRENT_BINARY_DIR}/SDL/include-revision ${CMAKE_CURRENT_SOURCE_DIR}/SDL/include)
    set_property(TARGET SDL3_Headers PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 "$<BUILD_INTERFACE:${sdl_build_include}>" $<INSTALL_INTERFACE:${ext_include}/SDL>)
    add_library(ext::sdl3 ALIAS SDL3-static)
endif()

# ---------------------------------------------------------------- Dear ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imlui_ext_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imlui_imgui_assert.cpp
)
target_include_directories(imlui_ext_imgui PUBLIC
    "$<BUILD_INTERFACE:${IMGUI_DIR};${IMGUI_DIR}/backends;${CMAKE_CURRENT_SOURCE_DIR}>"
    $<INSTALL_INTERFACE:${ext_include}/imgui>)
target_compile_definitions(imlui_ext_imgui PUBLIC
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_USER_CONFIG="imlui_imconfig.h")
target_link_libraries(imlui_ext_imgui PUBLIC ext::glfw)
find_package(OpenGL REQUIRED)
target_link_libraries(imlui_ext_imgui PUBLIC OpenGL::GL)
# Third-party code is not ours to scan for modules.
set_target_properties(imlui_ext_imgui PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON EXPORT_NAME ext_imgui)
add_library(ext::imgui ALIAS imlui_ext_imgui)

# ---------------------------------------------------------------- spdlog 1.15.3
# Built from its sources as spdlog's own CMakeLists.txt builds its static library with the default options: the
# compiled library (SPDLOG_COMPILED_LIB) with the bundled fmt.
if(TARGET spdlog::spdlog)
    _imlui_use_app_library(spdlog spdlog::spdlog)
else()
    set(SPDLOG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/spdlog)
    add_library(imlui_ext_spdlog STATIC
        ${SPDLOG_DIR}/src/spdlog.cpp ${SPDLOG_DIR}/src/stdout_sinks.cpp ${SPDLOG_DIR}/src/color_sinks.cpp
        ${SPDLOG_DIR}/src/file_sinks.cpp ${SPDLOG_DIR}/src/async.cpp ${SPDLOG_DIR}/src/cfg.cpp
        ${SPDLOG_DIR}/src/bundled_fmtlib_format.cpp
    )
    target_include_directories(imlui_ext_spdlog PUBLIC $<BUILD_INTERFACE:${SPDLOG_DIR}/include>
                                                       $<INSTALL_INTERFACE:${ext_include}/spdlog>)
    target_compile_definitions(imlui_ext_spdlog PUBLIC SPDLOG_COMPILED_LIB)
    target_link_libraries(imlui_ext_spdlog PUBLIC Threads::Threads)
    set_target_properties(imlui_ext_spdlog PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON EXPORT_NAME ext_spdlog)
    add_library(ext::spdlog ALIAS imlui_ext_spdlog)
endif()

# ---------------------------------------------------------------- GoogleTest 1.17.0
# Only googletest/ is vendored (no gmock, docs, samples or tests), and only the tests use it (IMLUI_BUILD_TESTS). Built
# from its sources (gtest-all.cc, gtest_main.cc) as its CMakeLists.txt builds them, with the project's C runtime.
if(IMLUI_BUILD_TESTS)
    if(TARGET GTest::gtest_main)
        _imlui_use_app_library(gtest_main GTest::gtest_main)
    else()
        set(GTEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/googletest/googletest)
        add_library(imlui_ext_gtest STATIC ${GTEST_DIR}/src/gtest-all.cc)
        add_library(imlui_ext_gtest_main STATIC ${GTEST_DIR}/src/gtest_main.cc)
        foreach(target imlui_ext_gtest imlui_ext_gtest_main)
            # gtest-all.cc includes src/*.cc relative to googletest/.
            target_include_directories(${target} PRIVATE ${GTEST_DIR})
            # SYSTEM only helps consumers; gtest's own sources include its headers as user headers.
            set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON)
        endforeach()
        target_include_directories(imlui_ext_gtest PUBLIC ${GTEST_DIR}/include)
        target_link_libraries(imlui_ext_gtest PUBLIC Threads::Threads)
        target_link_libraries(imlui_ext_gtest_main PUBLIC imlui_ext_gtest)
        # clang 21+ warns about a char8_t -> char32_t cast in gtest-printers.h.
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-Wno-character-conversion IMLUI_HAS_WNO_CHARACTER_CONVERSION)
        if(IMLUI_HAS_WNO_CHARACTER_CONVERSION)
            target_compile_options(imlui_ext_gtest PRIVATE -Wno-character-conversion)
            target_compile_options(imlui_ext_gtest_main PRIVATE -Wno-character-conversion)
        endif()
        add_library(ext::gtest_main ALIAS imlui_ext_gtest_main)
    endif()
endif()

# ---------------------------------------------------------------- stb_image 2.34 (C)
# Only the PNG and JPEG decoders, decoding from memory (resources), with a size limit. The definitions are PUBLIC so
# the wrapper module (src/wrap/stb_image.cppm) sees the same declarations.
add_library(imlui_ext_stb_image STATIC ${CMAKE_CURRENT_SOURCE_DIR}/imlui_stb_image.c)
target_include_directories(imlui_ext_stb_image PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>
                                                      $<INSTALL_INTERFACE:${ext_include}/stb>)
target_compile_definitions(imlui_ext_stb_image PUBLIC
    STBI_ONLY_PNG STBI_ONLY_JPEG STBI_NO_STDIO STBI_NO_LINEAR STBI_MAX_DIMENSIONS=16384)
set_target_properties(imlui_ext_stb_image PROPERTIES LINKER_LANGUAGE C SYSTEM ON EXPORT_NAME ext_stb_image)
add_library(ext::stb_image ALIAS imlui_ext_stb_image)
# ---------------------------------------------------------------- zstd 1.5.7
# Only lib/common, lib/compress and lib/decompress are vendored (no legacy formats, dictionary builder, programs
# or tests). The runtime (imlui) links only the decompressor; the build-time resource tools (tools/) also link the
# compressor. Single-threaded, no hand-written assembly (portable across the MSVC and GNU targets clang builds).
set(ZSTD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib)
file(GLOB ZSTD_COMMON_SOURCES CONFIGURE_DEPENDS ${ZSTD_DIR}/common/*.c)
file(GLOB ZSTD_DECOMPRESS_SOURCES CONFIGURE_DEPENDS ${ZSTD_DIR}/decompress/*.c)
file(GLOB ZSTD_COMPRESS_SOURCES CONFIGURE_DEPENDS ${ZSTD_DIR}/compress/*.c)
add_library(imlui_ext_zstd STATIC ${ZSTD_COMMON_SOURCES} ${ZSTD_DECOMPRESS_SOURCES})
add_library(imlui_ext_zstd_compress STATIC ${ZSTD_COMPRESS_SOURCES})
foreach(target imlui_ext_zstd imlui_ext_zstd_compress)
    target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${ZSTD_DIR}>
                                                $<INSTALL_INTERFACE:${ext_include}/zstd>)
    target_compile_definitions(${target} PRIVATE ZSTD_DISABLE_ASM ZSTD_LEGACY_SUPPORT=0 ZSTD_MULTITHREAD_SUPPORT_DEFAULT=0)
    set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C SYSTEM ON)
endforeach()
target_link_libraries(imlui_ext_zstd_compress PUBLIC imlui_ext_zstd) # lib/common lives in the decompressor
set_target_properties(imlui_ext_zstd PROPERTIES EXPORT_NAME ext_zstd)
add_library(ext::zstd ALIAS imlui_ext_zstd)
add_library(ext::zstd_compress ALIAS imlui_ext_zstd_compress)
