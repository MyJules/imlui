// Lua 5.4, compiled as C++ in a single translation unit (Lua's own onelua.c), so that raising an error throws a C++
// exception instead of longjmp-ing: the destructors of the C++ frames between a raise and the protected call that
// catches it run. The vendored sources are unchanged; only the error-handling hooks ldo.c leaves to the build
// (LUAI_THROW / LUAI_TRY, used when they're defined) are set here:
//   - Lua throws imlui_lua_error (a type the wrapper module can name: lua::Raised), not its internal lua_longjmp*.
//   - A std::bad_alloc thrown by C++ code under a protected call becomes a Lua memory error ("not enough memory").
//   - Any other foreign C++ exception becomes status -1, as in upstream Lua: not an error with a usable message.
//     The library's C functions turn their C++ exceptions into Lua errors themselves (luaui::catchCxx).
#include <new>

#include "imlui_lua_error.h"

#define LUAI_THROW(L, c) throw imlui_lua_error{}
#define LUAI_TRY(L, c, a)                                                                                              \
    try {                                                                                                              \
        a                                                                                                              \
    } catch (const imlui_lua_error&) { /* (c)->status is already set */                                               \
    } catch (const std::bad_alloc&) {                                                                                  \
        if ((c)->status == 0) (c)->status = LUA_ERRMEM;                                                                \
    } catch (...) {                                                                                                    \
        if ((c)->status == 0) (c)->status = -1;                                                                        \
    }
#define luai_jmpbuf int /* unused */

#define MAKE_LIB
#include "onelua.c"
