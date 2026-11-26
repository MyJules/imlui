# Lua language-server definitions as a build step: runs an app host with `--lua-defs <dir>`, which makes
# Engine::run() write the LuaLS `---@meta` files of the DSL and of every type and singleton the host registered
# (imlui.lua, types.lua) into <dir> and exit, without opening a window. See docs/extending.md, "Editor support: Lua
# definitions". imlui's CMakeLists.txt includes this file, and so does the installed package's imluiConfig.cmake.
#
#   imlui_add_lua_definitions(<target> [OUTPUT <dir>] [ALL])
#
#     Adds the target <target>_lua_definitions, which runs <target> (an executable whose main() registers its types
#     and calls Engine::run() with defaultConfig(argc, argv, ...)) with `--lua-defs <dir>` after <target> was built.
#     OUTPUT defaults to <current binary dir>/lua-definitions; a relative one is relative to the current binary dir.
#     ALL builds it with `all`, so the definitions follow every change of the registered types; otherwise build it
#     when you want them (`cmake --build <build> --target <target>_lua_definitions`). The files are rewritten only
#     when their content changes. The host runs on the build machine, so this doesn't work when cross-compiling.
include_guard(GLOBAL)

function(imlui_add_lua_definitions target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "ALL" "OUTPUT" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "imlui_add_lua_definitions: unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "imlui_add_lua_definitions: ${target} is not a target")
    endif()
    set(output ${arg_OUTPUT})
    if(NOT output)
        set(output ${CMAKE_CURRENT_BINARY_DIR}/lua-definitions)
    elseif(NOT IS_ABSOLUTE ${output})
        set(output ${CMAKE_CURRENT_BINARY_DIR}/${output})
    endif()
    set(all "")
    if(arg_ALL)
        set(all ALL)
    endif()
    add_custom_target(${target}_lua_definitions ${all}
        COMMAND $<TARGET_FILE:${target}> --lua-defs ${output}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Writing the Lua definitions of ${target} to ${output}"
        VERBATIM)
    add_dependencies(${target}_lua_definitions ${target})
endfunction()
