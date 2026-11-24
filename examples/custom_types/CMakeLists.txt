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

# main.lua is embedded (app:/main.lua, listed in resources.lua), so the binary runs on its own: the library embeds
# its Lua and fonts too. In dev builds (IMLUI_DEV_PATHS=ON) the package knows this folder and defaultConfig mounts it
# over the embedded copy (hot reload). In a release build, app/main.lua next to the binary would override it. The
# module is imlui.embedded.custom_types, named after this folder.
imlui_add_resources(imlui_custom_types resources.lua)
