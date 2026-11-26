# Install rules and the CMake package: `cmake --install <build> --prefix <prefix>` lays out a prefix that an app finds
# with find_package(imlui CONFIG) (CMAKE_PREFIX_PATH=<prefix>), or that FetchContent_Declare(... FIND_PACKAGE_ARGS)
# prefers to fetching. Included by the top-level CMakeLists.txt when IMLUI_INSTALL is ON (the default when imlui is
# the top-level project). See docs/architecture.md, "Install layout".
#
#   <prefix>/
#     bin/                       imlui_rc, imlui_pack                  (imported executables imlui::imlui_rc, ...)
#     lib/                       imlui, imlui_core, imlui_lua, imlui_glob (imlui::imlui, imlui::imlui_core, ...)
#     lib/imlui/                 the vendored static libraries         (imlui::ext_lua, ext_imgui, ext_glfw, ...)
#     lib/cmake/imlui/           imluiConfig.cmake, imluiConfigVersion.cmake, imluiTargets*.cmake, the helpers
#                                (ImluiResources.cmake, ImluiWarnings.cmake, ImluiStd.cmake: the config builds the
#                                module `std` in the app) and cxx-modules/ (module metadata)
#     include/imlui/modules/     the module interface units (src/ as it is laid out, embedded/imlui.cppm + .bin)
#     include/imlui/ext/<name>/  the vendored headers the modules include: lua, imgui, glfw, spdlog, stb, zstd
#     share/imlui/licenses/      the vendored code's and the fonts' licenses
#
# C++20 modules aren't installed as BMIs (they depend on the compiler and its flags): the interface units are, and an
# app's CMake (3.28+, Ninja) rebuilds their BMIs with the app's compiler, from the include folders, definitions and
# link libraries exported with them. That's why the vendored libraries the wrappers include are exported too, with
# their headers under include/imlui/ext/ (a private folder: they don't clash with another copy of the same library),
# and why an install carries the library's own files embedded (imlui_files_install, src/CMakeLists.txt): the generated
# module imlui.embedded.imlui and its blob are installed, the source folders aren't.
include(CMakePackageConfigHelpers)

set(IMLUI_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/imlui)
set(ext ${PROJECT_SOURCE_DIR}/external)

# The library (imlui, imlui_lua, imlui_glob) and its modules are installed by src/CMakeLists.txt: CMake records where
# a module file set is installed (for the exported module list) only from install rules in the target's own directory.

# ------------------------------------------------------------------ resource tools
install(TARGETS imlui_rc imlui_pack EXPORT imluiTargets RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ------------------------------------------------------------------ vendored libraries and their headers
install(TARGETS imlui_ext_lua imlui_ext_imgui glfw imlui_ext_spdlog imlui_ext_stb_image imlui_ext_zstd
        EXPORT imluiTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/imlui
)
install(FILES ${ext}/lua/lua.h ${ext}/lua/lualib.h ${ext}/lua/lauxlib.h ${ext}/lua/luaconf.h
              ${ext}/lua_cxx/imlui_lua_error.h ${ext}/lua_cxx/imlui_lua_state.h
        DESTINATION ${IMLUI_INSTALL_EXT_INCLUDEDIR}/lua)
install(FILES ${ext}/imgui/imgui.h ${ext}/imgui/imconfig.h ${ext}/imgui/imgui_internal.h
              ${ext}/imgui/imstb_rectpack.h ${ext}/imgui/imstb_textedit.h ${ext}/imgui/imstb_truetype.h
              ${ext}/imgui/backends/imgui_impl_glfw.h ${ext}/imgui/backends/imgui_impl_opengl3.h
              ${ext}/imlui_imconfig.h
        DESTINATION ${IMLUI_INSTALL_EXT_INCLUDEDIR}/imgui)
install(DIRECTORY ${ext}/glfw/include/GLFW DESTINATION ${IMLUI_INSTALL_EXT_INCLUDEDIR}/glfw)
install(DIRECTORY ${ext}/spdlog/include/spdlog DESTINATION ${IMLUI_INSTALL_EXT_INCLUDEDIR}/spdlog)
install(FILES ${ext}/stb/stb_image.h DESTINATION ${IMLUI_INSTALL_EXT_INCLUDEDIR}/stb)
install(FILES ${ext}/zstd/lib/zstd.h ${ext}/zstd/lib/zstd_errors.h DESTINATION ${IMLUI_INSTALL_EXT_INCLUDEDIR}/zstd)

set(licenses ${CMAKE_INSTALL_DATADIR}/imlui/licenses)
install(FILES ${ext}/lua/README.md DESTINATION ${licenses} RENAME lua.md)
install(FILES ${ext}/imgui/LICENSE.txt DESTINATION ${licenses} RENAME imgui.txt)
install(FILES ${ext}/glfw/LICENSE.md DESTINATION ${licenses} RENAME glfw.md)
install(FILES ${ext}/spdlog/LICENSE DESTINATION ${licenses} RENAME spdlog.txt)
install(FILES ${ext}/stb/LICENSE DESTINATION ${licenses} RENAME stb.txt)
install(FILES ${ext}/zstd/LICENSE DESTINATION ${licenses} RENAME zstd.txt)
install(FILES ${ext}/fonts/LICENSE-Roboto.txt ${ext}/fonts/LICENSE-MaterialIcons.txt DESTINATION ${licenses})

# ------------------------------------------------------------------ the package
install(EXPORT imluiTargets
    NAMESPACE imlui::
    DESTINATION ${IMLUI_INSTALL_CMAKEDIR}
    CXX_MODULES_DIRECTORY cxx-modules
)
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/imluiConfig.cmake.in
    ${PROJECT_BINARY_DIR}/imluiConfig.cmake
    INSTALL_DESTINATION ${IMLUI_INSTALL_CMAKEDIR}
)
# 0.x: a minor version may break the API, so only the same major.minor is compatible.
write_basic_package_version_file(${PROJECT_BINARY_DIR}/imluiConfigVersion.cmake COMPATIBILITY SameMinorVersion)
install(FILES ${PROJECT_BINARY_DIR}/imluiConfig.cmake ${PROJECT_BINARY_DIR}/imluiConfigVersion.cmake
              ${PROJECT_SOURCE_DIR}/cmake/ImluiResources.cmake ${PROJECT_SOURCE_DIR}/cmake/ImluiWarnings.cmake
              ${PROJECT_SOURCE_DIR}/cmake/ImluiStd.cmake
        DESTINATION ${IMLUI_INSTALL_CMAKEDIR})
