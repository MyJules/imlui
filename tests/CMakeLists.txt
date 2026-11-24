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
target_link_libraries(imlui_tests PRIVATE imlui ext::lua ext::imgui ext::spdlog GTest::gtest_main)
imlui_set_warnings(imlui_tests)
imlui_set_hardening(imlui_tests)
# lua_library_test and the headless harness load lua/imlui.lua from the source tree; the harness loads the
# Roboto fonts from external/fonts.
target_compile_definitions(imlui_tests PRIVATE IMLUI_LUA_DIR="${CMAKE_SOURCE_DIR}/lua"
                                               IMLUI_FONT_DIR="${CMAKE_SOURCE_DIR}/external/fonts")

include(GoogleTest)
gtest_discover_tests(imlui_tests DISCOVERY_MODE PRE_TEST)
