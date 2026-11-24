# The `tidy` target: runs clang-tidy (the naming checks in .clang-tidy) on every module in src/ and on app/main.cpp,
# and fails on any finding (--warnings-as-errors=*).
#
#   cmake --build build/clang-debug --target tidy
#
# clang-tidy reads the compile commands of this build (CMAKE_EXPORT_COMPILE_COMMANDS) and the module BMIs they
# reference, so the target depends on the imlui library and the app: the BMIs are current when it runs. Each file is
# its own command, so Ninja runs them in parallel; a file is checked again when it, or any module (the library),
# changed. See docs/development.md, "clang-tidy".
find_program(IMLUI_CLANG_TIDY NAMES clang-tidy)
if(NOT IMLUI_CLANG_TIDY)
    message(STATUS "clang-tidy not found: no `tidy` target")
    return()
endif()

file(GLOB_RECURSE IMLUI_TIDY_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cppm)
list(APPEND IMLUI_TIDY_FILES ${CMAKE_SOURCE_DIR}/app/main.cpp)

set(IMLUI_TIDY_STAMPS)
foreach(file IN LISTS IMLUI_TIDY_FILES)
    file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR} ${file})
    set(stamp ${CMAKE_BINARY_DIR}/tidy/${rel}.stamp)
    add_custom_command(
        OUTPUT ${stamp}
        COMMAND ${IMLUI_CLANG_TIDY} -p ${CMAKE_BINARY_DIR} --quiet --warnings-as-errors=* ${file}
        COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
        DEPENDS ${file} ${CMAKE_SOURCE_DIR}/.clang-tidy imlui imlui_app
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy ${rel}"
        VERBATIM
    )
    list(APPEND IMLUI_TIDY_STAMPS ${stamp})
endforeach()

add_custom_target(tidy DEPENDS ${IMLUI_TIDY_STAMPS})
