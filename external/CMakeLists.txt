# Third-party code vendored into the project. Everything here is plain C/C++
# compiled as ordinary libraries; the rest of the project consumes it only
# through the C++20 module wrappers in src/wrap.

# ---------------------------------------------------------------- Lua 5.4 (C)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua)
# SYSTEM: our targets see the vendored headers as system headers, so our warning flags
# (cmake/ImluiWarnings.cmake) don't fire inside them. Same for every ext target below.
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C SYSTEM ON)
if(WIN32)
    target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_library(ext::lua ALIAS lua_static)

# ---------------------------------------------------------------- GLFW 3.4
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw EXCLUDE_FROM_ALL)
set_target_properties(glfw PROPERTIES SYSTEM ON)

# ---------------------------------------------------------------- Dear ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imgui_static STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imlui_imgui_assert.cpp
)
target_include_directories(imgui_static PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(imgui_static PUBLIC
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_USER_CONFIG="imlui_imconfig.h")
target_link_libraries(imgui_static PUBLIC glfw)
find_package(OpenGL REQUIRED)
target_link_libraries(imgui_static PUBLIC OpenGL::GL)
# Third-party code is not ours to scan for modules.
set_target_properties(imgui_static PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON)
add_library(ext::imgui ALIAS imgui_static)

# ---------------------------------------------------------------- spdlog
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH   OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)
set_target_properties(spdlog PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON)
add_library(ext::spdlog ALIAS spdlog)

# ---------------------------------------------------------------- GoogleTest 1.17.0
# Only googletest/ is vendored (no gmock, docs, samples or tests). Built only when
# something links it (the unit tests), with the dynamic CRT like the rest of the project.
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK            OFF CACHE BOOL "" FORCE)
add_subdirectory(googletest EXCLUDE_FROM_ALL)
# SYSTEM: consumers see gtest headers as system headers (no warnings from them).
set_target_properties(gtest gtest_main PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON)
# SYSTEM only helps consumers; gtest's own sources include its headers as user headers,
# and clang 21+ warns about a char8_t -> char32_t cast in gtest-printers.h.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-character-conversion IMLUI_HAS_WNO_CHARACTER_CONVERSION)
if(IMLUI_HAS_WNO_CHARACTER_CONVERSION)
    target_compile_options(gtest PRIVATE -Wno-character-conversion)
    target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
endif()

# ---------------------------------------------------------------- stb_image 2.34 (C)
# Only the PNG and JPEG decoders, decoding from memory (resources), with a size limit. The definitions are PUBLIC so
# the wrapper module (src/wrap/stb_image.cppm) sees the same declarations.
add_library(stb_image_static STATIC ${CMAKE_CURRENT_SOURCE_DIR}/imlui_stb_image.c)
target_include_directories(stb_image_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
target_compile_definitions(stb_image_static PUBLIC
    STBI_ONLY_PNG STBI_ONLY_JPEG STBI_NO_STDIO STBI_NO_LINEAR STBI_MAX_DIMENSIONS=16384)
set_target_properties(stb_image_static PROPERTIES LINKER_LANGUAGE C SYSTEM ON)
add_library(ext::stb_image ALIAS stb_image_static)
# ---------------------------------------------------------------- zstd 1.5.7
# Only lib/common, lib/compress and lib/decompress are vendored (no legacy formats, dictionary builder, programs
# or tests). The runtime (imlui) links only the decompressor; the build-time resource tools (tools/) also link the
# compressor. Single-threaded, no hand-written assembly (portable across the MSVC and GNU targets clang builds).
set(ZSTD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib)
file(GLOB ZSTD_COMMON_SOURCES CONFIGURE_DEPENDS ${ZSTD_DIR}/common/*.c)
file(GLOB ZSTD_DECOMPRESS_SOURCES CONFIGURE_DEPENDS ${ZSTD_DIR}/decompress/*.c)
file(GLOB ZSTD_COMPRESS_SOURCES CONFIGURE_DEPENDS ${ZSTD_DIR}/compress/*.c)
add_library(zstd_decompress STATIC ${ZSTD_COMMON_SOURCES} ${ZSTD_DECOMPRESS_SOURCES})
add_library(zstd_compress STATIC ${ZSTD_COMPRESS_SOURCES})
foreach(target zstd_decompress zstd_compress)
    target_include_directories(${target} PUBLIC ${ZSTD_DIR})
    target_compile_definitions(${target} PRIVATE ZSTD_DISABLE_ASM ZSTD_LEGACY_SUPPORT=0 ZSTD_MULTITHREAD_SUPPORT_DEFAULT=0)
    set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C SYSTEM ON)
endforeach()
target_link_libraries(zstd_compress PUBLIC zstd_decompress) # lib/common lives in the decompressor
add_library(ext::zstd ALIAS zstd_decompress)
add_library(ext::zstd_compress ALIAS zstd_compress)
