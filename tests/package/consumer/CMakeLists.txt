cmake_minimum_required(VERSION 3.28)
project(imlui_consumer LANGUAGES C CXX)

# A minimal app that consumes imlui the way an app does, in one of the ways docs/getting-started.md describes
# ("Adding imlui to your project"). Configured, built and run by the package tests (tests/package/run.cmake):
#
#   IMLUI_CONSUMER_MODE  subdirectory      add_subdirectory(${IMLUI_SOURCE_DIR})
#                        fetchcontent      FetchContent_Declare(imlui SOURCE_DIR ${IMLUI_SOURCE_DIR}), or
#                                          GIT_REPOSITORY ${IMLUI_GIT_REPOSITORY} GIT_TAG ${IMLUI_GIT_TAG} when set
#                        find_package      find_package(imlui 0.1 CONFIG REQUIRED) (CMAKE_PREFIX_PATH: the install)
#                        fetchcontent_find FetchContent_Declare(... FIND_PACKAGE_ARGS 0.1 CONFIG): must find the
#                                          installed package instead of building the source
#
# It sets its own output folder (out/), as an app may, and uses every helper: imlui_set_warnings/_hardening,
# imlui_add_resources (src/resources/, module imlui.embedded.consumer) and imlui_add_pak (src/pak/, consumer.pak).
#
#   IMLUI_CONSUMER_OWN_DEPS  ON: the app has its own SDL3 and spdlog (imlui's vendored sources stand in for the app's
#                            copies), with their upstream target names: SDL3 added before imlui (imlui must use it,
#                            not add a second `SDL3-static`) and spdlog after it (imlui's own copy must not be named
#                            `spdlog`). See external/CMakeLists.txt.
#   IMLUI_SANITIZERS         as for imlui: the app sets nothing else; its targets get the flags by linking imlui.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/out)

set(IMLUI_CONSUMER_MODE "subdirectory" CACHE STRING
    "How to add imlui: subdirectory, fetchcontent, find_package, fetchcontent_find")
set(IMLUI_SOURCE_DIR "" CACHE PATH "imlui's source folder (subdirectory, fetchcontent)")
set(IMLUI_GIT_REPOSITORY "" CACHE STRING "fetchcontent: a git URL to fetch instead of IMLUI_SOURCE_DIR")
set(IMLUI_GIT_TAG "HEAD" CACHE STRING "fetchcontent: the commit, tag or branch of IMLUI_GIT_REPOSITORY")
option(IMLUI_CONSUMER_OWN_DEPS "Add the app's own SDL3 (before imlui) and spdlog (after imlui)" OFF)

if(IMLUI_CONSUMER_OWN_DEPS)
    # As an app would add SDL3 as a static library: its subsystems are left as they are.
    set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_REVISION "SDL-release-3.4.20" CACHE STRING "" FORCE)
    add_subdirectory(${IMLUI_SOURCE_DIR}/external/SDL app_sdl EXCLUDE_FROM_ALL)
endif()

if(IMLUI_CONSUMER_MODE STREQUAL "subdirectory")
    add_subdirectory(${IMLUI_SOURCE_DIR} imlui)
elseif(IMLUI_CONSUMER_MODE STREQUAL "fetchcontent")
    include(FetchContent)
    if(IMLUI_GIT_REPOSITORY)
        FetchContent_Declare(imlui GIT_REPOSITORY ${IMLUI_GIT_REPOSITORY} GIT_TAG ${IMLUI_GIT_TAG})
    else()
        FetchContent_Declare(imlui SOURCE_DIR ${IMLUI_SOURCE_DIR})
    endif()
    FetchContent_MakeAvailable(imlui)
elseif(IMLUI_CONSUMER_MODE STREQUAL "find_package")
    find_package(imlui 0.1 CONFIG REQUIRED)
elseif(IMLUI_CONSUMER_MODE STREQUAL "fetchcontent_find")
    include(FetchContent)
    FetchContent_Declare(imlui SOURCE_DIR ${IMLUI_SOURCE_DIR} FIND_PACKAGE_ARGS 0.1 CONFIG)
    FetchContent_MakeAvailable(imlui)
    get_target_property(imported imlui::imlui IMPORTED)
    if(NOT imported)
        message(FATAL_ERROR "FetchContent built imlui from source instead of finding the installed package")
    endif()
else()
    message(FATAL_ERROR "unknown IMLUI_CONSUMER_MODE '${IMLUI_CONSUMER_MODE}'")
endif()
message(STATUS "imlui consumer: ${IMLUI_CONSUMER_MODE}, imlui ${imlui_VERSION}")
if(IMLUI_CONSUMER_OWN_DEPS)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)
    add_subdirectory(${IMLUI_SOURCE_DIR}/external/spdlog app_spdlog EXCLUDE_FROM_ALL)
    if(NOT TARGET imlui_ext_sdl3) # imlui's wrapper around the app's SDL3 (external/CMakeLists.txt)
        message(FATAL_ERROR "imlui didn't use the app's SDL3")
    endif()
endif()
if(NOT imlui_VERSION VERSION_EQUAL 0.1.0)
    message(FATAL_ERROR "imlui_VERSION is '${imlui_VERSION}'")
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE imlui::imlui)
if(IMLUI_CONSUMER_OWN_DEPS)
    target_link_libraries(consumer PRIVATE SDL3::SDL3 spdlog::spdlog) # one SDL in the executable: the app's
endif()
imlui_set_warnings(consumer)
imlui_set_hardening(consumer)
imlui_add_resources(consumer src/resources/resources.lua NAME consumer)
imlui_add_pak(consumer_data src/pak/resources.lua)
add_dependencies(consumer consumer_data)
