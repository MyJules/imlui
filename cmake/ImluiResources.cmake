# Packing resource files into an executable (imlui_add_resources) or a .pak archive (imlui_add_pak), as a resource
# manifest lists them. The tools that do the work are built from tools/ (imlui_rc, imlui_pack). See
# docs/architecture.md, "Resource manifests" and "Embedding and archives".
#
#   imlui_add_resources(<target> <manifest> [NAME <name>])
#
#     Embeds the files of <manifest> (a Lua file, by convention resources.lua; a relative path is relative to the
#     current source dir) into <target>. Generates the module `imlui.embedded.<name>` (into the build tree) and adds
#     it to <target>; it exports `imlui::embedded::<name>::packages()`, the std::vector<imlui::ResourcePackage> to
#     pass to defaultConfig(): one per group of the manifest, with its files, its mount point and, when
#     IMLUI_DEV_PATHS is ON, the group's absolute base folder, which defaultConfig() mounts over the embedded copy
#     (hot reload; "" with IMLUI_DEV_PATHS=OFF). Also `mount(imlui::Resources&)`, which mounts them all.
#       NAME  the module and namespace name: lowercase letters, digits, `_` (default: the manifest's folder name,
#             made so)
#     imlui_rc writes a dependency file listing the manifest, every embedded file and every folder it scanned for
#     the manifest's patterns, and the target imlui_resources_<name>_list compares the manifest's file list on every
#     build, so editing the manifest or a file, or adding or removing a file in a scanned folder, re-embeds; nothing
#     is globbed at configure time. Each file is stored zstd-compressed when that saves at least
#     10 % (text, fonts), as is otherwise (PNG, JPEG, small files), unless the manifest's `compress` says otherwise,
#     and decompressed on first read.
#
#   imlui_add_pak(<name> <manifest> [OUTPUT <file>])
#
#     Adds the target <name> (built by `all`) that writes a .pak archive with the files of <manifest>, encoded the
#     same way and tracked the same way (dependency file, file list target <name>_list). The groups of the manifest must share a prefix: the
#     archive is mounted at the deepest folder holding all their mount points. OUTPUT defaults to <name>.pak in the
#     runtime output folder (bin/), where defaultConfig() mounts every *.pak it finds next to the executable, at the
#     mount point stored in the archive.

# The absolute path of manifest argument <manifest> (relative to the current source dir), in <out>.
function(_imlui_manifest_path out caller manifest)
    if(NOT manifest)
        message(FATAL_ERROR "${caller}: the manifest file is required")
    endif()
    cmake_path(ABSOLUTE_PATH manifest BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} NORMALIZE OUTPUT_VARIABLE path)
    if(NOT EXISTS ${path})
        message(FATAL_ERROR "${caller}: manifest '${path}' doesn't exist")
    endif()
    set(${out} ${path} PARENT_SCOPE)
endfunction()

# Adds the target <target>, run on every build, that writes the file list of <manifest> to <file> when it changed
# (`imlui_rc --list`), and sets <out> to <file>, an input for the command that packs the manifest. The dependency file
# lists the scanned folders too, but Ninja on Windows can miss a file added to one: it reads folder times from the
# parent folder's listing, which NTFS updates lazily. Comparing the listing is cheap (no file is read).
function(_imlui_manifest_listing out manifest file target)
    add_custom_target(${target}
        COMMAND imlui_rc --list ${manifest} ${file}
        BYPRODUCTS ${file}
        COMMENT "Checking the file list of ${manifest}"
        VERBATIM
    )
    set(${out} ${file} PARENT_SCOPE)
endfunction()

function(imlui_add_resources target manifest)
    cmake_parse_arguments(PARSE_ARGV 2 opt "" "NAME" "")
    if(opt_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "imlui_add_resources(${target}): unknown arguments ${opt_UNPARSED_ARGUMENTS}")
    endif()
    _imlui_manifest_path(manifest "imlui_add_resources(${target})" "${manifest}")
    set(name ${opt_NAME})
    if(NOT name)
        cmake_path(GET manifest PARENT_PATH folder)
        cmake_path(GET folder FILENAME name)
        string(TOLOWER "${name}" name)
        string(REGEX REPLACE "[^a-z0-9_]" "_" name "${name}")
    endif()
    if(NOT name MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "imlui_add_resources(${target}): NAME '${name}' must be lowercase letters, digits and _")
    endif()

    # Dev builds bake each group's base folder into the generated module (ResourcePackage::sourceDir):
    # defaultConfig() mounts it over the embedded copy, so editing a file hot-reloads.
    set(flags)
    if(IMLUI_DEV_PATHS)
        set(flags --dev-paths)
    endif()
    set(dir ${CMAKE_CURRENT_BINARY_DIR}/imlui_resources)
    _imlui_manifest_listing(listing ${manifest} ${dir}/${name}.files imlui_resources_${name}_list)
    add_custom_command(
        OUTPUT ${dir}/${name}.cppm ${dir}/${name}.bin
        COMMAND imlui_rc ${flags} ${manifest} ${name} ${dir}
        DEPENDS imlui_rc ${manifest} ${listing}
        DEPFILE ${dir}/${name}.d
        COMMENT "Embedding ${manifest} (imlui.embedded.${name})"
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

function(imlui_add_pak name manifest)
    cmake_parse_arguments(PARSE_ARGV 2 opt "" "OUTPUT" "")
    if(opt_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "imlui_add_pak(${name}): unknown arguments ${opt_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT name MATCHES "^[A-Za-z0-9_.-]+$")
        message(FATAL_ERROR "imlui_add_pak(${name}): the name must be a plain file name")
    endif()
    _imlui_manifest_path(manifest "imlui_add_pak(${name})" "${manifest}")
    set(output ${opt_OUTPUT})
    if(NOT output)
        set(output ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name}.pak)
    endif()
    cmake_path(ABSOLUTE_PATH output BASE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} NORMALIZE)
    cmake_path(GET output FILENAME file)
    set(depfile ${CMAKE_CURRENT_BINARY_DIR}/${name}.pak.d)
    _imlui_manifest_listing(listing ${manifest} ${CMAKE_CURRENT_BINARY_DIR}/${name}.pak.files ${name}_list)
    add_custom_command(
        OUTPUT ${output}
        COMMAND imlui_pack --depfile ${depfile} ${output} ${manifest}
        DEPENDS imlui_pack ${manifest} ${listing}
        DEPFILE ${depfile}
        COMMENT "Packing ${manifest} into ${file}"
        VERBATIM
    )
    add_custom_target(${name} ALL DEPENDS ${output})
endfunction()
