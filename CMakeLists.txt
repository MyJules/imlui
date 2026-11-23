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
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(imlui PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ------------------------------------------------------------------ app
add_executable(imlui_app app/main.cpp)
target_link_libraries(imlui_app PRIVATE imlui ext::lua ext::imgui ext::spdlog)
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
