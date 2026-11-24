# Warning policy and debug hardening for imlui's own targets (imlui, imlui_app, imlui_tests).
# Vendored code in external/ is never passed through here; it keeps its own flags.
# See CODE_STYLE.md, "Warnings and compiler checks".

# imlui_set_warnings(<target>)
#   Zero tolerance: every warning is an error. Third-party headers reach our targets as SYSTEM
#   includes (external/CMakeLists.txt), so these flags only judge our code.
function(imlui_set_warnings target)
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
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
    endif()
endfunction()

# imlui_set_hardening(<target>)
#   Debug only: trap on signed integer overflow. The MSVC STL's checked iterators are already on in
#   Debug (/MDd selects _ITERATOR_DEBUG_LEVEL=2), so nothing is added for them. Not for vendored Lua,
#   which relies on wrapping integer arithmetic.
function(imlui_set_hardening target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-ftrapv>)
    endif()
endfunction()
