// Module wrapper around the Lua 5.4 C API. Its own library (imlui_lua), so the resource tools, which can't depend
// on imlui, evaluate manifests with it too.
//
// Most of the Lua API is made of macros (lua_pop, lua_tostring, lua_pcall ...),
// which cannot cross a module boundary, so every call we use is re-exposed as
// a real function in namespace `lua`. Nothing else in the project includes
// Lua headers.
//
// Lua is compiled as C++ (external/lua_cxx/imlui_lua.cpp): its API has C++ linkage (so no extern "C" here), and a
// Lua error is a C++ exception, lua::Raised, which unwinds the C++ frames between the raise and the protected call
// that catches it, running their destructors.
module;

#include "imlui_lua_error.h"
#include "imlui_lua_state.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

// Under AddressSanitizer, newstate() keeps Lua's plain allocator: the pool would hide Lua's memory from ASan (a
// block freed to a free list is still "valid" to it, a read past a small block lands in its neighbour).
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define IMLUI_LUA_PLAIN_ALLOC 1
#endif
#endif

export module lua;

import std;

export namespace lua {

using State = ::lua_State;
using CFunction = ::lua_CFunction;
using Alloc = ::lua_Alloc;
using Hook = ::lua_Hook;
using Debug = ::lua_Debug;
using Integer = ::lua_Integer;
using Number = ::lua_Number;

/// The C++ exception that carries a Lua error (lua::error, lua::raise, luaL_check*, a memory error ...) to the
/// protected call that catches it, which takes the error value from the Lua stack (the exception itself is empty).
/// C++ code may let it pass, or catch it to rethrow it (`catch (const lua::Raised&) { throw; }` before a
/// `catch (...)`), but must never swallow it: Lua expects it to reach its protected call.
using Raised = ::imlui_lua_error;

// ---------------------------------------------------------------- constants
inline constexpr int ok = LUA_OK;
inline constexpr int multRet = LUA_MULTRET;
inline constexpr int registryIndex = LUA_REGISTRYINDEX;
inline constexpr int noRef = LUA_NOREF;
inline constexpr int refNil = LUA_REFNIL;
inline constexpr int errMem = LUA_ERRMEM;
inline constexpr int maskCount = LUA_MASKCOUNT;

/// Type of a Lua value, as returned by type(), getfield(), rawgeti() ...
enum class Type : int { None = -1, Nil, Boolean, LightUserData, Number, String, Table, Function, UserData, Thread };

// ---------------------------------------------------------------- state
/// A new state (luaL_newstate's panic and warning functions), nullptr when out of memory. Its memory comes from a pool
/// of its own (small blocks from size-class free lists), which close() releases: the state is used by one thread at
/// a time, like any Lua state.
State* newstate();
/// A state whose memory comes from `allocator` (called with `ud`), e.g. to cap a sandbox's memory.
State* newstate(Alloc allocator, void* ud);
/// Closes `state` (lua_close) and releases the pool newstate() gave it.
void close(State* state);
void openlibs(State* state);
/// Opens only the base library (print, pairs, tostring, ...) into the globals; for sandboxes.
void openbase(State* state);
/// Sets the panic function (called on an error outside any protected call, before Lua aborts); returns the old one.
CFunction atpanic(State* state, CFunction panicf);
/// Sets the debug hook (lua_sethook), e.g. maskCount to run `hook` after every `count` instructions.
void sethook(State* state, Hook hook, int mask, int count);

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
bool isuserdata(State* state, int idx);

// ---------------------------------------------------------------- read
bool toboolean(State* state, int idx);
Number tonumber(State* state, int idx);
Integer tointeger(State* state, int idx);
const void* topointer(State* state, int idx);
void* touserdata(State* state, int idx);
std::string_view tostring(State* state, int idx);
/// Like tostring but converts any value (uses __tostring); pushes nothing.
std::string toStringAny(State* state, int idx);
/// luaL_tolstring: converts any value (runs __tostring, which may raise) and pushes the result; returns it.
std::string_view pushToString(State* state, int idx);

// ---------------------------------------------------------------- push
void pushnil(State* state);
void pushboolean(State* state, bool b);
void pushnumber(State* state, Number n);
void pushinteger(State* state, Integer n);
void pushstring(State* state, std::string_view s);
/// Pushes a NUL-terminated string through lua_pushstring, whose cache (by address) makes pushing the same string
/// constant again cheap: for keys pushed over and over (property names).
void pushcstring(State* state, const char* s);
void pushcfunction(State* state, CFunction f);
void pushcclosure(State* state, CFunction f, int n);
void pushlightuserdata(State* state, void* p);
/// Pushes a new full userdata of `size` bytes with `nuvalue` user values; returns its memory.
void* newuserdata(State* state, std::size_t size, int nuvalue = 0);

// ---------------------------------------------------------------- tables
void newtable(State* state);
void createtable(State* state, int narr, int nrec);
/// Makes room in the table at `idx` for `n` more non-integer keys, so adding them resizes it at most once (only a
/// hint: nothing when they fit already).
void reservehash(State* state, int idx, int n);
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
/// Pushes t[p] (raw) for the table at `idx`, keyed by the light userdata `p`.
Type rawgetp(State* state, int idx, const void* p);
/// t[p] = top (raw) for the table at `idx`; pops the value.
void rawsetp(State* state, int idx, const void* p);
/// t[k] = v (raw) with the key below the value on top; pops both.
void rawset(State* state, int idx);
bool rawequal(State* state, int idx1, int idx2);
/// Pushes the n-th user value of the userdata at `idx`.
Type getiuservalue(State* state, int idx, int n);
/// Pops a value and sets it as the n-th user value of the userdata at `idx`.
bool setiuservalue(State* state, int idx, int n);

// ---------------------------------------------------------------- calls
int pcall(State* state, int nargs, int nresults, int msgh);
void call(State* state, int nargs, int nresults);
int loadfile(State* state, const char* path);
int loadbuffer(State* state, std::string_view code, const char* name);
/// loadbuffer with a mode: "t" text only, "b" binary only, "bt" both.
int loadbufferx(State* state, std::string_view code, const char* name, const char* mode);
/// Pops a value and sets it as upvalue `n` of the function at `funcindex`; nullptr when there is no such upvalue.
const char* setupvalue(State* state, int funcindex, int n);
void traceback(State* state, State* thread, const char* msg, int level);
/// Raises a Lua error with `msg` (throws lua::Raised). Never returns.
[[noreturn]] void error(State* state, std::string_view msg);
/// Raises the value on top of the stack as a Lua error (throws lua::Raised). Never returns.
[[noreturn]] void raise(State* state);

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

/// The memory of a state made by newstate(): Lua allocates mostly small blocks (tables, strings, closures, userdata,
/// a table's hash part), and building a UI allocates tens of thousands at once, each one a trip through the C heap's
/// locks and bookkeeping. Blocks up to `smallMax` bytes come from size-class free lists instead, carved from large
/// chunks that live as long as the state (a freed block goes back to its list, for the next block of its class);
/// bigger ones go to the C heap. Not thread-safe: one per state, and a state runs on one thread at a time.
class Pool {
public:
    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() {
        while (m_chunks) {
            Chunk* next = m_chunks->next;
            std::free(m_chunks);
            m_chunks = next;
        }
    }

    /// The lua_Alloc: `ud` is the Pool. Follows its contract: nsize 0 frees (ptr may be null), a null ptr allocates
    /// (osize is then a type tag, not a size), else resizes the osize-byte block; a failure returns null and leaves
    /// the block as it was.
    static void* alloc(void* ud, void* ptr, std::size_t osize, std::size_t nsize) noexcept {
        auto* pool = static_cast<Pool*>(ud);
        if (nsize == 0) {
            if (ptr) pool->release(ptr, osize);
            return nullptr;
        }
        if (!ptr) return pool->allocate(nsize);
        if (osize > smallMax && nsize > smallMax) return std::realloc(ptr, nsize);
        if (osize <= smallMax && nsize <= smallMax && sizeClass(osize) == sizeClass(nsize)) return ptr;
        void* moved = pool->allocate(nsize);
        if (!moved) return nullptr;
        std::memcpy(moved, ptr, std::min(osize, nsize));
        pool->release(ptr, osize);
        return moved;
    }

private:
    static constexpr std::size_t granule = 16; ///< block sizes are multiples of it (and so is their alignment)
    static constexpr std::size_t smallMax = 512;
    static constexpr std::size_t firstChunk = std::size_t{16} << 10; ///< chunks double up to lastChunk
    static constexpr std::size_t lastChunk = std::size_t{1} << 20;

    struct FreeBlock {
        FreeBlock* next;
    };
    /// A chunk's header; its blocks follow it.
    struct alignas(granule) Chunk {
        Chunk* next;
    };

    /// The free list of blocks of `size` bytes (1..smallMax): size classes of `granule` bytes.
    static std::size_t sizeClass(std::size_t size) { return (size - 1) / granule; }

    void* allocate(std::size_t size) {
        if (size > smallMax) return std::malloc(size);
        const std::size_t c = sizeClass(size);
        if (FreeBlock* b = m_free[c]) {
            m_free[c] = b->next;
            return b;
        }
        const std::size_t bytes = (c + 1) * granule;
        if (static_cast<std::size_t>(m_end - m_cursor) < bytes && !grow()) return nullptr;
        void* b = m_cursor;
        m_cursor += bytes;
        return b;
    }

    void release(void* ptr, std::size_t size) {
        if (size > smallMax) {
            std::free(ptr);
            return;
        }
        push(ptr, sizeClass(size));
    }

    void push(void* ptr, std::size_t c) {
        auto* b = static_cast<FreeBlock*>(ptr);
        b->next = m_free[c];
        m_free[c] = b;
    }

    /// A new chunk to carve from; what was left of the current one goes to the free list of its size.
    bool grow() {
        auto* chunk = static_cast<Chunk*>(std::malloc(m_nextChunk));
        if (!chunk) return false;
        if (const auto left = static_cast<std::size_t>(m_end - m_cursor); left >= granule)
            push(m_cursor, sizeClass(left - left % granule));
        chunk->next = m_chunks;
        m_chunks = chunk;
        m_cursor = reinterpret_cast<std::byte*>(chunk + 1);
        m_end = reinterpret_cast<std::byte*>(chunk) + m_nextChunk;
        m_nextChunk = std::min(m_nextChunk * 2, lastChunk);
        return true;
    }

    std::array<FreeBlock*, smallMax / granule> m_free{};
    std::byte* m_cursor = nullptr; ///< the unused rest of the newest chunk
    std::byte* m_end = nullptr;
    Chunk* m_chunks = nullptr;
    std::size_t m_nextChunk = firstChunk;
};
} // namespace

// ---------------------------------------------------------------- state
#ifdef IMLUI_LUA_PLAIN_ALLOC
State* newstate() { return luaL_newstate(); }
#else
State* newstate() {
    auto* pool = new (std::nothrow) Pool;
    if (!pool) return nullptr;
    State* state = imlui_lua_newstate(&Pool::alloc, pool);
    if (!state) delete pool;
    return state;
}
#endif
State* newstate(Alloc allocator, void* ud) { return lua_newstate(allocator, ud); }
void close(State* state) {
    void* ud = nullptr;
    const bool pooled = lua_getallocf(state, &ud) == &Pool::alloc;
    lua_close(state);
    if (pooled) delete static_cast<Pool*>(ud); // after lua_close, which frees its blocks through it
}
void openlibs(State* state) { luaL_openlibs(state); }
CFunction atpanic(State* state, CFunction panicf) { return lua_atpanic(state, panicf); }
void sethook(State* state, Hook hook, int mask, int count) { lua_sethook(state, hook, mask, count); }
void openbase(State* state) {
    luaL_requiref(state, LUA_GNAME, luaopen_base, 1);
    lua_pop(state, 1);
}

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
bool isuserdata(State* state, int idx) { return lua_isuserdata(state, idx) != 0; }

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
std::string_view pushToString(State* state, int idx) {
    std::size_t len = 0;
    const char* s = luaL_tolstring(state, idx, &len);
    return {s, len};
}

// ---------------------------------------------------------------- push
void pushnil(State* state) { lua_pushnil(state); }
void pushboolean(State* state, bool b) { lua_pushboolean(state, b ? 1 : 0); }
void pushnumber(State* state, Number n) { lua_pushnumber(state, n); }
void pushinteger(State* state, Integer n) { lua_pushinteger(state, n); }
void pushstring(State* state, std::string_view s) { lua_pushlstring(state, s.data(), s.size()); }
void pushcstring(State* state, const char* s) { lua_pushstring(state, s); }
void pushcfunction(State* state, CFunction f) { lua_pushcfunction(state, f); }
void pushcclosure(State* state, CFunction f, int n) { lua_pushcclosure(state, f, n); }
void pushlightuserdata(State* state, void* p) { lua_pushlightuserdata(state, p); }
void* newuserdata(State* state, std::size_t size, int nuvalue) { return lua_newuserdatauv(state, size, nuvalue); }

// ---------------------------------------------------------------- tables
void newtable(State* state) { lua_newtable(state); }
void createtable(State* state, int narr, int nrec) { lua_createtable(state, narr, nrec); }
void reservehash(State* state, int idx, int n) { imlui_lua_reservehash(state, idx, n); }
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
Type rawgetp(State* state, int idx, const void* p) { return toType(lua_rawgetp(state, idx, p)); }
void rawsetp(State* state, int idx, const void* p) { lua_rawsetp(state, idx, p); }
void rawset(State* state, int idx) { lua_rawset(state, idx); }
bool rawequal(State* state, int idx1, int idx2) { return lua_rawequal(state, idx1, idx2) != 0; }
Type getiuservalue(State* state, int idx, int n) { return toType(lua_getiuservalue(state, idx, n)); }
bool setiuservalue(State* state, int idx, int n) { return lua_setiuservalue(state, idx, n) != 0; }

// ---------------------------------------------------------------- calls
int pcall(State* state, int nargs, int nresults, int msgh) { return lua_pcall(state, nargs, nresults, msgh); }
void call(State* state, int nargs, int nresults) { lua_call(state, nargs, nresults); }
int loadfile(State* state, const char* path) { return luaL_loadfile(state, path); }
int loadbuffer(State* state, std::string_view code, const char* name) {
    return luaL_loadbuffer(state, code.data(), code.size(), name);
}
int loadbufferx(State* state, std::string_view code, const char* name, const char* mode) {
    return luaL_loadbufferx(state, code.data(), code.size(), name, mode);
}
const char* setupvalue(State* state, int funcindex, int n) { return lua_setupvalue(state, funcindex, n); }
void traceback(State* state, State* thread, const char* msg, int level) { luaL_traceback(state, thread, msg, level); }
void error(State* state, std::string_view msg) {
    lua_pushlstring(state, msg.data(), msg.size());
    lua_error(state);
    for (;;) {} // lua_error throws: never reached
}
void raise(State* state) {
    lua_error(state);
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
