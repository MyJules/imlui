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

# main.lua is embedded (app:/main.lua), so the binary runs on its own: the library embeds its Lua and fonts too.
# In dev builds defaultConfig mounts this folder over it (hot reload): the script path below is relative to the
# source tree. A main.lua at the same relative path next to the binary would override it in a release build.
imlui_add_resources(imlui_custom_types NAME custom_types PREFIX app BASE ${CMAKE_CURRENT_SOURCE_DIR} FILES main.lua)
file(RELATIVE_PATH IMLUI_CUSTOM_TYPES_SCRIPT ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/main.lua)
target_compile_definitions(imlui_custom_types PRIVATE IMLUI_EXAMPLE_SCRIPT="${IMLUI_CUSTOM_TYPES_SCRIPT}")
if(IMLUI_DEV_PATHS)
    target_compile_definitions(imlui_custom_types PRIVATE IMLUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()
