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

/// lua_rawget of `t[k]` for the table at `idx` and the string key `k`, in one call: pushes the value (nil when absent)
/// and returns its type. The key is looked up as lua_getfield does (Lua's string cache, checked against the bytes, so
/// `k` may be any NUL-terminated string), without being pushed. No metamethods; the value at `idx` must be a table.
int imlui_lua_rawgetfield(lua_State* L, int idx, const char* k);

/// An identity of the string at `idx` for change detection: its address, length and Lua's hash of its bytes (computed
/// once per long string, kept with it). Equal strings that are one object (every short string) give the same value;
/// the same address reused by a different string after a collection gives another one unless its hash and length are
/// equal too. The value at `idx` must be a string.
unsigned long long imlui_lua_strid(lua_State* L, int idx);

/// lua_rawget of `t[k]` for the table at `idx`, the key a string made once per state: kept in slot `slot` of the
/// state's key cache (made on its first use, with `k`), so the read finds it by index, with no lookup of `k`. A slot
/// must always be given the same `k` (lua::Key). Pushes the value, returns its type. The cache lives until
/// imlui_lua_freekeys().
int imlui_lua_rawgetkey(lua_State* L, int idx, int slot, const char* k);

/// The key cache of `L`'s state (null when none was made). Read it before lua_close, which may still use it (a
/// finalizer that reads fields), and free it with imlui_lua_freekeys() after.
void* imlui_lua_keys(lua_State* L);
void imlui_lua_freekeys(void* keys);
