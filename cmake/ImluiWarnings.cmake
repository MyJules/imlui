# Warning policy and debug hardening for imlui's own targets (imlui, the tools, the tests), and for an app's targets:
# an app that adds imlui as a subproject calls these on its own targets too.
# Vendored code in external/ is never passed through here; it keeps its own flags.
# See CODE_STYLE.md, "Warnings and compiler checks".
include_guard(GLOBAL)

# imlui_set_warnings(<target>)
#   Zero tolerance: every warning is an error. Third-party headers reach our targets as SYSTEM
#   includes (external/CMakeLists.txt), so these flags only judge our code. The flags are $<BUILD_INTERFACE:...>: an
#   installed imlui exports its modules' compile options, and an app that imports them rebuilds their BMIs with its
#   own compiler, which mustn't fail on a warning a newer clang adds.
function(imlui_set_warnings target)
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(flags
            -Wall -Wextra -Wpedantic -Werror
            -Wshadow
            -Wconversion -Wsign-conversion
            -Wold-style-cast
            -Wcast-align
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            -Wunused
            -Wextra-semi
            -Wmissing-declarations
        )
        target_compile_options(${target} PRIVATE "$<BUILD_INTERFACE:${flags}>")
    endif()
endfunction()

# imlui_set_hardening(<target>)
#   Debug only: trap on signed integer overflow. The MSVC STL's checked iterators are already on in
#   Debug (/MDd selects _ITERATOR_DEBUG_LEVEL=2), so nothing is added for them. Not for vendored Lua,
#   which relies on wrapping integer arithmetic. A module's BMI records the setting and clang refuses to import one
#   built differently, so the imlui library passes it on to whatever links it (src/CMakeLists.txt): an app's targets
#   that import imlui get it in Debug whether they call this or not.
function(imlui_set_hardening target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-ftrapv>)
    endif()
endfunction()
