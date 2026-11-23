// Module wrapper around the Lua 5.4 C API.
//
// Most of the Lua API is made of macros (lua_pop, lua_tostring, lua_pcall ...),
// which cannot cross a module boundary, so every call we use is re-exposed as
// a real (inline) function in namespace `lua`. Nothing else in the project
// includes Lua headers.
module;

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <string>
#include <string_view>

export module lua;

export namespace lua {

using State     = ::lua_State;
using CFunction = ::lua_CFunction;
using Integer   = ::lua_Integer;
using Number    = ::lua_Number;

// ---------------------------------------------------------------- constants
inline constexpr int OK            = LUA_OK;
inline constexpr int MULTRET       = LUA_MULTRET;
inline constexpr int REGISTRYINDEX = LUA_REGISTRYINDEX;
inline constexpr int NOREF         = LUA_NOREF;
inline constexpr int REFNIL        = LUA_REFNIL;

inline constexpr int TNONE          = LUA_TNONE;
inline constexpr int TNIL           = LUA_TNIL;
inline constexpr int TBOOLEAN       = LUA_TBOOLEAN;
inline constexpr int TLIGHTUSERDATA = LUA_TLIGHTUSERDATA;
inline constexpr int TNUMBER        = LUA_TNUMBER;
inline constexpr int TSTRING        = LUA_TSTRING;
inline constexpr int TTABLE         = LUA_TTABLE;
inline constexpr int TFUNCTION      = LUA_TFUNCTION;
inline constexpr int TUSERDATA      = LUA_TUSERDATA;

// ---------------------------------------------------------------- state
inline State* newstate() { return luaL_newstate(); }
inline void close(State* L) { lua_close(L); }
inline void openlibs(State* L) { luaL_openlibs(L); }

// ---------------------------------------------------------------- stack
inline int  gettop(State* L) { return lua_gettop(L); }
inline void settop(State* L, int idx) { lua_settop(L, idx); }
inline void pop(State* L, int n = 1) { lua_pop(L, n); }
inline void pushvalue(State* L, int idx) { lua_pushvalue(L, idx); }
inline int  absindex(State* L, int idx) { return lua_absindex(L, idx); }
inline void insert(State* L, int idx) { lua_insert(L, idx); }
inline void remove(State* L, int idx) { lua_remove(L, idx); }
inline bool checkstack(State* L, int n) { return lua_checkstack(L, n) != 0; }
inline int  upvalueindex(int i) { return lua_upvalueindex(i); }

// ---------------------------------------------------------------- types
inline int  type(State* L, int idx) { return lua_type(L, idx); }
inline const char* type_name(State* L, int idx) { return luaL_typename(L, idx); }
inline bool isnil(State* L, int idx) { return lua_isnil(L, idx); }
inline bool isnoneornil(State* L, int idx) { return lua_isnoneornil(L, idx); }
inline bool istable(State* L, int idx) { return lua_istable(L, idx); }
inline bool isfunction(State* L, int idx) { return lua_isfunction(L, idx); }
inline bool isboolean(State* L, int idx) { return lua_isboolean(L, idx); }
inline bool isnumber(State* L, int idx) { return lua_isnumber(L, idx) != 0; }
inline bool isinteger(State* L, int idx) { return lua_isinteger(L, idx) != 0; }
inline bool isstring(State* L, int idx) { return lua_isstring(L, idx) != 0; }

// ---------------------------------------------------------------- read
inline bool    toboolean(State* L, int idx) { return lua_toboolean(L, idx) != 0; }
inline Number  tonumber(State* L, int idx) { return lua_tonumberx(L, idx, nullptr); }
inline Integer tointeger(State* L, int idx) { return lua_tointegerx(L, idx, nullptr); }
inline const void* topointer(State* L, int idx) { return lua_topointer(L, idx); }
inline void*   touserdata(State* L, int idx) { return lua_touserdata(L, idx); }
inline std::string_view tostring(State* L, int idx) {
    std::size_t len = 0;
    const char* s = lua_tolstring(L, idx, &len);
    return s ? std::string_view{s, len} : std::string_view{};
}
/// Like tostring but converts any value (uses __tostring); pushes nothing.
inline std::string tostring_any(State* L, int idx) {
    std::size_t len = 0;
    const char* s = luaL_tolstring(L, idx, &len);
    std::string out{s, len};
    lua_pop(L, 1);
    return out;
}

// ---------------------------------------------------------------- push
inline void pushnil(State* L) { lua_pushnil(L); }
inline void pushboolean(State* L, bool b) { lua_pushboolean(L, b ? 1 : 0); }
inline void pushnumber(State* L, Number n) { lua_pushnumber(L, n); }
inline void pushinteger(State* L, Integer n) { lua_pushinteger(L, n); }
inline void pushstring(State* L, std::string_view s) { lua_pushlstring(L, s.data(), s.size()); }
inline void pushcfunction(State* L, CFunction f) { lua_pushcfunction(L, f); }
inline void pushcclosure(State* L, CFunction f, int n) { lua_pushcclosure(L, f, n); }
inline void pushlightuserdata(State* L, void* p) { lua_pushlightuserdata(L, p); }

// ---------------------------------------------------------------- tables
inline void newtable(State* L) { lua_newtable(L); }
inline void createtable(State* L, int narr, int nrec) { lua_createtable(L, narr, nrec); }
inline int  getfield(State* L, int idx, const char* k) { return lua_getfield(L, idx, k); }
inline void setfield(State* L, int idx, const char* k) { lua_setfield(L, idx, k); }
inline int  gettable(State* L, int idx) { return lua_gettable(L, idx); }
inline void settable(State* L, int idx) { lua_settable(L, idx); }
inline int  rawget(State* L, int idx) { return lua_rawget(L, idx); }
inline int  rawgeti(State* L, int idx, Integer n) { return lua_rawgeti(L, idx, n); }
inline void rawseti(State* L, int idx, Integer n) { lua_rawseti(L, idx, n); }
inline Integer rawlen(State* L, int idx) { return static_cast<Integer>(lua_rawlen(L, idx)); }
inline int  next(State* L, int idx) { return lua_next(L, idx); }
inline int  getglobal(State* L, const char* name) { return lua_getglobal(L, name); }
inline void setglobal(State* L, const char* name) { lua_setglobal(L, name); }
inline int  getmetatable(State* L, int idx) { return lua_getmetatable(L, idx); }
inline int  setmetatable(State* L, int idx) { return lua_setmetatable(L, idx); }

// ---------------------------------------------------------------- calls
inline int  pcall(State* L, int nargs, int nresults, int msgh) { return lua_pcall(L, nargs, nresults, msgh); }
inline void call(State* L, int nargs, int nresults) { lua_call(L, nargs, nresults); }
inline int  loadfile(State* L, const char* path) { return luaL_loadfile(L, path); }
inline int  loadbuffer(State* L, std::string_view code, const char* name) {
    return luaL_loadbuffer(L, code.data(), code.size(), name);
}
inline void traceback(State* L, State* L1, const char* msg, int level) { luaL_traceback(L, L1, msg, level); }
/// Raises a Lua error with `msg`. Never returns.
[[noreturn]] inline void error(State* L, std::string_view msg) {
    lua_pushlstring(L, msg.data(), msg.size());
    lua_error(L);
    // lua_error longjmps; this is unreachable.
    for (;;) {}
}

// ---------------------------------------------------------------- refs
inline int  ref(State* L, int t = LUA_REGISTRYINDEX) { return luaL_ref(L, t); }
inline void unref(State* L, int r, int t = LUA_REGISTRYINDEX) { luaL_unref(L, t, r); }

// ---------------------------------------------------------------- aux
inline void requiref(State* L, const char* name, CFunction open, bool global) {
    luaL_requiref(L, name, open, global ? 1 : 0);
}

/// Pops `n` values on scope exit, restoring the stack top recorded at entry.
class StackGuard {
public:
    explicit StackGuard(State* L) : L_{L}, top_{lua_gettop(L)} {}
    ~StackGuard() { lua_settop(L_, top_); }
    StackGuard(const StackGuard&) = delete;
    StackGuard& operator=(const StackGuard&) = delete;
private:
    State* L_;
    int top_;
};

} // namespace lua
