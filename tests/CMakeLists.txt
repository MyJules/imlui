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
target_link_libraries(imlui_tests PRIVATE imlui imlui_pack_lib ext::lua ext::imgui ext::spdlog GTest::gtest_main)
imlui_set_warnings(imlui_tests)
imlui_set_hardening(imlui_tests)
# lua_library_test and the headless harness load lua/imlui.lua from the source tree; the harness loads the
# Roboto fonts from external/fonts.
target_compile_definitions(imlui_tests PRIVATE IMLUI_LUA_DIR="${CMAKE_SOURCE_DIR}/lua"
                                               IMLUI_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
                                               IMLUI_FONT_DIR="${CMAKE_SOURCE_DIR}/external/fonts")
# An archive built through imlui_add_pak (examples/counter at app:/counter/, manifest data/counter_pak.lua), read
# back by resources_packed_test.
imlui_add_pak(imlui_test_pak data/counter_pak.lua OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/counter.pak)
add_dependencies(imlui_tests imlui_test_pak)
target_compile_definitions(imlui_tests PRIVATE IMLUI_TEST_PAK="${CMAKE_CURRENT_BINARY_DIR}/counter.pak"
                                               IMLUI_COUNTER_DIR="${CMAKE_SOURCE_DIR}/examples/counter")

include(GoogleTest)
gtest_discover_tests(imlui_tests DISCOVERY_MODE PRE_TEST)

# Release-style smoke runs: the executable is copied alone (with the DLLs next to it, e.g. the ASan runtime) into an
# empty folder and started with IMLUI_DEV_PATHS=0 (no source tree) and IMLUI_EXIT_AFTER_FRAMES (quit after a few
# frames, exit 1 on a script error), so it must run from its embedded Lua, fonts and app script alone.
function(imlui_add_smoke_test name target expect)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:${target}> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/smoke/${name}
                     -DEXPECT=${expect} -P ${CMAKE_CURRENT_SOURCE_DIR}/smoke_run.cmake)
    set_tests_properties(${name} PROPERTIES TIMEOUT 120 RUN_SERIAL ON)
endfunction()
imlui_add_smoke_test(smoke.imlui_app imlui_app "embedded 'gallery' main.lua")
if(TARGET imlui_custom_types)
    imlui_add_smoke_test(smoke.imlui_custom_types imlui_custom_types "embedded 'custom_types' main.lua")
endif()
