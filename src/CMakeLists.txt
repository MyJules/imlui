# The library targets: imlui_lua (the Lua wrapper) and imlui (every other module under src/, plus its own files from
# resources/), with the alias imlui::imlui, the name apps link in every mode (an installed package exports imlui as
# imlui::imlui). Included by the top-level CMakeLists.txt with add_subdirectory(src); paths here are relative to this
# folder (CMAKE_CURRENT_SOURCE_DIR).

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
set_target_properties(imlui_lua PROPERTIES EXPORT_NAME imlui_lua)
imlui_set_warnings(imlui_lua)
# imlui_set_hardening's -ftrapv is part of how a module is compiled: clang refuses to import a BMI built with a
# different setting. So for the library it's a usage requirement (imlui links imlui_lua PUBLIC): whatever imports its
# modules in Debug is compiled with it too, and the BMIs an app rebuilds from an installed package match the app.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(imlui_lua PUBLIC $<$<CONFIG:Debug>:-ftrapv>)
endif()

# ------------------------------------------------------------------ library
add_library(imlui STATIC)
add_library(imlui::imlui ALIAS imlui)
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
imlui_set_hardening(imlui) # -ftrapv in Debug, which imlui_lua already makes a usage requirement
# The library carries its own files (imlui.embedded.imlui: resources/ at imlui:/ and the fonts from external/fonts at
# imlui:/fonts/, listed in resources/resources.lua), so an app runs without them next to it; defaultConfig() always
# mounts them, under the .pak archives and folders that override them. Embedded as IMLUI_EMBED_RESOURCES says, except
# with IMLUI_INSTALL=ON: an installed library has no source folders to read, so it always embeds them.
# IMLUI_LIBRARY_EMBEDS tells the tests which it is.
set(IMLUI_LIBRARY_EMBEDS ${IMLUI_EMBED_RESOURCES})
if(IMLUI_INSTALL)
    set(IMLUI_LIBRARY_EMBEDS ON)
endif()
set(IMLUI_LIBRARY_EMBEDS ${IMLUI_LIBRARY_EMBEDS} PARENT_SCOPE)
imlui_add_resources(imlui resources/resources.lua NAME imlui EMBED ${IMLUI_LIBRARY_EMBEDS})

# ------------------------------------------------------------------ install (IMLUI_INSTALL, cmake/ImluiInstall.cmake)
# Here rather than in ImluiInstall.cmake: CMake records where a module file set is installed (the module list it
# exports) only from install rules in the target's own directory. The modules keep src/'s layout; the generated
# imlui.embedded.imlui goes to embedded/, with the blob it #embeds next to it (it names it so).
if(IMLUI_INSTALL)
    install(TARGETS imlui imlui_lua EXPORT imluiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        FILE_SET CXX_MODULES DESTINATION ${IMLUI_INSTALL_MODULEDIR}
        FILE_SET imlui_resources_imlui DESTINATION ${IMLUI_INSTALL_MODULEDIR}/embedded
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/imlui_resources/imlui.bin DESTINATION ${IMLUI_INSTALL_MODULEDIR}/embedded)
endif()
