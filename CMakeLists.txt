cmake_minimum_required(VERSION 3.28)
project(imlui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "imlui is developed with clang; other compilers are untested.")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(ImluiWarnings)
include(ImluiSanitizers)
include(ImluiResources)

# Dev builds bake each embedded block's source folder into it (imlui_add_resources), and defaultConfig() mounts those
# folders over the embedded copies, so editing lua/ or an example hot-reloads. OFF for a release: no source paths.
option(IMLUI_DEV_PATHS "Mount the source folders of embedded files over them (hot reload while editing)" ON)

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
file(GLOB_RECURSE IMLUI_MODULES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cppm)
list(REMOVE_ITEM IMLUI_MODULES ${CMAKE_SOURCE_DIR}/src/wrap/lua.cppm)
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

# ------------------------------------------------------------------ app
add_executable(imlui_app app/main.cpp)
target_link_libraries(imlui_app PRIVATE imlui ext::lua ext::imgui ext::spdlog)
imlui_set_warnings(imlui_app)
imlui_set_hardening(imlui_app)

# The gallery is the app (app:/main.lua) when no script is given: embedded, so the binary runs on its own; in dev
# builds its folder is mounted over the embedded copy (hot reload). The Lua side and the fonts are embedded in the
# library. Every Lua-only example folder (one without a CMakeLists.txt, e.g. examples/counter) is copied next to the
# binary: a relative script argument missing from the current folder is looked up next to the executable, so
# `imlui_app examples/counter/main.lua` works from anywhere. Examples with C++ embed their own files.
imlui_add_resources(imlui_app examples/gallery/resources.lua NAME gallery)
file(GLOB IMLUI_EXAMPLE_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/examples/*)
foreach(dir IN LISTS IMLUI_EXAMPLE_DIRS)
    if(IS_DIRECTORY ${dir} AND NOT EXISTS ${dir}/CMakeLists.txt)
        cmake_path(GET dir FILENAME name)
        add_custom_command(TARGET imlui_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${dir} $<TARGET_FILE_DIR:imlui_app>/examples/${name}
        )
    endif()
endforeach()

# ------------------------------------------------------------------ examples with C++ (examples/<name>/CMakeLists.txt)
option(IMLUI_BUILD_EXAMPLES "Build the example executables that register C++ types" ON)
if(IMLUI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ------------------------------------------------------------------ clang-tidy (`--target tidy`)
include(ImluiTidy)

# ------------------------------------------------------------------ tests
option(IMLUI_BUILD_TESTS "Build unit tests" ON)
if(IMLUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
