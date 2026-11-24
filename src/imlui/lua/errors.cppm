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

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {
std::vector<std::string> recorded;
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

} // namespace imlui::luaui
