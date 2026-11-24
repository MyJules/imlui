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

add_subdirectory(external)

# ------------------------------------------------------------------ library
add_library(imlui STATIC)
file(GLOB_RECURSE IMLUI_MODULES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cppm)
target_sources(imlui
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS src
        FILES ${IMLUI_MODULES}
)
target_link_libraries(imlui PRIVATE ext::lua ext::imgui ext::spdlog ext::stb_image ext::zstd)
target_compile_features(imlui PUBLIC cxx_std_20)
imlui_set_warnings(imlui)
imlui_set_hardening(imlui)
# The library carries its own files (imlui.embedded.imlui, imlui.embedded.imlui_fonts), so an app runs without
# lua/ or fonts/ next to it; defaultConfig() mounts them under any folders and .pak archives that override them.
imlui_add_resources(imlui NAME imlui PREFIX imlui BASE lua DIRECTORY .)
imlui_add_resources(imlui NAME imlui_fonts PREFIX imlui MOUNT fonts BASE ${IMLUI_FONTS_DIR}
    FILES Roboto-Regular.ttf Roboto-Medium.ttf LICENSE-Roboto.txt
          MaterialIconsRound-Regular.otf MaterialIconsRound-Regular.codepoints LICENSE-MaterialIcons.txt)

# ------------------------------------------------------------------ resource tools (imlui_rc, imlui_pack)
add_subdirectory(tools)

# ------------------------------------------------------------------ app
add_executable(imlui_app app/main.cpp)
target_link_libraries(imlui_app PRIVATE imlui ext::lua ext::imgui ext::spdlog)
imlui_set_warnings(imlui_app)
imlui_set_hardening(imlui_app)
option(IMLUI_DEV_PATHS "Load lua/ and examples/ from the source tree (hot reload while editing)" ON)
if(IMLUI_DEV_PATHS)
    target_compile_definitions(imlui_app PRIVATE IMLUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# The gallery is the default app (no script argument): embedded, so the binary runs on its own. The Lua side and
# the fonts are embedded in the library. Every Lua-only example folder (one without a CMakeLists.txt, e.g.
# examples/counter) is copied next to the binary, so `imlui_app examples/counter/main.lua` works in a release
# build too. Examples with C++ embed their own files.
imlui_add_resources(imlui_app NAME gallery PREFIX app BASE examples/gallery DIRECTORY .)
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
