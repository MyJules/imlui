# Packing resource files into an executable (imlui_add_resources) or a .pak archive (imlui_add_pak).
# The tools that do the work are built from tools/ (imlui_rc, imlui_pack). See docs/architecture.md, "Resources".
#
#   imlui_add_resources(<target> PREFIX <prefix> BASE <dir>
#                       [NAME <name>] [MOUNT <folder>] [FILES <file>...] [DIRECTORY <dir>...])
#
#     Embeds files into <target>. Generates the module `imlui.embedded.<name>` (into the build tree) and adds it to
#     <target>; it exports `imlui::embedded::<name>::mount(imlui::Resources&)`, which mounts the files at
#     `<prefix>:/` (or `<prefix>:/<folder>/` with MOUNT), and `files()`, the EmbeddedMount itself.
#       PREFIX     the resource prefix: `app`, `imlui`, a library's own
#       BASE       the folder the resource paths are relative to (relative paths: to the current source dir)
#       NAME       the module and namespace name: lowercase letters, digits, `_` (default: <target>, made so)
#       FILES      files, relative to BASE (or absolute, under BASE)
#       DIRECTORY  folders, relative to BASE: every file below them, recursively (re-globbed on each build)
#     Editing a listed file re-embeds it. Each file is stored zstd-compressed when that saves at least 10 % (text,
#     fonts), as is otherwise (PNG, JPEG, small files), and decompressed on first read.
#
#   imlui_add_pak(<name> PREFIX <prefix> BASE <dir>
#                 [OUTPUT <file>] [MOUNT <folder>] [FILES <file>...] [DIRECTORY <dir>...])
#
#     Adds the target <name> (built by `all`) that writes a .pak archive with the same files and encoding. OUTPUT
#     defaults to <name>.pak in the runtime output folder (bin/), where defaultConfig() mounts every *.pak it finds
#     next to the executable, at the mount point stored in the archive.

# Parses the shared arguments into a spec file for the tools. Sets <out>_SPEC, <out>_FILES and <out>_POINT.
function(_imlui_resource_spec out name)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "PREFIX;MOUNT;BASE;OUTPUT;NAME" "FILES;DIRECTORY")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "imlui resources '${name}': unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_PREFIX MATCHES "^[a-z][a-z0-9_-]+$")
        message(FATAL_ERROR "imlui resources '${name}': PREFIX '${arg_PREFIX}' must be lowercase, at least 2 characters")
    endif()
    if(NOT arg_BASE)
        message(FATAL_ERROR "imlui resources '${name}': BASE is required")
    endif()
    cmake_path(ABSOLUTE_PATH arg_BASE BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} NORMALIZE OUTPUT_VARIABLE base)

    set(point "${arg_PREFIX}:/")
    if(arg_MOUNT)
        string(REGEX REPLACE "^/+|/+$" "" mount "${arg_MOUNT}")
        set(point "${point}${mount}/")
    endif()

    set(files)
    foreach(file IN LISTS arg_FILES)
        cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY ${base} NORMALIZE OUTPUT_VARIABLE file)
        list(APPEND files ${file})
    endforeach()
    foreach(dir IN LISTS arg_DIRECTORY)
        cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY ${base} NORMALIZE OUTPUT_VARIABLE dir)
        file(GLOB_RECURSE found CONFIGURE_DEPENDS LIST_DIRECTORIES false ${dir}/*)
        list(APPEND files ${found})
    endforeach()
    list(REMOVE_DUPLICATES files)
    list(SORT files)

    set(content "name=${name}\nmount=${point}\nbase=${base}\n")
    foreach(file IN LISTS files)
        string(APPEND content "file=${file}\n")
    endforeach()
    set(spec ${CMAKE_CURRENT_BINARY_DIR}/imlui_resources/${name}.spec)
    file(GENERATE OUTPUT ${spec} CONTENT "${content}")

    set(${out}_SPEC ${spec} PARENT_SCOPE)
    set(${out}_FILES ${files} PARENT_SCOPE)
    set(${out}_POINT ${point} PARENT_SCOPE)
    set(${out}_OUTPUT ${arg_OUTPUT} PARENT_SCOPE)
endfunction()

function(imlui_add_resources target)
    cmake_parse_arguments(PARSE_ARGV 1 opt "" "NAME" "")
    set(name ${opt_NAME})
    if(NOT name)
        string(TOLOWER ${target} name)
        string(REGEX REPLACE "[^a-z0-9_]" "_" name ${name})
    endif()
    if(NOT name MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "imlui_add_resources(${target}): NAME '${name}' must be lowercase letters, digits and _")
    endif()
    _imlui_resource_spec(res ${name} ${ARGN})

    set(dir ${CMAKE_CURRENT_BINARY_DIR}/imlui_resources)
    add_custom_command(
        OUTPUT ${dir}/${name}.cppm ${dir}/${name}.bin
        COMMAND imlui_rc ${res_SPEC} ${dir}
        DEPENDS imlui_rc ${res_SPEC} ${res_FILES}
        COMMENT "Embedding ${res_POINT} (imlui.embedded.${name})"
        VERBATIM
    )
    # A library's embedded module is part of its interface (the tests import imlui.embedded.imlui); an
    # executable's file sets must be private.
    get_target_property(type ${target} TYPE)
    set(scope PUBLIC)
    if(type STREQUAL "EXECUTABLE")
        set(scope PRIVATE)
    endif()
    target_sources(${target} ${scope} FILE_SET imlui_resources_${name} TYPE CXX_MODULES BASE_DIRS ${dir}
                   FILES ${dir}/${name}.cppm)
endfunction()

function(imlui_add_pak name)
    if(NOT name MATCHES "^[A-Za-z0-9_.-]+$")
        message(FATAL_ERROR "imlui_add_pak(${name}): the name must be a plain file name")
    endif()
    _imlui_resource_spec(res ${name}_pak ${ARGN})
    set(output ${res_OUTPUT})
    if(NOT output)
        set(output ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name}.pak)
    endif()
    cmake_path(ABSOLUTE_PATH output BASE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} NORMALIZE)
    cmake_path(GET output FILENAME file)
    add_custom_command(
        OUTPUT ${output}
        COMMAND imlui_pack ${output} @${res_SPEC}
        DEPENDS imlui_pack ${res_SPEC} ${res_FILES}
        COMMENT "Packing ${res_POINT} into ${file}"
        VERBATIM
    )
    add_custom_target(${name} ALL DEPENDS ${output})
endfunction()
