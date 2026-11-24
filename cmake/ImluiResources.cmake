# Packing resource files into an executable (imlui_add_resources) or a .pak archive (imlui_add_pak), as a resource
# manifest lists them. The tools that do the work are imlui::imlui_rc and imlui::imlui_pack: built from tools/ when
# imlui is a subproject (add_subdirectory, FetchContent; aliases of imlui_rc and imlui_pack), imported executables of
# an installed package (find_package). See docs/architecture.md, "Resource manifests" and "Embedding and archives".
# imlui's CMakeLists.txt includes this file, and so does the installed package's imluiConfig.cmake, so an app calls
# these functions after add_subdirectory / FetchContent_MakeAvailable / find_package, on its own targets and manifests
# (relative paths are relative to the calling CMakeLists.txt). The options below are defined here for the same reason:
# they exist in the app's build in every mode.
#
#   imlui_add_resources(<target> <manifest> [NAME <name>] [EMBED ON|OFF])
#
#     Embeds the files of <manifest> (a Lua file, by convention resources.lua; a relative path is relative to the
#     current source dir) into <target>. Generates the module `imlui.embedded.<name>` (into the build tree) and adds
#     it to <target>; it exports `imlui::embedded::<name>::packages()`, the std::vector<imlui::ResourcePackage> to
#     pass to defaultConfig(): one per group of the manifest, with its files, its mount point and, when
#     IMLUI_DEV_PATHS is ON, the group's absolute base folder, which defaultConfig() mounts over the embedded copy
#     (hot reload; "" with IMLUI_DEV_PATHS=OFF). Also `mount(imlui::Resources&)`, which mounts them all.
#       NAME   the module and namespace name: lowercase letters, digits, `_` (default: the manifest's folder name,
#              made so)
#       EMBED  whether to embed the files (default: the option IMLUI_EMBED_RESOURCES, which is OFF in dev builds).
#              OFF generates the same module without the files: each package has only its mount point and its
#              source folder (`files` is null), defaultConfig() mounts the source folder, and `mount()` mounts the
#              source folders. The module then depends on the manifest only, so editing, adding or removing a
#              resource file needs no build. OFF needs IMLUI_DEV_PATHS=ON.
#     When embedding, imlui_rc writes a dependency file listing the manifest, every embedded file and every folder it
#     scanned for the manifest's patterns, and the target imlui_resources_<name>_list compares the manifest's file
#     list on every build, so editing the manifest or a file, or adding or removing a file in a scanned folder,
#     re-embeds; nothing is globbed at configure time. Each file is stored zstd-compressed when that saves at least
#     10 % (text, fonts), as is otherwise (PNG, JPEG, small files), unless the manifest's `compress` says otherwise,
#     and decompressed on first read.
#
#   imlui_add_pak(<name> <manifest> [OUTPUT <file>])
#
#     Adds the target <name> (built by `all`) that writes a .pak archive with the files of <manifest>, encoded the
#     same way and tracked the same way (dependency file, file list target <name>_list). The groups of the manifest
#     must share a prefix: the archive is mounted at the deepest folder holding all their mount points. OUTPUT
#     defaults to <name>.pak in the runtime output folder (CMAKE_RUNTIME_OUTPUT_DIRECTORY, else the current binary
#     dir), where defaultConfig() mounts every *.pak it finds next to the executable, at the mount point stored in the
#     archive.
include_guard(GLOBAL)

# Dev builds bake each embedded block's source folder into it (imlui_add_resources), and defaultConfig() mounts those
# folders over the embedded copies, so editing an app's scripts hot-reloads. OFF for a release: no source paths.
option(IMLUI_DEV_PATHS "Mount the source folders of embedded files over them (hot reload while editing)" ON)
# Whether imlui_add_resources embeds the files (unless a call says EMBED ON|OFF). Dev builds don't by default: they
# load the files from the source folders anyway, so editing a Lua file, an image or a font needs no build at all.
# Without embedding the source folders are the only copy, so OFF needs IMLUI_DEV_PATHS=ON.
if(IMLUI_DEV_PATHS)
    set(_imlui_embed_default OFF)
else()
    set(_imlui_embed_default ON)
endif()
option(IMLUI_EMBED_RESOURCES "Embed the files of resource manifests (default: OFF with IMLUI_DEV_PATHS, else ON)"
       ${_imlui_embed_default})
unset(_imlui_embed_default)
if(NOT IMLUI_EMBED_RESOURCES AND NOT IMLUI_DEV_PATHS)
    message(FATAL_ERROR "IMLUI_EMBED_RESOURCES=OFF needs IMLUI_DEV_PATHS=ON: without the embedded files and without "
                        "the source folders an executable would have no resources. For a build you ship, set "
                        "-DIMLUI_DEV_PATHS=OFF -DIMLUI_EMBED_RESOURCES=ON (a fresh build folder gets ON by default).")
endif()

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
        COMMAND imlui::imlui_rc --list ${manifest} ${file}
        BYPRODUCTS ${file}
        COMMENT "Checking the file list of ${manifest}"
        VERBATIM
    )
    set(${out} ${file} PARENT_SCOPE)
endfunction()

function(imlui_add_resources target manifest)
    cmake_parse_arguments(PARSE_ARGV 2 opt "" "NAME;EMBED" "")
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
    set(embed ${IMLUI_EMBED_RESOURCES})
    if(DEFINED opt_EMBED)
        set(embed ${opt_EMBED})
    endif()
    if(NOT embed AND NOT IMLUI_DEV_PATHS)
        message(FATAL_ERROR "imlui_add_resources(${target}): EMBED OFF needs IMLUI_DEV_PATHS=ON (the source folders "
                            "would be the only copy of the files)")
    endif()

    set(dir ${CMAKE_CURRENT_BINARY_DIR}/imlui_resources)
    if(embed)
        # Dev builds bake each group's base folder into the generated module (ResourcePackage::sourceDir):
        # defaultConfig() mounts it over the embedded copy, so editing a file hot-reloads.
        set(flags)
        if(IMLUI_DEV_PATHS)
            set(flags --dev-paths)
        endif()
        _imlui_manifest_listing(listing ${manifest} ${dir}/${name}.files imlui_resources_${name}_list)
        add_custom_command(
            OUTPUT ${dir}/${name}.cppm ${dir}/${name}.bin
            COMMAND imlui::imlui_rc ${flags} ${manifest} ${name} ${dir}
            DEPENDS imlui::imlui_rc ${manifest} ${listing}
            DEPFILE ${dir}/${name}.d
            COMMENT "Embedding ${manifest} (imlui.embedded.${name})"
            VERBATIM
        )
    else()
        # Only the mount points and source folders: regenerated when the manifest changes, never for its files.
        add_custom_command(
            OUTPUT ${dir}/${name}.cppm
            COMMAND imlui::imlui_rc --source-dirs ${manifest} ${name} ${dir}
            DEPENDS imlui::imlui_rc ${manifest}
            COMMENT "Listing the source folders of ${manifest} (imlui.embedded.${name}, not embedded)"
            VERBATIM
        )
    endif()
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
    if(NOT output AND CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(output ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name}.pak)
    elseif(NOT output)
        set(output ${CMAKE_CURRENT_BINARY_DIR}/${name}.pak)
    endif()
    cmake_path(ABSOLUTE_PATH output BASE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} NORMALIZE)
    cmake_path(GET output FILENAME file)
    set(depfile ${CMAKE_CURRENT_BINARY_DIR}/${name}.pak.d)
    _imlui_manifest_listing(listing ${manifest} ${CMAKE_CURRENT_BINARY_DIR}/${name}.pak.files ${name}_list)
    add_custom_command(
        OUTPUT ${output}
        COMMAND imlui::imlui_pack --depfile ${depfile} ${output} ${manifest}
        DEPENDS imlui::imlui_pack ${manifest} ${listing}
        DEPFILE ${depfile}
        COMMENT "Packing ${manifest} into ${file}"
        VERBATIM
    )
    add_custom_target(${name} ALL DEPENDS ${output})
endfunction()
