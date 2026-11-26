# Runs an app host the way a build step or CI job asks for its editor definitions: copied alone into an empty folder
# (like smoke_run.cmake, IMLUI_DEV_PATHS=0), once with `--lua-defs <dir>` and once with IMLUI_LUA_DEFS=<dir>. Each run
# must exit with 0 without opening a window, log no error, and write imlui.lua (the DSL's definitions, from the
# library's files) and types.lua with the built-in elements and the host's own type (`Probe` in the test host).
#
#   cmake -DEXE=<executable> -DWORK=<scratch folder> -P lua_definitions_run.cmake
file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
cmake_path(GET EXE PARENT_PATH bin)
cmake_path(GET EXE FILENAME exeName)
file(GLOB dlls ${bin}/*.dll)
file(COPY ${EXE} ${dlls} DESTINATION ${WORK})
set(ENV{IMLUI_DEV_PATHS} 0)

function(check_run dir)
    execute_process(COMMAND ${WORK}/${exeName} ${ARGN}
                    WORKING_DIRECTORY ${WORK}
                    RESULT_VARIABLE result
                    OUTPUT_VARIABLE out
                    ERROR_VARIABLE out
                    TIMEOUT 60)
    message("${out}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${exeName} exited with '${result}'")
    endif()
    foreach(level error critical)
        string(FIND "${out}" "] [${level}]" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR "${exeName} logged an error")
        endif()
    endforeach()
    foreach(expect "Lua definitions written to" "${dir}")
        string(FIND "${out}" "${expect}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${exeName} didn't log '${expect}'")
        endif()
    endforeach()
    file(READ ${WORK}/${dir}/imlui.lua prelude)
    file(READ ${WORK}/${dir}/types.lua types)
    foreach(expect "---@meta imlui" "function state(init) end")
        string(FIND "${prelude}" "${expect}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${dir}/imlui.lua lacks '${expect}'")
        endif()
    endforeach()
    foreach(expect "function Button(props) end" "function Probe(props) end" "---@class ProbeProps: ElementProps")
        string(FIND "${types}" "${expect}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${dir}/types.lua lacks '${expect}'")
        endif()
    endforeach()
endfunction()

check_run(defs --lua-defs defs)
set(ENV{IMLUI_LUA_DEFS} from-env)
check_run(from-env)
