// Script error collection and protected calls into Lua.
export module imlui.lua.errors;

import std;

import lua;
import imlui.log;

export namespace imlui::luaui {

/// Records a script error (deduplicated, capped) and prints it once.
void reportError(std::string msg);

/// Errors recorded since the last clearErrors(), oldest first.
[[nodiscard]] const std::vector<std::string>& errors();
void clearErrors();

/// Message handler for lua::pcall: appends a traceback to the error.
int tracebackHandler(lua::State* state);

/// Panic function for lua::atpanic: logs the error that escaped every protected call. Lua aborts the process
/// when it returns; installing it only makes that abort say why.
int panicHandler(lua::State* state);

/// Calls the function below `nargs` arguments at the top of the stack in
/// protected mode. On success leaves `nresults` values; on failure reports
/// the error (with traceback) and leaves nothing.
bool safeCall(lua::State* state, int nargs, int nresults);
/// safeCall() for a call that only reads (a property's binding, a bind source, `__children`): it doesn't count as a
/// possible change of the Lua state (changes()), and neither do the calls it makes.
bool safeRead(lua::State* state, int nargs, int nresults);

/// A count that moves whenever the Lua state may have changed: every safeCall() outside a safeRead() (a handler, a
/// write-back), and every noteChange(). While it stands still within a frame, an element's properties read the same,
/// so the TypeRegistry syncs each element at most once a frame.
[[nodiscard]] std::uint64_t changes();
/// Counts a change made without a call (C++ writing a table field).
void noteChange();
/// Whether a safeRead() is running: the Lua code running now (a binding, a bind source) only reads, so it changes
/// nothing a sync reads (a write to an element's declared field is still caught by the sync's fingerprint).
[[nodiscard]] bool reading();

/// Raises the Lua error "<where><detail>: <what>" (`what` null: "unknown C++ exception"). Never returns.
[[noreturn]] void raiseException(lua::State* state, std::string_view where, std::string_view detail, const char* what);

/// Reports "<where><detail>: <what>" (see raiseException) as a script error. Never raises or throws.
void reportException(std::string_view where, std::string_view detail, const char* what) noexcept;

/// For a Lua C function that runs C++ code which may throw: returns fn(). A C++ exception becomes the Lua error
/// "<where><detail>: <what>"; a Lua error raised inside fn (lua::Raised) passes through. Lua is compiled as C++, so
/// both unwind the C++ frames in between normally: fn may hold C++ objects and raise anywhere.
template <class F>
decltype(auto) catchCxx(lua::State* state, const F& fn, std::string_view where, std::string_view detail = {}) {
    try {
        return fn();
    } catch (const lua::Raised&) {
        throw;
    } catch (const std::exception& e) {
        raiseException(state, where, detail, e.what());
    } catch (...) {
        raiseException(state, where, detail, nullptr);
    }
}

/// For C++ code called from Lua that must not raise (a __gc, an element's tick or componentComplete): runs fn()
/// and reports a C++ exception it throws as a script error ("<where><detail>: <what>") instead of letting it
/// unwind. Returns false after an exception. A Lua error raised inside fn (lua::Raised) passes through: it isn't
/// this function's to swallow.
template <class F>
bool reportCxx(const F& fn, std::string_view where, std::string_view detail = {}) {
    try {
        fn();
        return true;
    } catch (const lua::Raised&) {
        throw;
    } catch (const std::exception& e) {
        reportException(where, detail, e.what());
    } catch (...) {
        reportException(where, detail, nullptr);
    }
    return false;
}

/// Runs fn() in protected mode on `state`, for C++ code outside every Lua call that uses the Lua API (a posted
/// function's notify()/emit(), setting up a new state): an API call that raises (a script's __index/__newindex, out of
/// memory) has no protected call to unwind to, so Lua would call its panic handler and abort. Here such an error, and
/// a C++ exception fn throws ("<where>: <what>"), is reported (with a traceback) instead. fn runs in a fresh Lua stack
/// frame (gettop() is 0 when it starts); the caller's stack is left as it was. Returns false after an error.
template <class F>
bool protectedCall(lua::State* state, std::string_view where, const F& fn);

/// The value at `idx` as text, like Lua's tostring(): a `__tostring` metamethod runs in protected mode, so one that
/// raises (or returns a non-string) is reported as a script error and yields "" instead of raising. It runs as a read
/// (safeRead): showing a value doesn't move changes().
[[nodiscard]] std::string toDisplayString(lua::State* state, int idx);

/// protectedCall's untyped core: calls invoke(state, context) in protected mode (see protectedCall).
bool runProtected(lua::State* state, void (*invoke)(lua::State*, void*), void* context);

template <class F>
bool protectedCall(lua::State* state, std::string_view where, const F& fn) {
    struct Context {
        const F& fn;
        std::string_view where;
    } context{fn, where};
    return runProtected(
        state,
        [](lua::State* s, void* p) {
            const Context& c = *static_cast<const Context*>(p);
            catchCxx(s, c.fn, c.where);
        },
        &context);
}

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

std::vector<std::string> recorded;
std::uint64_t changeCount = 0; // main thread only, like all Lua code
int readDepth = 0;             ///< safeRead() calls running

/// Leaves the read depth as it was, also when the call throws (a C++ exception through a Lua error).
class ReadScope {
public:
    ReadScope() { ++readDepth; }
    ~ReadScope() { --readDepth; }
    ReadScope(const ReadScope&) = delete;
    ReadScope& operator=(const ReadScope&) = delete;
    ReadScope(ReadScope&&) = delete;
    ReadScope& operator=(ReadScope&&) = delete;
};

/// Arg 1: any value. Pushes luaL_tolstring's result (runs __tostring).
int toStringFn(lua::State* state) {
    lua::pushToString(state, 1);
    return 1;
}

/// What runProtected hands its trampoline (as light userdata: a function pointer can't be one).
struct ProtectedCall {
    void (*invoke)(lua::State*, void*);
    void* context;
};

/// Arg 1: the ProtectedCall. Runs it on an empty stack frame.
int protectedTrampoline(lua::State* state) {
    const ProtectedCall call = *static_cast<const ProtectedCall*>(lua::touserdata(state, 1));
    lua::settop(state, 0);
    call.invoke(state, call.context);
    return 0;
}

/// The status of a pcall that a foreign C++ exception ended (external/lua_cxx/imlui_lua.cpp, as upstream Lua).
constexpr int lostExceptionStatus = -1;

/// lua::pcall of the function below `nargs` arguments, with tracebackHandler; reports the error of a failed call.
bool pcallReporting(lua::State* state, int nargs, int nresults) {
    const int base = lua::gettop(state) - nargs;
    lua::pushcfunction(state, &tracebackHandler);
    lua::insert(state, base);
    const int rc = lua::pcall(state, nargs, nresults, base);
    lua::remove(state, base);
    if (rc != lua::ok) {
        // Status -1: a C++ exception that wasn't a Lua error, std::bad_alloc or one a C function turned into a Lua
        // error (catchCxx) reached the pcall. The value Lua leaves is whatever was on top, not a message.
        reportError(rc == lostExceptionStatus ? std::string{"unknown C++ exception"}
                                              : std::string{lua::tostring(state, -1)});
        lua::pop(state);
        return false;
    }
    return true;
}

/// "<where><detail>: <what>", or "out of memory" when even that can't be built.
std::string describeException(std::string_view where, std::string_view detail, const char* what) noexcept {
    try {
        std::string msg{where};
        msg += detail;
        msg += ": ";
        msg += what ? what : "unknown C++ exception";
        return msg;
    } catch (...) {
        return {};
    }
}

} // namespace

void reportError(std::string msg) {
    for (const auto& e : recorded)
        if (e == msg) return;
    log::write(log::Level::Error, msg, "lua");
    if (recorded.size() >= 32) recorded.erase(recorded.begin());
    recorded.push_back(std::move(msg));
}

const std::vector<std::string>& errors() { return recorded; }
void clearErrors() { recorded.clear(); }

int tracebackHandler(lua::State* state) {
    std::string msg = lua::isstring(state, 1) ? std::string{lua::tostring(state, 1)} : lua::toStringAny(state, 1);
    lua::traceback(state, state, msg.c_str(), 1);
    return 1;
}

int panicHandler(lua::State* state) {
    try {
        const std::string msg =
            lua::isstring(state, -1) ? std::string{lua::tostring(state, -1)} : std::string{lua::typeName(state, -1)};
        log::write(log::Level::Critical, "unprotected Lua error, aborting: " + msg, "lua");
    } catch (...) { // the process aborts next anyway
    }
    return 0;
}

bool safeCall(lua::State* state, int nargs, int nresults) {
    // Counted before and after: code inside the call (a sync from a method) and code after it see new counts.
    if (readDepth == 0) ++changeCount;
    struct After {
        ~After() {
            if (readDepth == 0) ++changeCount;
        }
    } after;
    return pcallReporting(state, nargs, nresults);
}

bool safeRead(lua::State* state, int nargs, int nresults) {
    const ReadScope scope;
    return safeCall(state, nargs, nresults);
}

bool runProtected(lua::State* state, void (*invoke)(lua::State*, void*), void* context) {
    if (!lua::checkstack(state, 4)) {
        reportError("stack overflow (no room for a protected call)");
        return false;
    }
    const int top = lua::gettop(state);
    ProtectedCall call{invoke, context};
    // Neither push allocates (a light C function, a light userdata), so neither can raise out here.
    lua::pushcfunction(state, &protectedTrampoline);
    lua::pushlightuserdata(state, &call);
    // Not a change by itself (an emit() that finds no handler changes nothing); the calls fn makes count as usual.
    // An error may have cut fn short after a write, so a failed call counts.
    const bool ok = pcallReporting(state, 1, 0);
    if (!ok && readDepth == 0) noteChange();
    lua::settop(state, top);
    return ok;
}

std::uint64_t changes() { return changeCount; }
void noteChange() { ++changeCount; }
bool reading() { return readDepth > 0; }

void raiseException(lua::State* state, std::string_view where, std::string_view detail, const char* what) {
    const std::string msg = describeException(where, detail, what);
    lua::error(state, msg.empty() ? std::string_view{"out of memory"} : std::string_view{msg});
}

void reportException(std::string_view where, std::string_view detail, const char* what) noexcept {
    try {
        std::string msg = describeException(where, detail, what);
        reportError(msg.empty() ? std::string{"out of memory"} : std::move(msg));
    } catch (...) { // out of memory while reporting: nothing more to do
    }
}

std::string toDisplayString(lua::State* state, int idx) {
    if (lua::type(state, idx) == lua::Type::String) return std::string{lua::tostring(state, idx)};
    idx = lua::absindex(state, idx);
    if (!lua::checkstack(state, 3)) { // no room to call __tostring: say so instead of writing past the stack
        reportError("stack overflow (no room to convert a value to a string)");
        return {};
    }
    lua::pushcfunction(state, &toStringFn);
    lua::pushvalue(state, idx);
    if (!safeRead(state, 1, 1)) return {}; // showing a value is a read, not a possible change
    std::string out{lua::tostring(state, -1)};
    lua::pop(state);
    return out;
}

} // namespace imlui::luaui
