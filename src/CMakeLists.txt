# The library targets: imlui_lua (the Lua wrapper) and imlui (every other module under src/, plus its own files from
# resources/, embedded). Included by the top-level CMakeLists.txt with add_subdirectory(src); paths here are relative
# to this folder (CMAKE_CURRENT_SOURCE_DIR).

# ------------------------------------------------------------------ Lua wrapper (module `lua`)
# Its own library: the resource tools evaluate manifests with it, and imlui_rc can't depend on the imlui library,
# which embeds its own files with imlui_rc.
add_library(imlui_lua STATIC)
target_sources(imlui_lua
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES wrap/lua.cppm
)
target_link_libraries(imlui_lua PRIVATE ext::lua)
target_compile_features(imlui_lua PUBLIC cxx_std_20)
imlui_set_warnings(imlui_lua)
imlui_set_hardening(imlui_lua)

# ------------------------------------------------------------------ library
add_library(imlui STATIC)
file(GLOB_RECURSE IMLUI_MODULES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cppm)
list(REMOVE_ITEM IMLUI_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/wrap/lua.cppm)
target_sources(imlui
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${IMLUI_MODULES}
)
target_link_libraries(imlui PUBLIC imlui_lua PRIVATE ext::lua ext::imgui ext::spdlog ext::stb_image ext::zstd)
target_compile_features(imlui PUBLIC cxx_std_20)
imlui_set_warnings(imlui)
imlui_set_hardening(imlui)
# The library carries its own files (imlui.embedded.imlui: resources/ at imlui:/ and the fonts from external/fonts at
# imlui:/fonts/, listed in resources/resources.lua), so an app runs without them next to it; defaultConfig() always
# mounts them, under the .pak archives and folders that override them.
imlui_add_resources(imlui resources/resources.lua NAME imlui)
