# Runs an executable alone, like a shipped release: copied into an empty folder with only the DLLs that sit next to
# it in the build (the ASan runtime in sanitizer builds), no source tree (IMLUI_DEV_PATHS=0), quitting after a few
# frames (IMLUI_EXIT_AFTER_FRAMES; the exit code is 1 when a script error was reported). Fails unless it exits
# with 0, logged EXPECT (where the entry script came from), and logged no error.
#
#   cmake -DEXE=<executable> -DWORK=<scratch folder> -DEXPECT=<text> -P smoke_run.cmake
file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
cmake_path(GET EXE PARENT_PATH bin)
cmake_path(GET EXE FILENAME exeName)
file(GLOB dlls ${bin}/*.dll)
file(COPY ${EXE} ${dlls} DESTINATION ${WORK})

set(ENV{IMLUI_DEV_PATHS} 0)
set(ENV{IMLUI_EXIT_AFTER_FRAMES} 5)
execute_process(COMMAND ${WORK}/${exeName}
                WORKING_DIRECTORY ${WORK}
                RESULT_VARIABLE result
                OUTPUT_VARIABLE out
                ERROR_VARIABLE out
                TIMEOUT 90)
message("${out}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${exeName} exited with '${result}'")
endif()
string(FIND "${out}" "${EXPECT}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "${exeName} didn't log '${EXPECT}'")
endif()
foreach(level error critical)
    string(FIND "${out}" "] [${level}]" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "${exeName} logged an error")
    endif()
endforeach()
