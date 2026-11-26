// Lua 5.4, compiled as C++ in a single translation unit (Lua's own onelua.c), so that raising an error throws a C++
// exception instead of longjmp-ing: the destructors of the C++ frames between a raise and the protected call that
// catches it run. The vendored sources are unchanged; only the error-handling hooks ldo.c leaves to the build
// (LUAI_THROW / LUAI_TRY, used when they're defined) are set here:
//   - Lua throws imlui_lua_error (a type the wrapper module can name: lua::Raised), not its internal lua_longjmp*.
//   - A std::bad_alloc thrown by C++ code under a protected call becomes a Lua memory error ("not enough memory").
//   - Any other foreign C++ exception becomes status -1, as in upstream Lua: not an error with a usable message.
//     The library's C functions turn their C++ exceptions into Lua errors themselves (luaui::catchCxx).
// It also defines the additions of imlui_lua_state.h, which need Lua's internals: luaL_newstate with another allocator
// (lauxlib's private panic and warning functions) and imlui_lua_reservehash (ltable.c).
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

#include "imlui_lua_state.h"

// lauxlib.c's luaL_newstate, with `f` for its l_alloc.
lua_State* imlui_lua_newstate(lua_Alloc f, void* ud) {
    lua_State* L = lua_newstate(f, ud);
    if (L) {
        lua_atpanic(L, &panic);
        lua_setwarnf(L, warnfoff, L); /* default is warnings off */
    }
    return L;
}

void imlui_lua_reservehash(lua_State* L, int idx, int n) {
    Table* t = hvalue(index2value(L, idx));
    unsigned int used = 0;
    if (!isdummy(t)) {
        for (Node *node = gnode(t, 0), *end = gnode(t, sizenode(t)); node < end; ++node)
            if (!isempty(gval(node))) ++used;
        if (used + static_cast<unsigned int>(n) <= sizenode(t)) return;
    }
    luaH_resize(L, t, luaH_realasize(t), used + static_cast<unsigned int>(n));
}
