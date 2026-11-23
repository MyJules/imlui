# Unit tests (GoogleTest). Test files include <gtest/gtest.h> and import the modules they test.
file(GLOB IMLUI_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(imlui_tests ${IMLUI_TEST_SOURCES})
target_link_libraries(imlui_tests PRIVATE imlui ext::lua ext::imgui ext::spdlog GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(imlui_tests DISCOVERY_MODE PRE_TEST)
