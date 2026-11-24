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

/// Pushes exactly one value: `message` as a string, or Lua's memory-error message when Lua is out of memory. The
/// push runs in protected mode, so it never raises: safe inside a catch block, or with C++ objects alive.
void pushMessage(lua::State* state, std::string_view message) noexcept;

/// Pushes "<where><detail>: <what>" (`what` null: "unknown C++ exception") with pushMessage. Never raises or
/// throws.
void pushExceptionMessage(lua::State* state, std::string_view where, std::string_view detail,
                          const char* what) noexcept;

/// Reports "<where><detail>: <what>" (see pushExceptionMessage) as a script error. Never raises or throws.
void reportException(std::string_view where, std::string_view detail, const char* what) noexcept;

/// For a Lua C function that runs C++ code which may throw: returns fn(), an int (a negative result means "the
/// error message is on top of the stack"). A C++ exception becomes "<where><detail>: <what>" on top of the stack
/// and -1. The caller raises it with lua::raise once this returned, so Lua never longjmps out of a catch block or
/// past a live C++ object here, and no C++ exception crosses Lua's C frames. A Lua error raised inside fn passes
/// through (fn must not have C++ objects alive when it raises).
template <class F>
int catchCxx(lua::State* state, const F& fn, std::string_view where, std::string_view detail = {}) {
    try {
        return fn();
    } catch (const std::exception& e) {
        pushExceptionMessage(state, where, detail, e.what());
    } catch (...) {
        pushExceptionMessage(state, where, detail, nullptr);
    }
    return -1;
}

/// For C++ code called from Lua that must not raise (a __gc, an element's tick or componentComplete): runs fn()
/// and reports a C++ exception it throws as a script error ("<where><detail>: <what>") instead of letting it
/// unwind. Returns false after an exception.
template <class F>
bool reportCxx(const F& fn, std::string_view where, std::string_view detail = {}) noexcept {
    try {
        fn();
        return true;
    } catch (const std::exception& e) {
        reportException(where, detail, e.what());
    } catch (...) {
        reportException(where, detail, nullptr);
    }
    return false;
}

/// The value at `idx` as text, like Lua's tostring(): a `__tostring` metamethod runs in protected mode, so one that
/// raises (or returns a non-string) is reported as a script error and yields "" instead of raising.
[[nodiscard]] std::string toDisplayString(lua::State* state, int idx);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

std::vector<std::string> recorded;

/// Arg 1: a std::string_view (light userdata). Pushes it.
int pushMessageFn(lua::State* state) {
    lua::pushstring(state, *static_cast<const std::string_view*>(lua::touserdata(state, 1)));
    return 1;
}

/// Arg 1: any value. Pushes luaL_tolstring's result (runs __tostring).
int toStringFn(lua::State* state) {
    lua::pushToString(state, 1);
    return 1;
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
    } catch (...) { // nothing may unwind into Lua's C frames; the process aborts next anyway
    }
    return 0;
}

bool safeCall(lua::State* state, int nargs, int nresults) {
    int base = lua::gettop(state) - nargs;
    lua::pushcfunction(state, &tracebackHandler);
    lua::insert(state, base);
    int rc = lua::pcall(state, nargs, nresults, base);
    lua::remove(state, base);
    if (rc != lua::ok) {
        reportError(std::string{lua::tostring(state, -1)});
        lua::pop(state);
        return false;
    }
    return true;
}

void pushMessage(lua::State* state, std::string_view message) noexcept {
    lua::checkstack(state, 2);
    lua::pushcfunction(state, &pushMessageFn);
    lua::pushlightuserdata(state, &message);
    // Success leaves the string; a memory error leaves Lua's (preallocated) memory-error message. One value either way.
    lua::pcall(state, 1, 1, 0);
}

void pushExceptionMessage(lua::State* state, std::string_view where, std::string_view detail,
                          const char* what) noexcept {
    const std::string msg = describeException(where, detail, what);
    pushMessage(state, msg.empty() ? std::string_view{"out of memory"} : std::string_view{msg});
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
    lua::checkstack(state, 3);
    lua::pushcfunction(state, &toStringFn);
    lua::pushvalue(state, idx);
    if (!safeCall(state, 1, 1)) return {};
    std::string out{lua::tostring(state, -1)};
    lua::pop(state);
    return out;
}

} // namespace imlui::luaui
