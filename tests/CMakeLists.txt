# Unit tests (GoogleTest). Test files include <gtest/gtest.h> and import the modules they test.
file(GLOB IMLUI_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(imlui_tests ${IMLUI_TEST_SOURCES})
# Test-only modules shared by the tests: the headless ImGui frame harness (imlui.test.headless_frame).
file(GLOB IMLUI_TEST_MODULES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/support/*.cppm)
target_sources(imlui_tests
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/support
        FILES ${IMLUI_TEST_MODULES}
)
# imlui_pack_lib: the .pak writer and the resource encoding, for the round-trip tests (resources_packed_test).
target_link_libraries(imlui_tests PRIVATE imlui imlui_pack_lib ext::lua ext::imgui ext::spdlog ext::gtest_main)
imlui_set_warnings(imlui_tests)
imlui_set_hardening(imlui_tests)
# lua_library_test and the headless harness load src/resources/imlui.lua from the source tree; the harness loads the
# Roboto fonts from external/fonts.
target_compile_definitions(imlui_tests PRIVATE IMLUI_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/src/resources"
                                               IMLUI_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
                                               IMLUI_FONT_DIR="${PROJECT_SOURCE_DIR}/external/fonts")
# element_reference_test checks docs/elements.md and the element classes' doc comments against the registered types.
target_compile_definitions(imlui_tests PRIVATE IMLUI_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs"
                                               IMLUI_ELEMENTS_DIR="${PROJECT_SOURCE_DIR}/src/imlui/ui/elements")
# An archive built through imlui_add_pak (the smoke app's scripts at app:/smoke/, manifest data/smoke_pak.lua), read
# back by resources_packed_test.
imlui_add_pak(imlui_test_pak data/smoke_pak.lua OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/smoke.pak)
add_dependencies(imlui_tests imlui_test_pak)
target_compile_definitions(imlui_tests PRIVATE IMLUI_TEST_PAK="${CMAKE_CURRENT_BINARY_DIR}/smoke.pak"
                                               IMLUI_SMOKE_APP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_app")
# Both shapes of a generated module, whatever IMLUI_EMBED_RESOURCES says: the smoke app's manifest embedded
# (imlui.embedded.smoke_embedded) and, in dev builds, not embedded (imlui.embedded.smoke_sources: source folders only).
# The library's own imlui.embedded.imlui (target imlui, src/CMakeLists.txt) follows the option, which
# IMLUI_TEST_EMBED_RESOURCES tells the tests (the embedded copy an install carries isn't linked here).
imlui_add_resources(imlui_tests data/smoke_app/resources.lua NAME smoke_embedded EMBED ON)
if(IMLUI_DEV_PATHS)
    imlui_add_resources(imlui_tests data/smoke_app/resources.lua NAME smoke_sources EMBED OFF)
    target_compile_definitions(imlui_tests PRIVATE IMLUI_TEST_DEV_PATHS=1)
endif()
if(IMLUI_EMBED_RESOURCES)
    target_compile_definitions(imlui_tests PRIVATE IMLUI_TEST_EMBED_RESOURCES=1)
endif()

include(GoogleTest)
gtest_discover_tests(imlui_tests DISCOVERY_MODE PRE_TEST)

# The test app host (smoke_host/main.cpp): the smoke app in data/smoke_app, embedded from its resources.lua like any
# app's files, plus a registered C++ type (test::RenderCounter as `Probe`). Built only with the tests. Always embedded
# (EMBED ON), so the smoke test below runs it alone in every configuration; when the library itself isn't embedded
# (IMLUI_EMBED_RESOURCES=OFF), its imlui:/ files come from their source folders.
add_executable(imlui_smoke_host smoke_host/main.cpp)
target_sources(imlui_smoke_host
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/support
        FILES support/render_counter.cppm
)
target_link_libraries(imlui_smoke_host PRIVATE imlui)
imlui_set_warnings(imlui_smoke_host)
imlui_set_hardening(imlui_smoke_host)
imlui_add_resources(imlui_smoke_host data/smoke_app/resources.lua EMBED ON)

# Release-style smoke runs: the executable is copied alone (with the DLLs next to it, e.g. the ASan runtime) into an
# empty folder and started with IMLUI_DEV_PATHS=0 (no source tree) and IMLUI_EXIT_AFTER_FRAMES (quit after a few
# frames, exit 1 on a script error), so it must run from its embedded Lua, fonts and app script alone (the library's
# imlui:/ files from its source folders when the build doesn't embed them, IMLUI_EMBED_RESOURCES=OFF).
function(imlui_add_smoke_test name target expect)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:${target}> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/smoke/${name}
                     -DEXPECT=${expect} -P ${CMAKE_CURRENT_SOURCE_DIR}/smoke_run.cmake)
    set_tests_properties(${name} PROPERTIES TIMEOUT 120 RUN_SERIAL ON)
endfunction()
imlui_add_smoke_test(smoke.imlui_smoke_host imlui_smoke_host "embedded 'smoke_app (app:/)' main.lua")

# The package tests: a consumer app built with imlui added by add_subdirectory, FetchContent and find_package
# (package/CMakeLists.txt, label `package`).
add_subdirectory(package)
