# Third-party code vendored into the project. Everything here is plain C/C++
# compiled as ordinary libraries; the rest of the project consumes it only
# through the C++20 module wrappers in src/wrap.

# ---------------------------------------------------------------- Lua 5.4 (C)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua)
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C)
if(WIN32)
    target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_library(ext::lua ALIAS lua_static)

# ---------------------------------------------------------------- GLFW 3.4
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------- Dear ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imgui_static STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imlui_imgui_assert.cpp
)
target_include_directories(imgui_static PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(imgui_static PUBLIC
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_USER_CONFIG="imlui_imconfig.h")
target_link_libraries(imgui_static PUBLIC glfw)
find_package(OpenGL REQUIRED)
target_link_libraries(imgui_static PUBLIC OpenGL::GL)
# Third-party code is not ours to scan for modules.
set_target_properties(imgui_static PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_library(ext::imgui ALIAS imgui_static)

set(IMLUI_FONTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fonts PARENT_SCOPE)

# ---------------------------------------------------------------- spdlog
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH   OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)
set_target_properties(spdlog PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_library(ext::spdlog ALIAS spdlog)

# ---------------------------------------------------------------- GoogleTest 1.17.0
# Only googletest/ is vendored (no gmock, docs, samples or tests). Built only when
# something links it (the unit tests), with the dynamic CRT like the rest of the project.
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK            OFF CACHE BOOL "" FORCE)
add_subdirectory(googletest EXCLUDE_FROM_ALL)
# SYSTEM: consumers see gtest headers as system headers (no warnings from them).
set_target_properties(gtest gtest_main PROPERTIES CXX_SCAN_FOR_MODULES OFF SYSTEM ON)
