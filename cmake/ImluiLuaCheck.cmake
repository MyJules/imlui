# The `lua_check` target: checks the Lua definitions with lua-language-server (`--check`), the editor's own
# diagnostics, and fails on any problem at Warning level or above. Only when imlui is the top-level project and the
# tests are built (CMakeLists.txt).
#
#   cmake --build build/clang-debug --target lua_check
#
# It checks what an app's editor sees: the definitions the test host writes (imlui_smoke_host_lua_definitions,
# tests/CMakeLists.txt, into <build>/tests/lua-definitions/): the hand-written src/resources/definitions/imlui.lua next
# to the generated types.lua, which declares the element classes and enums imlui.lua refers to. Checked alone,
# imlui.lua would report those as undefined. lua-language-server's log goes to <build>/lua-check/. Skipped when
# lua-language-server isn't found. See docs/development.md, "Lua definitions check".
find_program(IMLUI_LUA_LANGUAGE_SERVER NAMES lua-language-server)
if(NOT IMLUI_LUA_LANGUAGE_SERVER)
    message(STATUS "lua-language-server not found: no `lua_check` target")
    return()
endif()
if(NOT TARGET imlui_smoke_host_lua_definitions)
    return()
endif()

add_custom_target(lua_check
    COMMAND ${IMLUI_LUA_LANGUAGE_SERVER} --check=${PROJECT_BINARY_DIR}/tests/lua-definitions --checklevel=Warning
            --logpath=${PROJECT_BINARY_DIR}/lua-check
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "lua-language-server --check on the Lua definitions"
    VERBATIM)
add_dependencies(lua_check imlui_smoke_host_lua_definitions)
