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
#   - ASan is built without stack-use-after-return instrumentation (-fsanitize-address-use-after-return=never): it
#     breaks throwing from a catch block, which Lua errors (C++ exceptions, external/lua_cxx) do.
#   - The ASan runtime DLL is copied next to the executables (CMAKE_RUNTIME_OUTPUT_DIRECTORY) so tests and apps run
#     without PATH edits.
#
# add_compile_options only reaches targets in this directory and below: imlui's own and the vendored libraries. The
# flags are also usage requirements of the imlui library (IMLUI_SANITIZER_COMPILE_OPTIONS and
# IMLUI_SANITIZER_LINK_OPTIONS, src/CMakeLists.txt), so every target of an app that links imlui::imlui gets them too,
# which importing imlui's modules needs: an app that builds imlui as a subproject only sets IMLUI_SANITIZERS (and, on
# Windows, CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL), e.g. in a preset. An app may still include this file before
# add_subdirectory, to instrument its targets that don't link imlui too; the include guard keeps imlui from adding
# the flags a second time.
include_guard(GLOBAL)

set(IMLUI_SANITIZERS "" CACHE STRING "Comma-separated sanitizers for all targets (e.g. address,undefined)")

if(IMLUI_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "IMLUI_SANITIZERS needs clang")
    endif()
    set(IMLUI_SANITIZER_COMPILE_OPTIONS -fsanitize=${IMLUI_SANITIZERS} -fno-omit-frame-pointer)
    set(IMLUI_SANITIZER_LINK_OPTIONS -fsanitize=${IMLUI_SANITIZERS})
    if(IMLUI_SANITIZERS MATCHES "undefined")
        # Abort on the first UB report instead of logging and carrying on, so ctest fails.
        list(APPEND IMLUI_SANITIZER_COMPILE_OPTIONS -fno-sanitize-recover=undefined)
    endif()
    if(WIN32 AND IMLUI_SANITIZERS MATCHES "address")
        # ASan's stack-use-after-return instrumentation (a fake stack; off at run time by default anyway) breaks
        # throwing from a catch block on Windows: `throw;` or a new throw in a handler crashes ("stack-overflow").
        # Lua is compiled as C++, so a Lua error raised in a handler, or rethrown past one, is such a throw.
        list(APPEND IMLUI_SANITIZER_COMPILE_OPTIONS -fsanitize-address-use-after-return=never)
    endif()
    add_compile_options(${IMLUI_SANITIZER_COMPILE_OPTIONS})
    add_link_options(${IMLUI_SANITIZER_LINK_OPTIONS})

    if(WIN32 AND IMLUI_SANITIZERS MATCHES "address")
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=clang_rt.asan_dynamic-x86_64.dll
                        OUTPUT_VARIABLE IMLUI_ASAN_DLL OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(TO_CMAKE_PATH "${IMLUI_ASAN_DLL}" IMLUI_ASAN_DLL)
        if(NOT EXISTS ${IMLUI_ASAN_DLL})
            message(FATAL_ERROR "ASan runtime not found: ${IMLUI_ASAN_DLL}")
        endif()
        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
            file(COPY ${IMLUI_ASAN_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
        else()
            message(STATUS "imlui: no CMAKE_RUNTIME_OUTPUT_DIRECTORY, so ${IMLUI_ASAN_DLL} must be on PATH")
        endif()
    endif()
    message(STATUS "imlui: sanitizers ${IMLUI_SANITIZERS}")
endif()
