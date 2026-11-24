# The library targets, included by the top-level CMakeLists.txt with add_subdirectory(src); paths here are relative to
# this folder (CMAKE_CURRENT_SOURCE_DIR):
#
#   imlui_lua    the Lua wrapper (module `lua`)
#   imlui_core   every other module under src/
#   imlui        what apps link in the build tree, with the alias imlui::imlui: the library's own files
#                (resources/resources.lua, the generated module imlui.embedded.imlui), linking imlui_core PUBLIC
#
# An installed package exports imlui_core as imlui::imlui (the modules: CMake only builds the BMIs of the modules an
# imported target holds itself, so that's the target an app must link), imlui_lua as imlui::imlui_lua and an embedded
# copy of the files (imlui_files_install, below) as imlui::imlui_files, which imlui::imlui links.

# ------------------------------------------------------------------ the standard library (module `std`)
# Every module imports std instead of including the standard headers (cmake/ImluiStd.cmake). Built from the standard
# library's own module source, with imlui_lua's ABI flags (-ftrapv in Debug, the sanitizers): a BMI compiled
# differently can't be imported. Not installed: imluiConfig.cmake builds it again in the app, with the app's compiler,
# STL and flags. Everything links it through imlui_lua (IMLUI_STD_LINK keeps the installed targets pointing at the
# app's copy, imlui::std).
imlui_add_std_module(imlui_std ABI_FROM imlui_lua)
set(IMLUI_STD_LINK $<BUILD_INTERFACE:imlui_std> $<INSTALL_INTERFACE:imlui::std>)

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
target_link_libraries(imlui_lua PUBLIC ${IMLUI_STD_LINK} PRIVATE ext::lua)
target_compile_features(imlui_lua PUBLIC cxx_std_20)
set_target_properties(imlui_lua PROPERTIES EXPORT_NAME imlui_lua)
imlui_set_warnings(imlui_lua)
# imlui_set_hardening's -ftrapv is part of how a module is compiled: clang refuses to import a BMI built with a
# different setting. So for the library it's a usage requirement (imlui_core links imlui_lua PUBLIC): whatever imports
# its modules in Debug is compiled with it too, and the BMIs an app rebuilds from an installed package match the app.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(imlui_lua PUBLIC $<$<CONFIG:Debug>:-ftrapv>)
endif()
# The same for sanitizers (IMLUI_SANITIZERS, cmake/ImluiSanitizers.cmake): a module compiled with -fsanitize=address
# can't be imported by a translation unit compiled without it, and its objects need the sanitizer runtime at link time.
# So every target that links imlui gets the flags, the app's own targets included, however imlui was added.
if(IMLUI_SANITIZER_COMPILE_OPTIONS)
    target_compile_options(imlui_lua INTERFACE ${IMLUI_SANITIZER_COMPILE_OPTIONS})
    target_link_options(imlui_lua INTERFACE ${IMLUI_SANITIZER_LINK_OPTIONS})
endif()

# ------------------------------------------------------------------ library (modules)
add_library(imlui_core STATIC)
file(GLOB_RECURSE IMLUI_MODULES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cppm)
list(REMOVE_ITEM IMLUI_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/wrap/lua.cppm)
target_sources(imlui_core
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${IMLUI_MODULES}
)
target_link_libraries(imlui_core PUBLIC imlui_lua PRIVATE ext::lua ext::imgui ext::spdlog ext::stb_image ext::zstd)
# imlui.platform.window colors the title bar through DWM (DwmSetWindowAttribute).
if(WIN32)
    target_link_libraries(imlui_core PRIVATE dwmapi)
endif()
target_compile_features(imlui_core PUBLIC cxx_std_20)
imlui_set_warnings(imlui_core)
imlui_set_hardening(imlui_core) # -ftrapv in Debug, which imlui_lua already makes a usage requirement

# ------------------------------------------------------------------ library (its own files): imlui
# The library carries its own files (imlui.embedded.imlui: resources/ at imlui:/ and the fonts from external/fonts at
# imlui:/fonts/, listed in resources/resources.lua), so an app runs without them next to it; defaultConfig() always
# mounts them, under the .pak archives and folders that override them. They're not part of imlui_core: imlui_core
# declares defaultConfig() and the target that holds the generated module defines it (imlui/app/default_config.cpp,
# an implementation unit of imlui.app.config). So there are two variants without compiling the library twice:
#
#   imlui                linked in the build tree (the tests, an app that adds imlui as a subproject). Embeds as
#                        IMLUI_EMBED_RESOURCES says (OFF in dev builds: the source folders are mounted instead, and
#                        editing them needs no build), with source folders as IMLUI_DEV_PATHS says. Its module is
#                        public: the tests import imlui.embedded.imlui.
#   imlui_files_install  with IMLUI_INSTALL=ON: installed as imlui::imlui_files, always embedded and without source
#                        folders (an installed library can't rely on imlui's checkout). Built by `all`, but nothing in
#                        the build links it, so editing resources/ re-embeds this one target only. Its module is
#                        private: the installed package holds the compiled blob, not a module to import.
function(_imlui_add_library_files target embed dev_paths)
    add_library(${target} STATIC)
    target_sources(${target} PRIVATE imlui/app/default_config.cpp)
    imlui_set_warnings(${target})
    imlui_set_hardening(${target})
    imlui_add_resources(${target} resources/resources.lua NAME imlui EMBED ${embed} DEV_PATHS ${dev_paths} ${ARGN})
endfunction()

_imlui_add_library_files(imlui ${IMLUI_EMBED_RESOURCES} ${IMLUI_DEV_PATHS})
target_link_libraries(imlui PUBLIC imlui_core)
add_library(imlui::imlui ALIAS imlui)

if(IMLUI_INSTALL)
    _imlui_add_library_files(imlui_files_install ON OFF PRIVATE)
    # PRIVATE: exported as $<LINK_ONLY:imlui::imlui>, so the imported targets imlui::imlui and imlui::imlui_files link
    # each other (a cycle CMake resolves by repeating them on the link line) without being each other's usage
    # requirement. imlui::imlui links imlui::imlui_files, where its defaultConfig() is.
    target_link_libraries(imlui_files_install PRIVATE imlui_core)
    set_target_properties(imlui_files_install PROPERTIES EXPORT_NAME imlui_files OUTPUT_NAME imlui_files)
    set_target_properties(imlui_core PROPERTIES EXPORT_NAME imlui)
    target_link_libraries(imlui_core INTERFACE $<INSTALL_INTERFACE:imlui::imlui_files>)

    # Here rather than in ImluiInstall.cmake: CMake records where a module file set is installed (the module list it
    # exports) only from install rules in the target's own directory. The modules keep src/'s layout.
    install(TARGETS imlui_core imlui_lua imlui_files_install EXPORT imluiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        FILE_SET CXX_MODULES DESTINATION ${IMLUI_INSTALL_MODULEDIR}
    )
endif()
