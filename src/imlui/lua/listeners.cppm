// Signal listeners: the functions `connect(target, signal, fn)` adds to a signal, besides its one handler slot.
//
// The DSL (src/resources/imlui.lua, ui.connect) keeps the listeners: per target (an element table or a singleton
// userdata) and signal, in connect order. It hands C++ one function through setListenerDispatch (the `imlui.types`
// library's setDispatch): dispatch(target, signal, args...) calls the listeners connected when it starts, each
// protected, and returns whether one ran without an error. ScriptObject::emit and notify call it through
// callListeners, after the handler slot, once a listener was connected to the object (ScriptObject::noteConnection):
// an object without listeners pays nothing more than a flag check.
export module imlui.lua.listeners;

import std;

import lua;
import imlui.lua.errors;

export namespace imlui::luaui {

/// Stores the DSL's dispatch function, at `idx`, for this state.
void setListenerDispatch(lua::State* state, int idx);

/// Calls the listeners of `signal` on the value at `target` with the `nargs` values from `first` up, which stay on the
/// stack. Errors are reported. Returns whether a listener ran without an error (false without a dispatch function).
bool callListeners(lua::State* state, int target, const char* signal, int first, int nargs);

/// The change signal of property `property`, which ScriptObject::notify emits: "value" -> "onValueChanged".
[[nodiscard]] std::string changedSignal(std::string_view property);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

/// Registry key (its address) of the dispatch function.
char dispatchKey = 0;

} // namespace

void setListenerDispatch(lua::State* state, int idx) {
    lua::pushvalue(state, idx);
    lua::rawsetp(state, lua::registryIndex, &dispatchKey);
}

bool callListeners(lua::State* state, int target, const char* signal, int first, int nargs) {
    target = lua::absindex(state, target);
    first = lua::absindex(state, first);
    // No room for the call (a C stack of calls this deep, or out of memory): reported like an error a listener raised,
    // and the listeners don't run. Pushing anyway would write past the stack.
    if (nargs < 0 || !lua::checkstack(state, nargs + 4)) {
        reportError(std::string{"stack overflow (no room to call the listeners of "} + signal + ")");
        return false;
    }
    if (lua::rawgetp(state, lua::registryIndex, &dispatchKey) != lua::Type::Function) {
        lua::pop(state);
        return false;
    }
    lua::pushvalue(state, target);
    lua::pushstring(state, signal);
    for (int i = 0; i < nargs; ++i)
        lua::pushvalue(state, first + i);
    if (!safeCall(state, nargs + 2, 1)) return false;
    const bool ran = lua::toboolean(state, -1);
    lua::pop(state);
    return ran;
}

std::string changedSignal(std::string_view property) {
    std::string s = "on";
    s += property;
    if (s.size() > 2 && s[2] >= 'a' && s[2] <= 'z') s[2] = static_cast<char>(s[2] - 'a' + 'A');
    s += "Changed";
    return s;
}

} // namespace imlui::luaui
