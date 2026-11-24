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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/out)

set(IMLUI_CONSUMER_MODE "subdirectory" CACHE STRING
    "How to add imlui: subdirectory, fetchcontent, find_package, fetchcontent_find")
set(IMLUI_SOURCE_DIR "" CACHE PATH "imlui's source folder (subdirectory, fetchcontent)")
set(IMLUI_GIT_REPOSITORY "" CACHE STRING "fetchcontent: a git URL to fetch instead of IMLUI_SOURCE_DIR")
set(IMLUI_GIT_TAG "HEAD" CACHE STRING "fetchcontent: the commit, tag or branch of IMLUI_GIT_REPOSITORY")

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
if(NOT imlui_VERSION VERSION_EQUAL 0.1.0)
    message(FATAL_ERROR "imlui_VERSION is '${imlui_VERSION}'")
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE imlui::imlui)
imlui_set_warnings(consumer)
imlui_set_hardening(consumer)
imlui_add_resources(consumer src/resources/resources.lua NAME consumer)
imlui_add_pak(consumer_data src/pak/resources.lua)
add_dependencies(consumer consumer_data)
