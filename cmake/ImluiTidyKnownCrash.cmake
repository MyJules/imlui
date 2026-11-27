# Runs clang-tidy on a file of IMLUI_TIDY_KNOWN_CRASHES (ImluiTidy.cmake), for the `tidy` target. Like the plain
# command: a finding (or any other failure) fails, a clean run passes. A crash (an access violation, or a segmentation
# fault elsewhere) is the known clang-tidy bug: it is reported as a warning and passes. A clean run is reported too, so
# the file can come off the list.
#
#   cmake -DTIDY=<clang-tidy> -DBUILD=<build folder> -DFILE=<source file> -P ImluiTidyKnownCrash.cmake
execute_process(COMMAND ${TIDY} -p ${BUILD} --quiet --warnings-as-errors=* ${FILE}
                RESULT_VARIABLE result
                OUTPUT_VARIABLE out
                ERROR_VARIABLE out)
if(result STREQUAL "Access violation" OR result STREQUAL "Segmentation fault")
    message(WARNING "clang-tidy crashed on ${FILE} (${result}), a known crash: not checked. "
                    "See IMLUI_TIDY_KNOWN_CRASHES in cmake/ImluiTidy.cmake.")
    return()
endif()
message("${out}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "clang-tidy failed on ${FILE} (${result})")
endif()
message(NOTICE "clang-tidy passed on ${FILE}, which is listed as a known crash: take it off IMLUI_TIDY_KNOWN_CRASHES "
               "in cmake/ImluiTidy.cmake.")
