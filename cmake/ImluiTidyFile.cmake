# Runs clang-tidy on one file for the `tidy` target (ImluiTidy.cmake). A finding (or any other failure) fails; a clean
# run passes. A crash (an access violation, or a segmentation fault elsewhere) is the known clang-tidy 22 bug in
# readability-identifier-naming (docs/development.md, "clang-tidy"): it is reported as a warning, the file is not
# checked, and the run passes. Which files crash changes with the BMIs, so there is no list of them.
#
#   cmake -DTIDY=<clang-tidy> -DBUILD=<build folder> -DFILE=<source file> -P ImluiTidyFile.cmake
execute_process(COMMAND ${TIDY} -p ${BUILD} --quiet --warnings-as-errors=* ${FILE}
                RESULT_VARIABLE result
                OUTPUT_VARIABLE out
                ERROR_VARIABLE out)
if(result STREQUAL "Access violation" OR result STREQUAL "Segmentation fault")
    message(WARNING "clang-tidy crashed on ${FILE} (${result}), the known clang-tidy bug: not checked.")
    return()
endif()
if(NOT result EQUAL 0)
    message("${out}")
    message(FATAL_ERROR "clang-tidy failed on ${FILE} (${result})")
endif()
