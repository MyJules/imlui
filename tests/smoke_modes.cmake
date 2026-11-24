# smoke.modes: runs the smoke host (tests/smoke_host/main.cpp) once per way an app gets its files, each copied alone
# into its own empty folder like smoke_run.cmake does, and fails unless every run prints the same `summary:` lines
# (the files under app:/ with sizes and hashes, the folders listed, what the Lua app read, where the layout probe
# landed). That catches what works in development and breaks once shipped: a file served from the source folder that
# isn't embedded, a listing that differs, a name whose case only the file system ignores.
#
#   dev       the build's defaults: in a dev build (DEV_PATHS ON) the source folders over the embedded copy, filtered
#             by the manifest; the same as `embedded` in a shipped build (no source paths)
#   embedded  IMLUI_DEV_PATHS=0: the embedded copy alone
#   pak       IMLUI_DEV_PATHS=0 and IMLUI_SMOKE_NO_EMBEDDED_APP=1: app:/ only from PAK, copied next to the executable
#
# Each run must also exit with 0 (the app asserts what must and mustn't be visible), log no error, and load
# app:/main.lua from where its mode says.
#
#   cmake -DEXE=<executable> -DPAK=<smoke_app.pak> -DWORK=<scratch folder> -DDEV_PATHS=<ON|OFF> -P smoke_modes.cmake
file(REMOVE_RECURSE ${WORK})
cmake_path(GET EXE PARENT_PATH bin)
cmake_path(GET EXE FILENAME exeName)
file(GLOB dlls ${bin}/*.dll)

# Runs the executable in WORK/<mode> with the files in ARGN next to it; sets summary_<mode> to its summary lines.
function(run_mode mode expect)
    set(dir ${WORK}/${mode})
    file(MAKE_DIRECTORY ${dir})
    file(COPY ${EXE} ${dlls} ${ARGN} DESTINATION ${dir})
    set(ENV{IMLUI_EXIT_AFTER_FRAMES} 5)
    execute_process(COMMAND ${dir}/${exeName}
                    WORKING_DIRECTORY ${dir}
                    RESULT_VARIABLE result
                    OUTPUT_VARIABLE out
                    ERROR_VARIABLE out
                    TIMEOUT 90)
    message("---- ${mode}\n${out}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${mode}: ${exeName} exited with '${result}'")
    endif()
    string(FIND "${out}" "loaded app:/main.lua (${expect}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${mode}: app:/main.lua wasn't loaded from ${expect}...")
    endif()
    foreach(level error critical)
        string(FIND "${out}" "] [${level}]" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR "${mode}: ${exeName} logged an error")
        endif()
    endforeach()
    string(REPLACE "\r" "" out "${out}")
    string(REGEX MATCHALL "summary: [^\n]*" lines "${out}")
    if(NOT lines)
        message(FATAL_ERROR "${mode}: ${exeName} printed no summary")
    endif()
    set(summary_${mode} "${lines}" PARENT_SCOPE)
endfunction()

if(DEV_PATHS)
    set(devOrigin "directory '")
else()
    set(devOrigin "embedded 'smoke_app (app:/)'")
endif()
unset(ENV{IMLUI_DEV_PATHS})
unset(ENV{IMLUI_SMOKE_NO_EMBEDDED_APP})
run_mode(dev "${devOrigin}")
set(ENV{IMLUI_DEV_PATHS} 0)
run_mode(embedded "embedded 'smoke_app (app:/)'")
set(ENV{IMLUI_SMOKE_NO_EMBEDDED_APP} 1)
run_mode(pak "pak '" ${PAK})

# Compares every mode with the first, line by line, and names each line only one of them printed.
set(failed OFF)
foreach(mode embedded pak)
    set(diff "")
    foreach(line IN LISTS summary_dev)
        if(NOT line IN_LIST summary_${mode})
            string(APPEND diff "\n  only in dev: ${line}")
        endif()
    endforeach()
    foreach(line IN LISTS summary_${mode})
        if(NOT line IN_LIST summary_dev)
            string(APPEND diff "\n  only in ${mode}: ${line}")
        endif()
    endforeach()
    if(diff STREQUAL "" AND NOT summary_dev STREQUAL summary_${mode})
        set(diff "\n  the same lines in another order")
    endif()
    if(NOT diff STREQUAL "")
        message(SEND_ERROR "dev and ${mode} runs differ:${diff}")
        set(failed ON)
    endif()
endforeach()
if(failed)
    message(FATAL_ERROR "the smoke app saw different resources depending on where they came from")
endif()
list(LENGTH summary_dev count)
message("all modes printed the same ${count} summary lines")
