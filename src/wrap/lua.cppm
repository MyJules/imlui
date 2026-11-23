// Module wrapper around the Lua 5.4 C API.
//
// Most of the Lua API is made of macros (lua_pop, lua_tostring, lua_pcall ...),
// which cannot cross a module boundary, so every call we use is re-exposed as
// a real function in namespace `lua`. Nothing else in the project includes
// Lua headers.
module;

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <cstddef>
#include <string>
#include <string_view>

export module lua;

export namespace lua {

using State = ::lua_State;
using CFunction = ::lua_CFunction;
using Integer = ::lua_Integer;
using Number = ::lua_Number;

// ---------------------------------------------------------------- constants
inline constexpr int ok = LUA_OK;
inline constexpr int multRet = LUA_MULTRET;
inline constexpr int registryIndex = LUA_REGISTRYINDEX;
inline constexpr int noRef = LUA_NOREF;
inline constexpr int refNil = LUA_REFNIL;

/// Type of a Lua value, as returned by type(), getfield(), rawgeti() ...
enum class Type : int { None = -1, Nil, Boolean, LightUserData, Number, String, Table, Function, UserData, Thread };

// ---------------------------------------------------------------- state
State* newstate();
void close(State* state);
void openlibs(State* state);

// ---------------------------------------------------------------- stack
int gettop(State* state);
void settop(State* state, int idx);
void pop(State* state, int n = 1);
void pushvalue(State* state, int idx);
int absindex(State* state, int idx);
void insert(State* state, int idx);
void remove(State* state, int idx);
bool checkstack(State* state, int n);
int upvalueindex(int i);

// ---------------------------------------------------------------- types
Type type(State* state, int idx);
const char* typeName(State* state, int idx);
bool isnil(State* state, int idx);
bool isnoneornil(State* state, int idx);
bool istable(State* state, int idx);
bool isfunction(State* state, int idx);
bool isboolean(State* state, int idx);
bool isnumber(State* state, int idx);
bool isinteger(State* state, int idx);
bool isstring(State* state, int idx);

// ---------------------------------------------------------------- read
bool toboolean(State* state, int idx);
Number tonumber(State* state, int idx);
Integer tointeger(State* state, int idx);
const void* topointer(State* state, int idx);
void* touserdata(State* state, int idx);
std::string_view tostring(State* state, int idx);
/// Like tostring but converts any value (uses __tostring); pushes nothing.
std::string toStringAny(State* state, int idx);

// ---------------------------------------------------------------- push
void pushnil(State* state);
void pushboolean(State* state, bool b);
void pushnumber(State* state, Number n);
void pushinteger(State* state, Integer n);
void pushstring(State* state, std::string_view s);
void pushcfunction(State* state, CFunction f);
void pushcclosure(State* state, CFunction f, int n);
void pushlightuserdata(State* state, void* p);

// ---------------------------------------------------------------- tables
void newtable(State* state);
void createtable(State* state, int narr, int nrec);
Type getfield(State* state, int idx, const char* k);
void setfield(State* state, int idx, const char* k);
Type gettable(State* state, int idx);
void settable(State* state, int idx);
Type rawget(State* state, int idx);
Type rawgeti(State* state, int idx, Integer n);
void rawseti(State* state, int idx, Integer n);
Integer rawlen(State* state, int idx);
int next(State* state, int idx);
Type getglobal(State* state, const char* name);
void setglobal(State* state, const char* name);
int getmetatable(State* state, int idx);
int setmetatable(State* state, int idx);

// ---------------------------------------------------------------- calls
int pcall(State* state, int nargs, int nresults, int msgh);
void call(State* state, int nargs, int nresults);
int loadfile(State* state, const char* path);
int loadbuffer(State* state, std::string_view code, const char* name);
void traceback(State* state, State* thread, const char* msg, int level);
/// Raises a Lua error with `msg`. Never returns.
[[noreturn]] void error(State* state, std::string_view msg);

// ---------------------------------------------------------------- refs
int ref(State* state, int t = registryIndex);
void unref(State* state, int r, int t = registryIndex);

// ---------------------------------------------------------------- aux
void requiref(State* state, const char* name, CFunction open, bool global);

} // namespace lua

module :private;

namespace lua {

static_assert(static_cast<int>(Type::None) == LUA_TNONE);
static_assert(static_cast<int>(Type::Nil) == LUA_TNIL);
static_assert(static_cast<int>(Type::Boolean) == LUA_TBOOLEAN);
static_assert(static_cast<int>(Type::LightUserData) == LUA_TLIGHTUSERDATA);
static_assert(static_cast<int>(Type::Number) == LUA_TNUMBER);
static_assert(static_cast<int>(Type::String) == LUA_TSTRING);
static_assert(static_cast<int>(Type::Table) == LUA_TTABLE);
static_assert(static_cast<int>(Type::Function) == LUA_TFUNCTION);
static_assert(static_cast<int>(Type::UserData) == LUA_TUSERDATA);
static_assert(static_cast<int>(Type::Thread) == LUA_TTHREAD);

namespace {
Type toType(int t) { return static_cast<Type>(t); }
} // namespace

// ---------------------------------------------------------------- state
State* newstate() { return luaL_newstate(); }
void close(State* state) { lua_close(state); }
void openlibs(State* state) { luaL_openlibs(state); }

// ---------------------------------------------------------------- stack
int gettop(State* state) { return lua_gettop(state); }
void settop(State* state, int idx) { lua_settop(state, idx); }
void pop(State* state, int n) { lua_pop(state, n); }
void pushvalue(State* state, int idx) { lua_pushvalue(state, idx); }
int absindex(State* state, int idx) { return lua_absindex(state, idx); }
void insert(State* state, int idx) { lua_insert(state, idx); }
void remove(State* state, int idx) { lua_remove(state, idx); }
bool checkstack(State* state, int n) { return lua_checkstack(state, n) != 0; }
int upvalueindex(int i) { return lua_upvalueindex(i); }

// ---------------------------------------------------------------- types
Type type(State* state, int idx) { return toType(lua_type(state, idx)); }
const char* typeName(State* state, int idx) { return luaL_typename(state, idx); }
bool isnil(State* state, int idx) { return lua_isnil(state, idx); }
bool isnoneornil(State* state, int idx) { return lua_isnoneornil(state, idx); }
bool istable(State* state, int idx) { return lua_istable(state, idx); }
bool isfunction(State* state, int idx) { return lua_isfunction(state, idx); }
bool isboolean(State* state, int idx) { return lua_isboolean(state, idx); }
bool isnumber(State* state, int idx) { return lua_isnumber(state, idx) != 0; }
bool isinteger(State* state, int idx) { return lua_isinteger(state, idx) != 0; }
bool isstring(State* state, int idx) { return lua_isstring(state, idx) != 0; }

// ---------------------------------------------------------------- read
bool toboolean(State* state, int idx) { return lua_toboolean(state, idx) != 0; }
Number tonumber(State* state, int idx) { return lua_tonumberx(state, idx, nullptr); }
Integer tointeger(State* state, int idx) { return lua_tointegerx(state, idx, nullptr); }
const void* topointer(State* state, int idx) { return lua_topointer(state, idx); }
void* touserdata(State* state, int idx) { return lua_touserdata(state, idx); }
std::string_view tostring(State* state, int idx) {
    std::size_t len = 0;
    const char* s = lua_tolstring(state, idx, &len);
    return s ? std::string_view{s, len} : std::string_view{};
}
std::string toStringAny(State* state, int idx) {
    std::size_t len = 0;
    const char* s = luaL_tolstring(state, idx, &len);
    std::string out{s, len};
    lua_pop(state, 1);
    return out;
}

// ---------------------------------------------------------------- push
void pushnil(State* state) { lua_pushnil(state); }
void pushboolean(State* state, bool b) { lua_pushboolean(state, b ? 1 : 0); }
void pushnumber(State* state, Number n) { lua_pushnumber(state, n); }
void pushinteger(State* state, Integer n) { lua_pushinteger(state, n); }
void pushstring(State* state, std::string_view s) { lua_pushlstring(state, s.data(), s.size()); }
void pushcfunction(State* state, CFunction f) { lua_pushcfunction(state, f); }
void pushcclosure(State* state, CFunction f, int n) { lua_pushcclosure(state, f, n); }
void pushlightuserdata(State* state, void* p) { lua_pushlightuserdata(state, p); }

// ---------------------------------------------------------------- tables
void newtable(State* state) { lua_newtable(state); }
void createtable(State* state, int narr, int nrec) { lua_createtable(state, narr, nrec); }
Type getfield(State* state, int idx, const char* k) { return toType(lua_getfield(state, idx, k)); }
void setfield(State* state, int idx, const char* k) { lua_setfield(state, idx, k); }
Type gettable(State* state, int idx) { return toType(lua_gettable(state, idx)); }
void settable(State* state, int idx) { lua_settable(state, idx); }
Type rawget(State* state, int idx) { return toType(lua_rawget(state, idx)); }
Type rawgeti(State* state, int idx, Integer n) { return toType(lua_rawgeti(state, idx, n)); }
void rawseti(State* state, int idx, Integer n) { lua_rawseti(state, idx, n); }
Integer rawlen(State* state, int idx) { return static_cast<Integer>(lua_rawlen(state, idx)); }
int next(State* state, int idx) { return lua_next(state, idx); }
Type getglobal(State* state, const char* name) { return toType(lua_getglobal(state, name)); }
void setglobal(State* state, const char* name) { lua_setglobal(state, name); }
int getmetatable(State* state, int idx) { return lua_getmetatable(state, idx); }
int setmetatable(State* state, int idx) { return lua_setmetatable(state, idx); }

// ---------------------------------------------------------------- calls
int pcall(State* state, int nargs, int nresults, int msgh) { return lua_pcall(state, nargs, nresults, msgh); }
void call(State* state, int nargs, int nresults) { lua_call(state, nargs, nresults); }
int loadfile(State* state, const char* path) { return luaL_loadfile(state, path); }
int loadbuffer(State* state, std::string_view code, const char* name) {
    return luaL_loadbuffer(state, code.data(), code.size(), name);
}
void traceback(State* state, State* thread, const char* msg, int level) { luaL_traceback(state, thread, msg, level); }
void error(State* state, std::string_view msg) {
    lua_pushlstring(state, msg.data(), msg.size());
    lua_error(state);
    // lua_error longjmps; this is unreachable.
    for (;;) {}
}

// ---------------------------------------------------------------- refs
int ref(State* state, int t) { return luaL_ref(state, t); }
void unref(State* state, int r, int t) { luaL_unref(state, t, r); }

// ---------------------------------------------------------------- aux
void requiref(State* state, const char* name, CFunction open, bool global) {
    luaL_requiref(state, name, open, global ? 1 : 0);
}

} // namespace lua
