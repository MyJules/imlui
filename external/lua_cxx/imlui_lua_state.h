// What imlui_lua.cpp adds to the Lua API, besides its error handling (imlui_lua_error.h). Included by the wrapper
// module (src/wrap/lua.cppm) only.
#pragma once

#include "lua.h"

/// luaL_newstate with the memory allocator `f` (called with `ud`) instead of lauxlib's: the same panic function and
/// warning function (warnings off until "@on"), which are private to lauxlib.c. NULL when the state can't be created.
lua_State* imlui_lua_newstate(lua_Alloc f, void* ud);

/// Makes room in the hash part of the table at `idx` for `n` more keys, in one resize: a table that gets several new
/// keys at once (an element table: `__type`, then `__native`) otherwise grows at each key that doesn't fit, which
/// reallocates and rehashes the whole hash part each time. Only a hint: does nothing when the keys fit already.
/// Raises a memory error like any table insertion.
void imlui_lua_reservehash(lua_State* L, int idx, int n);
