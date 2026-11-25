# imlui_bench (main.cpp): a headless benchmark of big UIs, the stress scenarios of stress.lua rendered through the
# headless frame harness (support/headless_frame.cppm). Not a test: ctest doesn't run it. Numbers come from a Release
# build (docs/development.md, "Benchmark").
add_executable(imlui_bench main.cpp)
target_sources(imlui_bench
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../support
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/../support/headless_frame.cppm
)
target_link_libraries(imlui_bench PRIVATE imlui ext::lua ext::imgui)
imlui_set_warnings(imlui_bench)
imlui_set_hardening(imlui_bench)
# The harness loads src/resources/ and the fonts from the source tree; the bench reads stress.lua from here.
target_compile_definitions(imlui_bench PRIVATE IMLUI_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/src/resources"
                                               IMLUI_FONT_DIR="${PROJECT_SOURCE_DIR}/external/fonts"
                                               IMLUI_BENCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
