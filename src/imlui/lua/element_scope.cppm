// Element scopes: C++ calls of an element's functions (its bindings and on* handlers) run in the id scope the element
// was built in, so elements they build register their ids there (a component instance's or a delegate's `ids`).
//
// The DSL (src/resources/imlui.lua) owns the scopes. At load it hands over, through native.setScopeHooks, a table
// { [element] = scope } (weak keys; only elements built inside a scope are in it) and `callIn(scope, fn, ...)`, which
// calls fn(...) with that scope active and pops it however the call ends. Before calling a function of an element,
// C++ code passes the call through scopedArgs().
export module imlui.lua.element_scope;

import lua;

export namespace imlui::luaui {

/// Stores the DSL's hooks: the scopes table at `scopes` and the callIn function at `callIn`.
void setScopeHooks(lua::State* state, int scopes, int callIn);

/// Prepares the call of a function of the element table at `element`: the function and its `nargs` arguments are on
/// top of the stack. When the element was built in a scope, inserts callIn and the scope below the function and
/// returns `nargs + 2` (call with that); otherwise leaves the stack as it is and returns `nargs`.
[[nodiscard]] int scopedArgs(lua::State* state, int element, int nargs);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

/// Registry keys (their addresses): two members of one non-const object, so they are distinct. Two identical const
/// objects needn't be: identical code folding may merge them into one.
struct RegistryKeys {
    char scopes;
    char callIn;
};
RegistryKeys keys{};

} // namespace

void setScopeHooks(lua::State* state, int scopes, int callIn) {
    scopes = lua::absindex(state, scopes);
    callIn = lua::absindex(state, callIn);
    lua::pushvalue(state, scopes);
    lua::rawsetp(state, lua::registryIndex, &keys.scopes);
    lua::pushvalue(state, callIn);
    lua::rawsetp(state, lua::registryIndex, &keys.callIn);
}

int scopedArgs(lua::State* state, int element, int nargs) {
    element = lua::absindex(state, element);
    const int fn = lua::gettop(state) - nargs;
    lua::checkstack(state, 3);
    if (lua::rawgetp(state, lua::registryIndex, &keys.scopes) != lua::Type::Table) {
        lua::pop(state);
        return nargs;
    }
    lua::pushvalue(state, element);
    if (lua::rawget(state, -2) == lua::Type::Nil) {
        lua::pop(state, 2);
        return nargs;
    }
    lua::remove(state, -2);                              // fn, args..., scope
    lua::rawgetp(state, lua::registryIndex, &keys.callIn); // fn, args..., scope, callIn
    lua::insert(state, fn);                              // callIn, fn, args..., scope
    lua::insert(state, fn + 1);                          // callIn, scope, fn, args...
    return nargs + 2;
}

} // namespace imlui::luaui
