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
target_link_libraries(imlui PRIVATE ext::lua ext::imgui ext::spdlog)
target_compile_features(imlui PUBLIC cxx_std_20)
imlui_set_warnings(imlui)
imlui_set_hardening(imlui)

# ------------------------------------------------------------------ app
add_executable(imlui_app app/main.cpp)
target_link_libraries(imlui_app PRIVATE imlui ext::lua ext::imgui ext::spdlog)
imlui_set_warnings(imlui_app)
imlui_set_hardening(imlui_app)
option(IMLUI_DEV_PATHS "Load lua/ and examples/ from the source tree (hot reload while editing)" ON)
if(IMLUI_DEV_PATHS)
    target_compile_definitions(imlui_app PRIVATE IMLUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# Ship the Lua side (DSL + examples) and the default font next to the binary.
add_custom_command(TARGET imlui_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lua $<TARGET_FILE_DIR:imlui_app>/lua
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples $<TARGET_FILE_DIR:imlui_app>/examples
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${IMLUI_FONTS_DIR} $<TARGET_FILE_DIR:imlui_app>/fonts
)

# ------------------------------------------------------------------ example: C++ types
# Registers C++ element types and a singleton (examples/custom_types/*.cppm) and runs
# examples/custom_types.lua. See docs/extending.md.
add_executable(imlui_custom_types examples/custom_types/main.cpp)
target_sources(imlui_custom_types
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS examples/custom_types
        FILES examples/custom_types/gauge.cppm examples/custom_types/system_info.cppm
)
target_link_libraries(imlui_custom_types PRIVATE imlui ext::lua ext::imgui ext::spdlog)
imlui_set_warnings(imlui_custom_types)
imlui_set_hardening(imlui_custom_types)
if(IMLUI_DEV_PATHS)
    target_compile_definitions(imlui_custom_types PRIVATE IMLUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()
add_custom_command(TARGET imlui_custom_types POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lua $<TARGET_FILE_DIR:imlui_custom_types>/lua
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples $<TARGET_FILE_DIR:imlui_custom_types>/examples
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${IMLUI_FONTS_DIR} $<TARGET_FILE_DIR:imlui_custom_types>/fonts
)

# ------------------------------------------------------------------ tests
option(IMLUI_BUILD_TESTS "Build unit tests" ON)
if(IMLUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
