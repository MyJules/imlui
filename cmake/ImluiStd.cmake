# The standard library module (`import std;`). Every imlui module and tool imports it instead of including the
# standard headers. CMake's own `import std` support (CMAKE_CXX_MODULE_STD) is experimental and doesn't cover clang on
# the MSVC STL (x86_64-pc-windows-msvc), so imlui builds the module itself, from the module source the standard
# library ships:
#
#   MSVC STL   <VC tools>/modules/std.ixx, next to the include folder in the compiler's header search list
#   libc++     the `std` entry of libc++.modules.json (clang -print-file-name=libc++.modules.json)
#   libstdc++  the `std` entry of libstdc++.modules.json
#
# IMLUI_STD_MODULE_SOURCE overrides the search with a path to the module source (e.g. a different STL's std.ixx).
#
#   imlui_add_std_module(<target> [ABI_FROM <target>])
#
# Adds the static library <target> with the module `std`, unless it exists already. Clang refuses to import a BMI
# compiled with a different -ftrapv, and silently accepts other mismatches (sanitizers, the CRT macros) that are no
# safer, so `std` must be compiled the way its importers are: ABI_FROM names the target whose usage requirements give
# those flags (imlui_lua: $<$<CONFIG:Debug>:-ftrapv> and the sanitizers), and <target> is compiled with its
# INTERFACE_COMPILE_OPTIONS. That's also why an installed package doesn't install `std`: imluiConfig.cmake calls this
# again, so the app builds it with its own compiler, its own STL and its own flags, as it builds imlui's other BMIs.
include_guard(GLOBAL)

set(IMLUI_STD_MODULE_SOURCE "" CACHE FILEPATH
    "The standard library's module source for `import std` (empty: found from the compiler)")

# Sets <out_source> to the module source of `std` and <out_includes> to the system include folders it needs.
function(_imlui_find_std_module out_source out_includes)
    set(includes "")
    if(IMLUI_STD_MODULE_SOURCE)
        set(source "${IMLUI_STD_MODULE_SOURCE}")
    elseif(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        # The MSVC STL: <VC tools>/include is in the compiler's header search list (which CMake doesn't record for a
        # clang that targets MSVC, so ask the compiler), and std.ixx is in <VC tools>/modules.
        if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            set(search_dirs ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES} $ENV{INCLUDE})
        else()
            set(probe ${CMAKE_BINARY_DIR}/CMakeFiles/imlui_std_probe.cpp)
            file(WRITE ${probe} "")
            execute_process(COMMAND ${CMAKE_CXX_COMPILER} -E -v ${probe} OUTPUT_QUIET ERROR_VARIABLE verbose)
            string(REGEX MATCH "#include <...> search starts here:\n(.*)End of search list" _ "${verbose}")
            string(REPLACE "\n" ";" search_dirs "${CMAKE_MATCH_1}")
        endif()
        foreach(dir IN LISTS search_dirs)
            string(STRIP "${dir}" dir)
            file(TO_CMAKE_PATH "${dir}" dir)
            if(dir AND EXISTS "${dir}/yvals_core.h" AND EXISTS "${dir}/../modules/std.ixx")
                cmake_path(SET source NORMALIZE "${dir}/../modules/std.ixx")
                break()
            endif()
        endforeach()
    else()
        # libc++ or libstdc++: the compiler's <library>.modules.json lists the module sources.
        foreach(library libc++ libstdc++)
            execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${library}.modules.json
                OUTPUT_VARIABLE json_file OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT IS_ABSOLUTE "${json_file}" OR NOT EXISTS "${json_file}")
                continue()
            endif()
            get_filename_component(json_dir "${json_file}" DIRECTORY)
            file(READ "${json_file}" json)
            string(JSON count LENGTH "${json}" modules)
            math(EXPR last "${count} - 1")
            foreach(i RANGE 0 ${last})
                string(JSON name GET "${json}" modules ${i} logical-name)
                if(NOT name STREQUAL "std")
                    continue()
                endif()
                string(JSON source GET "${json}" modules ${i} source-path)
                if(NOT IS_ABSOLUTE "${source}")
                    set(source "${json_dir}/${source}")
                endif()
                string(JSON dirs ERROR_VARIABLE no_dirs GET "${json}" modules ${i} local-arguments
                       system-include-directories)
                if(NOT no_dirs)
                    string(JSON ndirs LENGTH "${dirs}")
                    if(ndirs GREATER 0)
                        math(EXPR last_dir "${ndirs} - 1")
                        foreach(d RANGE 0 ${last_dir})
                            string(JSON dir GET "${dirs}" ${d})
                            if(NOT IS_ABSOLUTE "${dir}")
                                set(dir "${json_dir}/${dir}")
                            endif()
                            list(APPEND includes "${dir}")
                        endforeach()
                    endif()
                endif()
                break()
            endforeach()
            if(source)
                break()
            endif()
        endforeach()
    endif()

    if(NOT source OR NOT EXISTS "${source}")
        message(FATAL_ERROR "imlui: can't find the standard library's module source for `import std` (the MSVC "
                            "STL's modules/std.ixx, or libc++/libstdc++ built with modules). Set "
                            "IMLUI_STD_MODULE_SOURCE to its path.")
    endif()
    set(${out_source} "${source}" PARENT_SCOPE)
    set(${out_includes} "${includes}" PARENT_SCOPE)
endfunction()

function(imlui_add_std_module target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ABI_FROM" "")
    if(TARGET ${target})
        return()
    endif()
    _imlui_find_std_module(source includes)
    get_filename_component(source_dir "${source}" DIRECTORY)
    message(STATUS "imlui: import std from ${source}")

    add_library(${target} STATIC)
    target_sources(${target} PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${source_dir} FILES ${source})
    set_source_files_properties(${source} PROPERTIES LANGUAGE CXX) # .ixx isn't a C++ extension to every generator
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(includes)
        target_include_directories(${target} SYSTEM PRIVATE ${includes})
    endif()
    # The module source is the standard library's, not ours (a reserved module name, standard headers in its purview:
    # clang warns about both). -w rather than the two -Wno-... flags: clang-scan-deps ignores those, not -w.
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
    if(arg_ABI_FROM)
        target_compile_options(${target} PRIVATE "$<TARGET_PROPERTY:${arg_ABI_FROM},INTERFACE_COMPILE_OPTIONS>")
    endif()
endfunction()
