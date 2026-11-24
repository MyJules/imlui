# One package test (tests/CMakeLists.txt): configures, builds and runs the consumer app (tests/package/consumer) with
# imlui added in one MODE, with the parent build's generator, compilers and build type. The consumer's build folder is
# kept between runs (only the first run builds everything). The executable is then run like a shipped one: copied
# with the files next to it (its .pak, DLLs) into an empty folder, with IMLUI_DEV_PATHS=0 (only packages that aren't
# embedded read their source folders) and IMLUI_EXIT_AFTER_FRAMES (exit code 1 on a script error). Fails unless it
# exits with 0 and logged no error.
#
#   cmake -DMODE=<subdirectory|fetchcontent|find_package|fetchcontent_find> -DSOURCE=<imlui source> -DWORK=<folder>
#         -DGENERATOR=<...> -DMAKE_PROGRAM=<...> -DC_COMPILER=<...> -DCXX_COMPILER=<...> -DBUILD_TYPE=<...>
#         [-DPREFIX=<installed imlui>] [-DMSVC_RUNTIME=<...>] [-DOWN_DEPS=ON] [-DSANITIZERS=<list>] -P run.cmake
set(consumer ${CMAKE_CURRENT_LIST_DIR}/consumer)
set(build ${WORK}/build)

set(args -G ${GENERATOR} -DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM} -DCMAKE_C_COMPILER=${C_COMPILER}
         -DCMAKE_CXX_COMPILER=${CXX_COMPILER} -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
         -DIMLUI_CONSUMER_MODE=${MODE} -DIMLUI_SOURCE_DIR=${SOURCE})
if(MSVC_RUNTIME)
    list(APPEND args -DCMAKE_MSVC_RUNTIME_LIBRARY=${MSVC_RUNTIME})
endif()
if(OWN_DEPS)
    list(APPEND args -DIMLUI_CONSUMER_OWN_DEPS=ON)
endif()
if(SANITIZERS)
    list(APPEND args -DIMLUI_SANITIZERS=${SANITIZERS})
endif()
if(PREFIX)
    # An installed imlui embeds its own files; the app's are embedded too, so the executable runs on its own.
    list(APPEND args -DCMAKE_PREFIX_PATH=${PREFIX} -DIMLUI_DEV_PATHS=OFF)
endif()

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT result EQUAL 0)
        message("${out}")
        message(FATAL_ERROR "failed (${result}): ${ARGN}")
    endif()
endfunction()

message("configuring the consumer (${MODE}) in ${build}")
run(${CMAKE_COMMAND} -S ${consumer} -B ${build} ${args})
message("building")
run(${CMAKE_COMMAND} --build ${build})

set(dir ${WORK}/run)
file(REMOVE_RECURSE ${dir})
file(MAKE_DIRECTORY ${dir})
file(GLOB files ${build}/out/*.exe ${build}/out/consumer ${build}/out/*.pak ${build}/out/*.dll)
file(COPY ${files} DESTINATION ${dir})
set(exe consumer)
if(CMAKE_HOST_WIN32)
    set(exe consumer.exe)
endif()
set(ENV{IMLUI_DEV_PATHS} 0)
set(ENV{IMLUI_EXIT_AFTER_FRAMES} 5)
execute_process(COMMAND ${dir}/${exe} WORKING_DIRECTORY ${dir} RESULT_VARIABLE result OUTPUT_VARIABLE out
                ERROR_VARIABLE out TIMEOUT 90)
message("${out}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the consumer exited with '${result}'")
endif()
foreach(level error critical)
    string(FIND "${out}" "] [${level}]" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "the consumer logged an error")
    endif()
endforeach()
