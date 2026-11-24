cmake_minimum_required(VERSION 3.28)
project(imlui LANGUAGES C CXX)

# imlui is a library: an app adds this repository as a subproject (`add_subdirectory(vendor/imlui)`, see
# docs/getting-started.md) and gets the targets `imlui` and the resource tools, plus the CMake helpers
# imlui_add_resources, imlui_add_pak, imlui_set_warnings and imlui_set_hardening. Built on its own, it builds the
# library, the tools and the tests. Paths below are relative to this project (PROJECT_SOURCE_DIR,
# CMAKE_CURRENT_*), never to the top-level project.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "imlui is developed with clang; other compilers are untested.")
endif()

# Output folders and compile_commands.json belong to the top-level project; as a subproject they're its choice.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

# The helpers are functions, so once included here they're callable from the parent project too.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(ImluiWarnings)
include(ImluiSanitizers)
include(ImluiResources)

# Dev builds bake each embedded block's source folder into it (imlui_add_resources, also for the parent project's
# calls), and defaultConfig() mounts those folders over the embedded copies, so editing lua/ or an app's scripts
# hot-reloads. OFF for a release: no source paths.
option(IMLUI_DEV_PATHS "Mount the source folders of embedded files over them (hot reload while editing)" ON)
option(IMLUI_BUILD_TESTS "Build the imlui unit and smoke tests" ${PROJECT_IS_TOP_LEVEL})

add_subdirectory(external)

# ------------------------------------------------------------------ Lua wrapper (module `lua`)
# Its own library: the resource tools evaluate manifests with it, and imlui_rc can't depend on the imlui library,
# which embeds its own files with imlui_rc.
add_library(imlui_lua STATIC)
target_sources(imlui_lua
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS src
        FILES src/wrap/lua.cppm
)
target_link_libraries(imlui_lua PRIVATE ext::lua)
target_compile_features(imlui_lua PUBLIC cxx_std_20)
imlui_set_warnings(imlui_lua)
imlui_set_hardening(imlui_lua)

# ------------------------------------------------------------------ library
add_library(imlui STATIC)
file(GLOB_RECURSE IMLUI_MODULES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cppm)
list(REMOVE_ITEM IMLUI_MODULES ${PROJECT_SOURCE_DIR}/src/wrap/lua.cppm)
target_sources(imlui
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS src
        FILES ${IMLUI_MODULES}
)
target_link_libraries(imlui PUBLIC imlui_lua PRIVATE ext::lua ext::imgui ext::spdlog ext::stb_image ext::zstd)
target_compile_features(imlui PUBLIC cxx_std_20)
imlui_set_warnings(imlui)
imlui_set_hardening(imlui)
# The library carries its own files (imlui.embedded.imlui: lua/ at imlui:/ and the fonts at imlui:/fonts/, listed in
# resources.lua), so an app runs without them next to it; defaultConfig() always mounts them, under the .pak archives
# and folders that override them.
imlui_add_resources(imlui resources.lua NAME imlui)

# ------------------------------------------------------------------ resource tools (imlui_rc, imlui_pack)
add_subdirectory(tools)

# ------------------------------------------------------------------ tests
if(IMLUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------ clang-tidy (`--target tidy`, top level only)
if(PROJECT_IS_TOP_LEVEL)
    include(ImluiTidy)
endif()
