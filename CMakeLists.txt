cmake_minimum_required(VERSION 3.28)
project(imlui VERSION 0.1.0 LANGUAGES C CXX)

# imlui is a library. An app consumes it in one of three ways (docs/getting-started.md, "Adding imlui to your
# project"): add_subdirectory (e.g. a git submodule in vendor/imlui), FetchContent, or find_package(imlui CONFIG) on
# an installed copy (cmake --install). Each gives the targets imlui::imlui, imlui::imlui_rc and imlui::imlui_pack and
# the CMake helpers imlui_add_resources, imlui_add_pak, imlui_set_warnings and imlui_set_hardening. Built on its own,
# it builds the library, the tools and the tests. Paths below are relative to this project (PROJECT_SOURCE_DIR,
# CMAKE_CURRENT_*), never to the top-level project, and nothing here sets global flags.

# project() sets imlui_VERSION in this folder only. A parent that added imlui (add_subdirectory, or FetchContent, which
# adds it from inside a function) sees it through the cache, as it would see find_package's.
if(NOT PROJECT_IS_TOP_LEVEL)
    set(imlui_VERSION ${imlui_VERSION} CACHE INTERNAL "imlui's version, for the project that added it")
endif()

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

# The helpers are functions, so once included here they're callable from the parent project too. ImluiResources also
# defines the options IMLUI_DEV_PATHS and IMLUI_EMBED_RESOURCES (the installed package's config includes it too).
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(ImluiWarnings)
include(ImluiSanitizers)
include(ImluiResources)
include(ImluiLuaDefinitions) # imlui_add_lua_definitions: an app's editor definitions as a build step
include(ImluiStd) # imlui_add_std_module: the module `std` every imlui module imports (src/CMakeLists.txt)

option(IMLUI_BUILD_TESTS "Build the imlui unit and smoke tests" ${PROJECT_IS_TOP_LEVEL})
# Install rules and the CMake package (find_package(imlui CONFIG), cmake/ImluiInstall.cmake). An installed library
# can't read its source folders, so with IMLUI_INSTALL=ON an embedded copy of the library's own files is built for the
# install (imlui_files_install, src/CMakeLists.txt), whatever IMLUI_EMBED_RESOURCES says; the build itself (the tests,
# the tools) still follows IMLUI_EMBED_RESOURCES, so a dev build doesn't rebuild the library when they change.
option(IMLUI_INSTALL "Add imlui's install rules and CMake package (an embedded copy of its files for the install)"
       ${PROJECT_IS_TOP_LEVEL})
# Where an install puts the vendored headers the modules include (their global module fragments), relative to the
# prefix: their targets' INSTALL_INTERFACE include folders (external/CMakeLists.txt) and the install rules agree on it.
# The module interface units go to IMLUI_INSTALL_MODULEDIR (src/CMakeLists.txt).
include(GNUInstallDirs)
set(IMLUI_INSTALL_EXT_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/imlui/ext)
set(IMLUI_INSTALL_MODULEDIR ${CMAKE_INSTALL_INCLUDEDIR}/imlui/modules)

add_subdirectory(external)

# ------------------------------------------------------------------ library (imlui, imlui_core, ...: src/)
add_subdirectory(src)

# ------------------------------------------------------------------ resource tools (imlui_rc, imlui_pack)
add_subdirectory(tools)

# ------------------------------------------------------------------ install rules and package config
if(IMLUI_INSTALL)
    include(ImluiInstall)
endif()

# ------------------------------------------------------------------ tests
if(IMLUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------ clang-tidy (`--target tidy`, top level only)
if(PROJECT_IS_TOP_LEVEL)
    include(ImluiTidy)
endif()
