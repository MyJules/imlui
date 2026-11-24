# Build-time resource tools (see cmake/ImluiResources.cmake and docs/architecture.md, "Resources"):
#
#   imlui_rc    embeds files into a generated C++ module (imlui_add_resources). Doesn't depend on the imlui library,
#               which embeds its own Lua and fonts with it.
#   imlui_pack  builds .pak archives (imlui_add_pak). Uses the archive format from the imlui library.
#
# Both encode files the same way (imlui.tools.resource_encoding: zstd when it saves enough). They run on the build
# machine, so they are built with the same toolchain as everything else (no cross-compiling).

add_library(imlui_resource_tools STATIC)
target_sources(imlui_resource_tools
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/lib
        FILES
            lib/zstd_compress.cppm
            lib/resource_encoding.cppm
            lib/resource_spec.cppm
            lib/rc.cppm
)
target_link_libraries(imlui_resource_tools PRIVATE ext::zstd_compress)
target_compile_features(imlui_resource_tools PUBLIC cxx_std_20)
imlui_set_warnings(imlui_resource_tools)
imlui_set_hardening(imlui_resource_tools)

add_executable(imlui_rc rc.cpp)
target_link_libraries(imlui_rc PRIVATE imlui_resource_tools)
imlui_set_warnings(imlui_rc)
imlui_set_hardening(imlui_rc)

# The .pak writer is a library too, so the tests can round-trip archives through it.
add_library(imlui_pack_lib STATIC)
target_sources(imlui_pack_lib
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES pack.cppm
)
target_link_libraries(imlui_pack_lib PUBLIC imlui imlui_resource_tools)
imlui_set_warnings(imlui_pack_lib)
imlui_set_hardening(imlui_pack_lib)

add_executable(imlui_pack pack.cpp)
target_link_libraries(imlui_pack PRIVATE imlui_pack_lib)
imlui_set_warnings(imlui_pack)
imlui_set_hardening(imlui_pack)
