# The `tidy` target: runs clang-tidy (the naming checks in .clang-tidy) on every module in src/ and tools/ (and the
# implementation unit src/imlui/app/default_config.cpp) and on the test host (tests/smoke_host/main.cpp), and fails on
# any finding (--warnings-as-errors=*). Only when imlui is the top-level project (CMakeLists.txt): an app has its own
# checks.
#
#   cmake --build build/clang-debug --target tidy
#
# clang-tidy reads the compile commands of this build (CMAKE_EXPORT_COMPILE_COMMANDS) and the module BMIs they
# reference, so the target depends on the targets that build them: the BMIs are current when it runs. Each file is
# its own command, so Ninja runs them in parallel; a file is checked again when it, or any module (the library),
# changed. See docs/development.md, "clang-tidy".
find_program(IMLUI_CLANG_TIDY NAMES clang-tidy)
if(NOT IMLUI_CLANG_TIDY)
    message(STATUS "clang-tidy not found: no `tidy` target")
    return()
endif()

file(GLOB_RECURSE IMLUI_TIDY_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cppm ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE IMLUI_TIDY_TOOL_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tools/*.cppm)
list(APPEND IMLUI_TIDY_FILES ${IMLUI_TIDY_TOOL_FILES})
set(IMLUI_TIDY_DEPENDS imlui imlui_pack)
if(TARGET imlui_smoke_host)
    list(APPEND IMLUI_TIDY_FILES ${PROJECT_SOURCE_DIR}/tests/smoke_host/main.cpp)
    list(APPEND IMLUI_TIDY_DEPENDS imlui_smoke_host)
endif()

set(IMLUI_TIDY_STAMPS)
foreach(file IN LISTS IMLUI_TIDY_FILES)
    file(RELATIVE_PATH rel ${PROJECT_SOURCE_DIR} ${file})
    set(stamp ${PROJECT_BINARY_DIR}/tidy/${rel}.stamp)
    add_custom_command(
        OUTPUT ${stamp}
        COMMAND ${IMLUI_CLANG_TIDY} -p ${PROJECT_BINARY_DIR} --quiet --warnings-as-errors=* ${file}
        COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
        DEPENDS ${file} ${PROJECT_SOURCE_DIR}/.clang-tidy ${IMLUI_TIDY_DEPENDS}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "clang-tidy ${rel}"
        VERBATIM
    )
    list(APPEND IMLUI_TIDY_STAMPS ${stamp})
endforeach()

add_custom_target(tidy DEPENDS ${IMLUI_TIDY_STAMPS})
