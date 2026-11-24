# Sanitizer builds (the clang-asan preset). Included before external/ so the flags reach every
# target, vendored libraries included: ASan only sees what was compiled with it.
#
#   IMLUI_SANITIZERS  comma-separated -fsanitize= list, e.g. "address,undefined". Empty: off.
#
# On x86_64-pc-windows-msvc:
#   - ASan needs the release dynamic CRT (/MD). The preset sets CMAKE_MSVC_RUNTIME_LIBRARY to
#     MultiThreadedDLL, so a sanitizer build is Debug code (-O0 -g) on the release CRT, without the
#     STL's debug iterators.
#   - The `undefined` group drops `vptr` on this target (no RTTI-based vptr checks for the MSVC ABI).
#   - LeakSanitizer is not available on Windows.
#   - The ASan runtime DLL is copied next to the executables so tests and the app run without PATH edits.

set(IMLUI_SANITIZERS "" CACHE STRING "Comma-separated sanitizers for all targets (e.g. address,undefined)")

if(IMLUI_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "IMLUI_SANITIZERS needs clang")
    endif()
    add_compile_options(-fsanitize=${IMLUI_SANITIZERS} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${IMLUI_SANITIZERS})
    if(IMLUI_SANITIZERS MATCHES "undefined")
        # Abort on the first UB report instead of logging and carrying on, so ctest fails.
        add_compile_options(-fno-sanitize-recover=undefined)
    endif()

    if(WIN32 AND IMLUI_SANITIZERS MATCHES "address")
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=clang_rt.asan_dynamic-x86_64.dll
                        OUTPUT_VARIABLE IMLUI_ASAN_DLL OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(TO_CMAKE_PATH "${IMLUI_ASAN_DLL}" IMLUI_ASAN_DLL)
        if(NOT EXISTS ${IMLUI_ASAN_DLL})
            message(FATAL_ERROR "ASan runtime not found: ${IMLUI_ASAN_DLL}")
        endif()
        file(COPY ${IMLUI_ASAN_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    endif()
    message(STATUS "imlui: sanitizers ${IMLUI_SANITIZERS}")
endif()
