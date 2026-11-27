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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h> // VirtualAlloc, VirtualFree: the pool's chunks
#endif

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
/// a time, like any Lua state. trimMemory() gives back what a peak left unused.
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
/// The allocator of `state` and its `ud` (lua_getallocf).
Alloc getallocf(State* state, void** ud);

/// What newstate()'s pool holds (all zero for a state made with another allocator).
struct MemoryStats {
    std::size_t reserved = 0; ///< bytes the pool took from the OS for its small blocks
    std::size_t free = 0;     ///< of those, the bytes no live block uses
    std::size_t trims = 0;    ///< times trimMemory() walked the pool
};
/// When trimMemory() walks the pool.
struct TrimOptions {
    /// Trims when the free bytes are at least this many, more than the live ones, and this many more than their
    /// lowest since the last trim (a pool kept fragmented by a few live blocks isn't walked again until it grows).
    std::size_t minFree = std::size_t{8} << 20;
    /// And at most this often.
    std::chrono::steady_clock::duration minInterval = std::chrono::seconds{1};
    /// Trims now, whatever the free bytes and the last trim.
    bool force = false;
};
/// Gives the OS back the pool's chunks that hold no live block (after a collection freed a big tree), when
/// `options` says it's worth it; the check is cheap, so call it often, but never from inside a Lua call (an
/// allocation). Returns the bytes given back (0 without a pool: a state with another allocator, or under ASan).
std::size_t trimMemory(State* state, const TrimOptions& options = {});
[[nodiscard]] MemoryStats memoryStats(State* state);
/// The main thread of the state `state` belongs to (`state` itself unless it is a coroutine): it lives as long as the
/// state, unlike a coroutine.
State* mainthread(State* state);

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
/// An identity of the string (Type::String, not a number) at `idx` for change detection, cheaper than hashing its
/// bytes: its address, length and Lua's own hash of it (see imlui_lua_strid).
std::uint64_t strid(State* state, int idx);
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
/// rawget of `t[k]` for the table at `idx` and the key `k`, in one call (no key pushed): pushes the value, returns its
/// type. For the hot reads of known keys (an element's props); `k` may be any NUL-terminated string.
Type rawgetfield(State* state, int idx, const char* k);

/// A field name read over and over (an element's props, imlui's own markers): its Lua string is made once per state
/// and kept in the state's key cache under the Key's slot, so rawget(state, idx, key) reads the field with no lookup
/// of the name at all. A Key's name never changes; make one per name and keep it (a static, a member of what lives
/// as long as the names it reads), not one per read: every Key takes a slot for the life of the process.
class Key {
public:
    explicit Key(std::string name) : m_name{std::move(name)}, m_slot{nextSlot()} {}
    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] int slot() const { return m_slot; }

private:
    static int nextSlot();
    std::string m_name;
    int m_slot;
};
/// rawget of `t[key]` for the table at `idx`: pushes the value, returns its type (rawgetfield with a Key).
Type rawget(State* state, int idx, const Key& key);
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

/// The chunk of a closed state's one-chunk slab (its first), kept for the next state: creating and closing small
/// states (sandboxes, the tests) then costs no OS call and no fresh pages. Any thread may close or create a state.
std::atomic<std::byte*> spareChunk{nullptr};

/// The memory of a state made by newstate(): Lua allocates mostly small blocks (tables, strings, closures, userdata,
/// a table's hash part), and building a UI allocates tens of thousands at once, each one a trip through the C heap's
/// locks and bookkeeping. Blocks up to `smallMax` bytes come from size-class free lists instead (a freed block goes
/// back to its list, for the next block of its class); bigger ones go to the C heap. The lists are carved from chunks
/// of `chunkSize` bytes aligned to their size, so a block's chunk is its address with the low bits cleared, and each
/// chunk counts its live blocks: trim() gives the chunks a peak left empty back to the OS. Not thread-safe: one per
/// state, and a state runs on one thread at a time.
///
/// Chunks come from the OS in slabs of 1 to `slabChunks` chunks (64 KiB to 1 MiB, doubling as the pool grows), so a
/// small state takes 64 KiB (the last closed state's, when there is one: spareChunk) and a big one makes one OS call
/// per MiB. trim() decommits each empty chunk (the OS takes back its memory but the slab keeps the address range,
/// where a later chunk is committed again) and releases a slab once none of its chunks is committed.
class Pool {
public:
    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() {
        while (m_slabs) {
            Slab* next = m_slabs->next;
            releaseSlab(m_slabs);
            m_slabs = next;
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

    /// trimMemory(): trims when `options` says it's worth it (see TrimOptions). Cheap unless it trims: a few
    /// comparisons, and the clock only once the bytes say yes.
    std::size_t trimIfWorth(const TrimOptions& options) {
        const std::size_t free = freeBytes();
        if (!options.force) {
            m_freeLow = std::min(m_freeLow, free);
            if (free < options.minFree || free <= m_live || free - m_freeLow < options.minFree) return 0;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!options.force && m_trims > 0 && now - m_lastTrim < options.minInterval) return 0;
        const std::size_t released = trim();
        ++m_trims;
        m_lastTrim = now;
        m_freeLow = freeBytes();
        return released;
    }

    /// Gives back every chunk without a live block but the one being carved: drops their blocks from the free lists
    /// (one pass over them), then decommits them. Returns the bytes given back.
    std::size_t trim() {
        std::size_t empty = 0;
        for (Chunk* chunk = m_chunks; chunk; chunk = chunk->next) {
            chunk->empty = chunk->live == 0 && chunk != m_current;
            empty += chunk->empty ? 1 : 0;
        }
        if (empty == 0) return 0;
        for (FreeBlock*& head : m_free) {
            FreeBlock** link = &head;
            while (FreeBlock* b = *link) {
                if (chunkOf(b)->empty)
                    *link = b->next;
                else
                    link = &b->next;
            }
        }
        for (Chunk** link = &m_chunks; Chunk* chunk = *link;) {
            if (!chunk->empty) {
                link = &chunk->next;
                continue;
            }
            *link = chunk->next;
            Slab* slab = chunk->slab;
            slab->committed &= ~chunkBit(slab, chunk);
            decommit(chunk);
            ++m_holes;
        }
        // Slabs left without a committed chunk go back whole (their holes with them).
        for (Slab** link = &m_slabs; Slab* slab = *link;) {
            if (slab->committed != 0) {
                link = &slab->next;
                continue;
            }
            *link = slab->next;
            m_holes -= slab->chunks;
            releaseSlab(slab);
        }
        const std::size_t released = empty * chunkSize;
        m_reserved -= released;
        return released;
    }

    [[nodiscard]] MemoryStats stats() const { return {.reserved = m_reserved, .free = freeBytes(), .trims = m_trims}; }

private:
    static constexpr std::size_t granule = 16; ///< block sizes are multiples of it (and so is their alignment)
    static constexpr std::size_t smallMax = 512;
    /// The size and alignment of a chunk: Windows' allocation granularity, so every slab VirtualAlloc gives starts
    /// on a chunk.
    static constexpr std::size_t chunkSize = std::size_t{64} << 10;
    static constexpr std::size_t slabChunks = 16; ///< the most chunks in a slab (1 MiB); fits Slab::committed

    struct FreeBlock {
        FreeBlock* next;
    };
    struct Chunk;
    /// A range of chunks taken from the OS at once (on the C heap: its chunks may be decommitted).
    struct Slab {
        Slab* next;
        std::byte* base;
        std::uint32_t chunks;    ///< chunks in the range
        std::uint32_t carved;    ///< chunks handed out so far (the others are committed and unused)
        std::uint32_t committed; ///< a bit per chunk: committed (handed out or not yet), not decommitted by trim()
    };
    /// A chunk's header, at its start; its blocks follow it.
    struct alignas(granule) Chunk {
        Chunk* next;
        Slab* slab;
        std::uint32_t live; ///< blocks handed out and not released
        bool empty;         ///< trim(): to be given back
    };
    static_assert(sizeof(Chunk) == 2 * granule);

    /// The free list of blocks of `size` bytes (1..smallMax): size classes of `granule` bytes.
    static std::size_t sizeClass(std::size_t size) { return (size - 1) / granule; }
    static Chunk* chunkOf(void* block) {
        return reinterpret_cast<Chunk*>(reinterpret_cast<std::uintptr_t>(block) & ~(chunkSize - 1));
    }
    static std::uint32_t chunkBit(const Slab* slab, const void* chunk) {
        const auto offset = static_cast<std::size_t>(static_cast<const std::byte*>(chunk) - slab->base);
        return std::uint32_t{1} << (offset / chunkSize);
    }
    [[nodiscard]] std::size_t freeBytes() const { return m_reserved - m_live; }

    void* allocate(std::size_t size) {
        if (size > smallMax) return std::malloc(size);
        const std::size_t c = sizeClass(size);
        const std::size_t bytes = (c + 1) * granule;
        if (FreeBlock* b = m_free[c]) {
            m_free[c] = b->next;
            ++chunkOf(b)->live;
            m_live += bytes;
            return b;
        }
        if (static_cast<std::size_t>(m_end - m_cursor) < bytes && !grow()) return nullptr;
        void* b = m_cursor;
        m_cursor += bytes;
        ++m_current->live;
        m_live += bytes;
        return b;
    }

    void release(void* ptr, std::size_t size) {
        if (size > smallMax) {
            std::free(ptr);
            return;
        }
        const std::size_t c = sizeClass(size);
        --chunkOf(ptr)->live;
        m_live -= (c + 1) * granule;
        push(ptr, c);
    }

    void push(void* ptr, std::size_t c) {
        auto* b = static_cast<FreeBlock*>(ptr);
        b->next = m_free[c];
        m_free[c] = b;
    }

    /// A new chunk to carve from; what was left of the current one goes to the free list of its size.
    bool grow() {
        void* memory = nullptr;
        Slab* slab = m_slabs;
        if (slab && slab->carved < slab->chunks) { // the newest slab's next chunk
            memory = slab->base + std::size_t{slab->carved++} * chunkSize;
        } else if (m_holes > 0) { // a chunk trim() gave back, committed again
            for (slab = m_slabs; slab; slab = slab->next) {
                const std::uint32_t all = (std::uint32_t{1} << slab->chunks) - 1;
                if (slab->committed == all) continue;
                const auto i = static_cast<std::size_t>(std::countr_one(slab->committed));
                memory = commit(slab->base + i * chunkSize);
                if (!memory) return false;
                slab->committed |= std::uint32_t{1} << i;
                --m_holes;
                m_reserved += chunkSize;
                break;
            }
            if (!memory) return false; // never: m_holes counts the slabs' holes
        } else {
            slab = newSlab(m_nextSlab);
            if (!slab) return false;
            m_nextSlab = std::min(m_nextSlab * 2, slabChunks);
            memory = slab->base;
        }
        if (const auto left = static_cast<std::size_t>(m_end - m_cursor); left >= granule)
            push(m_cursor, sizeClass(left - left % granule));
        auto* chunk = static_cast<Chunk*>(memory);
        chunk->next = m_chunks;
        chunk->slab = slab;
        chunk->live = 0;
        chunk->empty = false;
        m_chunks = chunk;
        m_current = chunk;
        m_cursor = reinterpret_cast<std::byte*>(chunk + 1);
        m_end = reinterpret_cast<std::byte*>(chunk) + chunkSize;
        return true;
    }

    /// A slab of `chunks` committed chunks, put first in m_slabs, its first chunk carved; nullptr when out of memory.
    Slab* newSlab(std::size_t chunks) {
        auto* slab = static_cast<Slab*>(std::malloc(sizeof(Slab)));
        if (!slab) return nullptr;
        const std::size_t bytes = chunks * chunkSize;
        std::byte* base = chunks == 1 ? spareChunk.exchange(nullptr, std::memory_order_acquire) : nullptr;
        if (!base) base = osAllocate(bytes);
        if (!base) {
            std::free(slab);
            return nullptr;
        }
        *slab = {.next = m_slabs,
                 .base = base,
                 .chunks = static_cast<std::uint32_t>(chunks),
                 .carved = 1,
                 .committed = (std::uint32_t{1} << chunks) - 1};
        m_slabs = slab;
        m_reserved += bytes;
        return slab;
    }

    static void releaseSlab(Slab* slab) {
        std::byte* expected = nullptr;
        const bool kept = slab->chunks == 1 && slab->committed == 1 &&
                          spareChunk.compare_exchange_strong(expected, slab->base, std::memory_order_release);
        if (!kept) osRelease(slab->base);
        std::free(slab);
    }

    /// `bytes` (a multiple of chunkSize) committed, aligned to chunkSize; nullptr when out of memory.
    static std::byte* osAllocate(std::size_t bytes) {
#if defined(_WIN32)
        // VirtualAlloc's ranges start on its allocation granularity, 64 KiB everywhere Windows runs.
        auto* base = static_cast<std::byte*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (base && reinterpret_cast<std::uintptr_t>(base) % chunkSize != 0) {
            VirtualFree(base, 0, MEM_RELEASE);
            return nullptr;
        }
        return base;
#else
        return static_cast<std::byte*>(std::aligned_alloc(chunkSize, bytes));
#endif
    }
    static void osRelease(std::byte* base) {
#if defined(_WIN32)
        VirtualFree(base, 0, MEM_RELEASE);
#else
        std::free(base);
#endif
    }

    /// The OS takes the chunk's memory back; its address range stays the slab's. (Elsewhere a decommitted chunk
    /// stays as it is, until its whole slab goes.)
    static void decommit(void* chunk) {
#if defined(_WIN32)
        VirtualFree(chunk, chunkSize, MEM_DECOMMIT);
#else
        static_cast<void>(chunk);
#endif
    }
    static void* commit(void* chunk) {
#if defined(_WIN32)
        return VirtualAlloc(chunk, chunkSize, MEM_COMMIT, PAGE_READWRITE);
#else
        return chunk;
#endif
    }

    std::array<FreeBlock*, smallMax / granule> m_free{};
    std::byte* m_cursor = nullptr; ///< the unused rest of m_current
    std::byte* m_end = nullptr;
    Chunk* m_current = nullptr; ///< the chunk being carved
    Chunk* m_chunks = nullptr;  ///< every chunk handed out and not given back
    Slab* m_slabs = nullptr;    ///< newest first
    std::size_t m_nextSlab = 1; ///< chunks in the next slab
    std::size_t m_holes = 0;    ///< chunks trim() decommitted in the slabs still held
    std::size_t m_reserved = 0; ///< bytes committed in the slabs
    std::size_t m_live = 0;     ///< bytes in live blocks (of their size class)
    std::size_t m_freeLow = 0;  ///< the lowest free bytes trimIfWorth() saw since the last trim
    std::size_t m_trims = 0;
    std::chrono::steady_clock::time_point m_lastTrim;
};

Pool* poolOf(State* state) {
    void* ud = nullptr;
    return lua_getallocf(state, &ud) == &Pool::alloc ? static_cast<Pool*>(ud) : nullptr;
}

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
    Pool* pool = poolOf(state);
    void* keys = imlui_lua_keys(state); // used until the end of lua_close (a finalizer may read fields)
    lua_close(state);
    imlui_lua_freekeys(keys);
    delete pool; // after lua_close, which frees its blocks through it
}
Alloc getallocf(State* state, void** ud) { return lua_getallocf(state, ud); }
std::size_t trimMemory(State* state, const TrimOptions& options) {
    Pool* pool = poolOf(state);
    return pool ? pool->trimIfWorth(options) : 0;
}
MemoryStats memoryStats(State* state) {
    const Pool* pool = poolOf(state);
    return pool ? pool->stats() : MemoryStats{};
}
void openlibs(State* state) { luaL_openlibs(state); }
CFunction atpanic(State* state, CFunction panicf) { return lua_atpanic(state, panicf); }
void sethook(State* state, Hook hook, int mask, int count) { lua_sethook(state, hook, mask, count); }
State* mainthread(State* state) {
    lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    State* main = lua_tothread(state, -1);
    lua_pop(state, 1);
    return main;
}
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
std::uint64_t strid(State* state, int idx) { return imlui_lua_strid(state, idx); }
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
Type rawgetfield(State* state, int idx, const char* k) { return toType(imlui_lua_rawgetfield(state, idx, k)); }
int Key::nextSlot() {
    static std::atomic<int> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
}
Type rawget(State* state, int idx, const Key& key) {
    return toType(imlui_lua_rawgetkey(state, idx, key.slot(), key.name().c_str()));
}
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
