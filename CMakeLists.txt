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
# calls), and defaultConfig() mounts those folders over the embedded copies, so editing src/resources/ or an app's
# scripts hot-reloads. OFF for a release: no source paths.
option(IMLUI_DEV_PATHS "Mount the source folders of embedded files over them (hot reload while editing)" ON)
option(IMLUI_BUILD_TESTS "Build the imlui unit and smoke tests" ${PROJECT_IS_TOP_LEVEL})

add_subdirectory(external)

# ------------------------------------------------------------------ library (imlui_lua, imlui: src/CMakeLists.txt)
add_subdirectory(src)

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
