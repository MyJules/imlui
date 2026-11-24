# imlui_custom_types: registers C++ element types and a singleton (gauge.cppm, system_info.cppm) and runs
# main.lua from this folder. See docs/extending.md.
add_executable(imlui_custom_types main.cpp)
target_sources(imlui_custom_types
    PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES gauge.cppm system_info.cppm
)
target_link_libraries(imlui_custom_types PRIVATE imlui ext::lua ext::imgui ext::spdlog)
imlui_set_warnings(imlui_custom_types)
imlui_set_hardening(imlui_custom_types)

# The script path is relative to the root that defaultConfig picks: the source tree in dev builds (main.lua and
# lua/ hot-reload from there), otherwise the binary's folder, where the post-build step below copies them.
file(RELATIVE_PATH IMLUI_CUSTOM_TYPES_SCRIPT ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/main.lua)
target_compile_definitions(imlui_custom_types PRIVATE IMLUI_EXAMPLE_SCRIPT="${IMLUI_CUSTOM_TYPES_SCRIPT}")
if(IMLUI_DEV_PATHS)
    target_compile_definitions(imlui_custom_types PRIVATE IMLUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

add_custom_command(TARGET imlui_custom_types POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lua $<TARGET_FILE_DIR:imlui_custom_types>/lua
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${IMLUI_FONTS_DIR} $<TARGET_FILE_DIR:imlui_custom_types>/fonts
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:imlui_custom_types>/examples/custom_types
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/main.lua
            $<TARGET_FILE_DIR:imlui_custom_types>/${IMLUI_CUSTOM_TYPES_SCRIPT}
)
