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
#   - Executables link with an 8 MiB stack (/STACK, a link option of the usage requirements too): ASan's frames are
#     several times larger, and the renderer's stack check (Renderer::stackReserve) would cut a tree early on 1 MiB.
#   - The ASan runtime DLL (named after the target's architecture, which the compiler reports) is copied next to the
#     executables (CMAKE_RUNTIME_OUTPUT_DIRECTORY, or its folder per configuration with a multi-config generator) so
#     tests and apps run without PATH edits.
#
# add_compile_options only reaches targets in this directory and below: imlui's own and the vendored libraries. The
# flags are also usage requirements of the imlui library (IMLUI_SANITIZER_COMPILE_OPTIONS and
# IMLUI_SANITIZER_LINK_OPTIONS, src/CMakeLists.txt), so every target of an app that links imlui::imlui gets them too,
# which importing imlui's modules needs: an app that builds imlui as a subproject only sets IMLUI_SANITIZERS (and, on
# Windows, CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL), e.g. in a preset. An app may still include this file before
# add_subdirectory, to instrument its targets that don't link imlui too; the include guard keeps imlui from adding
# the flags a second time.
#
# Fuzzing (the clang-fuzz preset, docs/development.md#fuzzing):
#
#   IMLUI_FUZZ  ON: also build the libFuzzer executables of tests/fuzz (imlui_fuzz_<target>). Every target is then
#               compiled with the coverage instrumentation libFuzzer needs (-fsanitize=fuzzer-no-link, added to the
#               sanitizer list so it reaches every target and importer alike); only the fuzzer executables link
#               libFuzzer itself (-fsanitize=fuzzer, IMLUI_FUZZ_LINK_OPTIONS). When the compiler can't link a libFuzzer
#               executable, a warning says so and IMLUI_FUZZ_SUPPORTED stays off. The corpus replay tests
#               (imlui_fuzz_replay_<target>, label `fuzz`) are built in every configuration.
#
# libFuzzer on x86_64-pc-windows-msvc: LLVM ships clang_rt.fuzzer built against the static release CRT, without the
# STL's AddressSanitizer annotations. The MSVC STL refuses to link objects that disagree on either (/failifmismatch),
# so the clang-fuzz preset sets CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded (ASan works with it: the static runtime
# thunk), and IMLUI_FUZZ adds _DISABLE_STRING_ANNOTATION and _DISABLE_VECTOR_ANNOTATION to every target.
include_guard(GLOBAL)

set(IMLUI_SANITIZERS "" CACHE STRING "Comma-separated sanitizers for all targets (e.g. address,undefined)")
option(IMLUI_FUZZ "Build the libFuzzer targets of tests/fuzz (needs clang with libFuzzer)" OFF)

set(IMLUI_FUZZ_SUPPORTED OFF)
set(_imlui_sanitizers ${IMLUI_SANITIZERS})
if(IMLUI_FUZZ)
    # With the sanitizers it will be linked with (ASan brings the runtime libFuzzer needs on Windows).
    include(CheckCXXSourceCompiles)
    if(IMLUI_SANITIZERS)
        set(CMAKE_REQUIRED_FLAGS -fsanitize=${IMLUI_SANITIZERS},fuzzer)
    else()
        set(CMAKE_REQUIRED_FLAGS -fsanitize=fuzzer)
    endif()
    set(CMAKE_REQUIRED_LINK_OPTIONS ${CMAKE_REQUIRED_FLAGS})
    set(_imlui_fuzz_definitions)
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        set(_imlui_fuzz_definitions -D_DISABLE_STRING_ANNOTATION -D_DISABLE_VECTOR_ANNOTATION)
    endif()
    string(JOIN " " CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS} ${_imlui_fuzz_definitions})
    check_cxx_source_compiles([[
        #include <stddef.h>
        #include <stdint.h>
        #include <string>
        extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
            return std::string(reinterpret_cast<const char*>(data), size) == "x";
        }
    ]] IMLUI_HAS_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND IMLUI_HAS_LIBFUZZER)
        set(IMLUI_FUZZ_SUPPORTED ON)
        set(IMLUI_FUZZ_LINK_OPTIONS -fsanitize=fuzzer)
        if(_imlui_sanitizers)
            string(APPEND _imlui_sanitizers ",fuzzer-no-link")
        else()
            set(_imlui_sanitizers fuzzer-no-link)
        endif()
        message(STATUS "imlui: libFuzzer targets on")
    else()
        message(WARNING "IMLUI_FUZZ: ${CMAKE_CXX_COMPILER} can't link a libFuzzer executable (-fsanitize=fuzzer); "
                        "only the corpus replay tests are built")
    endif()
endif()

if(_imlui_sanitizers)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "IMLUI_SANITIZERS needs clang")
    endif()
    set(IMLUI_SANITIZER_COMPILE_OPTIONS -fsanitize=${_imlui_sanitizers} -fno-omit-frame-pointer)
    if(IMLUI_FUZZ_SUPPORTED)
        list(APPEND IMLUI_SANITIZER_COMPILE_OPTIONS ${_imlui_fuzz_definitions})
    endif()
    set(IMLUI_SANITIZER_LINK_OPTIONS -fsanitize=${_imlui_sanitizers})
    if(IMLUI_SANITIZERS MATCHES "undefined")
        # Abort on the first UB report instead of logging and carrying on, so ctest fails.
        list(APPEND IMLUI_SANITIZER_COMPILE_OPTIONS -fno-sanitize-recover=undefined)
    endif()
    if(WIN32 AND IMLUI_SANITIZERS MATCHES "address")
        # ASan's stack-use-after-return instrumentation (a fake stack; off at run time by default anyway) breaks
        # throwing from a catch block on Windows: `throw;` or a new throw in a handler crashes ("stack-overflow").
        # Lua is compiled as C++, so a Lua error raised in a handler, or rethrown past one, is such a throw.
        list(APPEND IMLUI_SANITIZER_COMPILE_OPTIONS -fsanitize-address-use-after-return=never)
        # ASan's redzones make every frame several times larger (a level of the element tree: ~2 KiB in a Debug build,
        # ~8 KiB with ASan), and Windows gives a program's main thread 1 MiB of stack. 8 MiB lets a sanitized program
        # nest as deep as an unsanitized one (Renderer::maxDepth) before the renderer's stack check cuts the tree.
        list(APPEND IMLUI_SANITIZER_LINK_OPTIONS "LINKER:/STACK:8388608")
    endif()
    add_compile_options(${IMLUI_SANITIZER_COMPILE_OPTIONS})
    add_link_options(${IMLUI_SANITIZER_LINK_OPTIONS})

    if(WIN32 AND IMLUI_SANITIZERS MATCHES "address")
        # The runtime is named after the target's architecture (clang_rt.asan_dynamic-x86_64.dll, -i386, -aarch64),
        # or has no suffix in a per-target runtime folder (lib/clang/<v>/lib/<triple>/).
        set(_imlui_target_flags)
        if(CMAKE_CXX_COMPILER_TARGET)
            set(_imlui_target_flags --target=${CMAKE_CXX_COMPILER_TARGET})
        endif()
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_imlui_target_flags} -print-target-triple
                        OUTPUT_VARIABLE _imlui_triple OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "^[^-]+" _imlui_arch "${_imlui_triple}")
        if(_imlui_arch MATCHES "^i[3-6]86$")
            set(_imlui_arch i386)
        endif()
        set(IMLUI_ASAN_DLL)
        foreach(_imlui_name clang_rt.asan_dynamic-${_imlui_arch}.dll clang_rt.asan_dynamic.dll)
            execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_imlui_target_flags} -print-file-name=${_imlui_name}
                            OUTPUT_VARIABLE _imlui_found OUTPUT_STRIP_TRAILING_WHITESPACE)
            file(TO_CMAKE_PATH "${_imlui_found}" _imlui_found)
            # Not found: clang prints the name as given.
            if(IS_ABSOLUTE "${_imlui_found}" AND EXISTS "${_imlui_found}")
                set(IMLUI_ASAN_DLL ${_imlui_found})
                break()
            endif()
        endforeach()
        if(NOT IMLUI_ASAN_DLL)
            message(FATAL_ERROR "ASan runtime not found for ${_imlui_triple} (clang_rt.asan_dynamic-${_imlui_arch}.dll)")
        endif()
        # A multi-config generator puts executables in a folder per configuration under the output folder.
        get_property(_imlui_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY MATCHES [[\$<]])
            message(STATUS "imlui: CMAKE_RUNTIME_OUTPUT_DIRECTORY has a generator expression, so ${IMLUI_ASAN_DLL} "
                           "must be copied next to the executables or be on PATH")
        elseif(CMAKE_RUNTIME_OUTPUT_DIRECTORY AND _imlui_multi_config)
            foreach(_imlui_config IN LISTS CMAKE_CONFIGURATION_TYPES)
                file(COPY ${IMLUI_ASAN_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_imlui_config})
            endforeach()
        elseif(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
            file(COPY ${IMLUI_ASAN_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
        else()
            message(STATUS "imlui: no CMAKE_RUNTIME_OUTPUT_DIRECTORY, so ${IMLUI_ASAN_DLL} must be on PATH")
        endif()
    endif()
    message(STATUS "imlui: sanitizers ${_imlui_sanitizers}")
endif()
