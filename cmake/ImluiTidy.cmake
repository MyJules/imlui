# The `tidy` target: runs clang-tidy (the naming checks in .clang-tidy) on every module and implementation unit in src/,
# every module in tools/, and the tests: the test files, their support modules, the test host, the fuzz targets and
# the benchmark (tests/**/*.cpp and *.cppm, but the package consumer, which a package test builds in a tree of its
# own). It fails on any finding (--warnings-as-errors=*). Only when imlui is the top-level project (CMakeLists.txt):
# an app has its own checks.
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
if(TARGET imlui_tests)
    # Every file of the tests' targets is in this build's compile commands. The package consumer
    # (tests/package/consumer/) isn't: a package test configures it as a project of its own.
    file(GLOB_RECURSE IMLUI_TIDY_TEST_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/*.cpp
                                                              ${PROJECT_SOURCE_DIR}/tests/*.cppm)
    list(FILTER IMLUI_TIDY_TEST_FILES EXCLUDE REGEX "/tests/package/")
    if(NOT TARGET imlui_bench)
        list(FILTER IMLUI_TIDY_TEST_FILES EXCLUDE REGEX "/tests/bench/")
    endif()
    list(APPEND IMLUI_TIDY_FILES ${IMLUI_TIDY_TEST_FILES})
    foreach(target imlui_tests imlui_smoke_host imlui_bench imlui_fuzz_replay_pak imlui_fuzz_replay_font
                   imlui_fuzz_replay_manifest imlui_fuzz_replay_names imlui_fuzz_replay_image)
        if(TARGET ${target})
            list(APPEND IMLUI_TIDY_DEPENDS ${target})
        endif()
    endforeach()
endif()

# Files clang-tidy 22.1.7 crashes on (an access violation in readability-identifier-naming, "Processing
# 'readability-identifier-naming' against: TranslationUnitDecl", with freshly built BMIs and one file at a time; see
# docs/development.md, "clang-tidy"). Each is still checked, through ImluiTidyKnownCrash.cmake: a finding fails the
# target as anywhere else, and only that crash is tolerated, with a warning. A listed file that no longer crashes is
# reported, so the list shrinks when the crash is fixed. Remove a file from it as soon as it passes.
set(IMLUI_TIDY_KNOWN_CRASHES
    src/imlui/lua/native.cppm
    src/imlui/lua/node.cppm
    src/imlui/lua/painter.cppm
    src/imlui/lua/theme_table.cppm
    src/imlui/skin/fluent/common.cppm
    src/imlui/skin/fluent/divider.cppm
    src/imlui/skin/fluent/navigation_drawer.cppm
    src/imlui/skin/fluent/segmented.cppm
    src/imlui/skin/fluent/tabs.cppm
    src/imlui/skin/registry.cppm
    src/imlui/theme/font_library.cppm
    src/imlui/theme/typography.cppm
    src/imlui/ui/enum_names.cppm
    src/imlui/ui/props.cppm
    src/imlui/widgets/anchored_popup.cppm
    src/imlui/widgets/button.cppm
    src/imlui/widgets/color_field.cppm
    src/imlui/widgets/common.cppm
    src/imlui/widgets/dialog.cppm
    src/imlui/widgets/menu.cppm
    src/imlui/widgets/selection.cppm
    src/imlui/widgets/slider.cppm
    src/imlui/widgets/text.cppm
)

set(IMLUI_TIDY_STAMPS)
foreach(file IN LISTS IMLUI_TIDY_FILES)
    file(RELATIVE_PATH rel ${PROJECT_SOURCE_DIR} ${file})
    set(stamp ${PROJECT_BINARY_DIR}/tidy/${rel}.stamp)
    if(rel IN_LIST IMLUI_TIDY_KNOWN_CRASHES)
        set(command ${CMAKE_COMMAND} -DTIDY=${IMLUI_CLANG_TIDY} -DBUILD=${PROJECT_BINARY_DIR} -DFILE=${file}
                    -P ${CMAKE_CURRENT_LIST_DIR}/ImluiTidyKnownCrash.cmake)
    else()
        set(command ${IMLUI_CLANG_TIDY} -p ${PROJECT_BINARY_DIR} --quiet --warnings-as-errors=* ${file})
    endif()
    add_custom_command(
        OUTPUT ${stamp}
        COMMAND ${command}
        COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
        DEPENDS ${file} ${PROJECT_SOURCE_DIR}/.clang-tidy ${IMLUI_TIDY_DEPENDS}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "clang-tidy ${rel}"
        VERBATIM
    )
    list(APPEND IMLUI_TIDY_STAMPS ${stamp})
endforeach()

add_custom_target(tidy DEPENDS ${IMLUI_TIDY_STAMPS})
