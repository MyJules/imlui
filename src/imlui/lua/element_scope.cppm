// Element scopes: C++ calls of an element's functions (its bindings and on* handlers) run in the id scope the element
// was built in, so elements they build register their ids there (a component instance's or a delegate's `ids`).
//
// The DSL (src/resources/imlui.lua) owns the scopes. At load it hands over, through native.setScopeHooks, a table
// { [element] = scope } (weak keys; only elements built inside a scope are in it), `callIn(scope, fn, ...)`, which
// calls fn(...) with that scope active and pops it however the call ends, and a table whose [1] is the scope stack in
// use (the DSL swaps it while an async function's coroutine runs). Around a call of a function of an element, C++ code
// pushes the element's scope on that stack itself (ElementScope), which saves the Lua call of callIn; scopedArgs()
// passes the call through callIn instead.
export module imlui.lua.element_scope;

import std;

import lua;
import imlui.lua.errors;

export namespace imlui::luaui {

/// Stores the DSL's hooks: the scopes table at `scopes`, the callIn function at `callIn`, and the table holding the
/// scope stack at `stack` (0: none; ElementScope then calls through callIn).
void setScopeHooks(lua::State* state, int scopes, int callIn, int stack = 0);

/// Prepares the call of a function of the element table at `element`: the function and its `nargs` arguments are on
/// top of the stack. When the element was built in a scope, inserts callIn and the scope below the function and
/// returns `nargs + 2` (call with that); otherwise leaves the stack as it is and returns `nargs`. When the stack has
/// no room for the two (a stack of calls this deep, or out of memory), reports a stack overflow, replaces the function
/// and its arguments with a function that does nothing and returns 0: the caller's protected call then runs nothing
/// (a read gets nil), rather than the element's function outside its scope or a write past the stack here.
[[nodiscard]] int scopedArgs(lua::State* state, int element, int nargs);

/// Runs the call that follows in the id scope of the element table at `element`, like scopedArgs(): the function and
/// its `nargs` arguments are on top of the stack; call with nargs(). When the element was built in a scope, pushes it
/// on the DSL's scope stack (the innermost scope) until this object goes, which pops the innermost scope, as the
/// DSL's own `inScope` does: no Lua call is made for it, and the stack is left as it is. Without the DSL's stack table
/// it inserts callIn like scopedArgs() (nargs() is then `nargs + 2`). On a full stack it reports and skips the call,
/// like scopedArgs().
class ElementScope {
public:
    ElementScope(lua::State* state, int element, int nargs);
    ~ElementScope();
    ElementScope(const ElementScope&) = delete;
    ElementScope& operator=(const ElementScope&) = delete;
    ElementScope(ElementScope&&) = delete;
    ElementScope& operator=(ElementScope&&) = delete;

    /// The arguments to call with.
    [[nodiscard]] int nargs() const { return m_nargs; }

private:
    lua::State* m_state;
    int m_nargs;
    bool m_pushed = false; ///< a scope was pushed on the DSL's stack: popped when this goes
};

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

/// Registry keys (their addresses): two members of one non-const object, so they are distinct. Two identical const
/// objects needn't be: identical code folding may merge them into one.
struct RegistryKeys {
    char scopes;
    char callIn;
    char stack;
};
RegistryKeys keys{};

} // namespace

void setScopeHooks(lua::State* state, int scopes, int callIn, int stack) {
    scopes = lua::absindex(state, scopes);
    callIn = lua::absindex(state, callIn);
    if (stack != 0) stack = lua::absindex(state, stack);
    lua::pushvalue(state, scopes);
    lua::rawsetp(state, lua::registryIndex, &keys.scopes);
    lua::pushvalue(state, callIn);
    lua::rawsetp(state, lua::registryIndex, &keys.callIn);
    if (stack != 0)
        lua::pushvalue(state, stack);
    else
        lua::pushnil(state);
    lua::rawsetp(state, lua::registryIndex, &keys.stack);
}

int scopedArgs(lua::State* state, int element, int nargs) {
    element = lua::absindex(state, element);
    const int fn = lua::gettop(state) - nargs;
    if (!lua::checkstack(state, 3)) {
        reportError("stack overflow (no room to run an element's function in its id scope); the call is skipped");
        // Popping the function and its arguments frees the slot the stand-in takes, so this needs no room.
        lua::settop(state, fn - 1);
        lua::pushcfunction(state, [](lua::State*) { return 0; });
        return 0;
    }
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

ElementScope::ElementScope(lua::State* state, int element, int nargs) : m_state{state}, m_nargs{nargs} {
    element = lua::absindex(state, element);
    if (!lua::checkstack(state, 3)) {
        m_nargs = scopedArgs(state, element, nargs); // reports, and skips the call
        return;
    }
    if (lua::rawgetp(state, lua::registryIndex, &keys.scopes) != lua::Type::Table) {
        lua::pop(state);
        return;
    }
    lua::pushvalue(state, element);
    if (lua::rawget(state, -2) == lua::Type::Nil) { // scopes, nil: built outside every scope
        lua::pop(state, 2);
        return;
    }
    lua::remove(state, -2); // scope
    const bool holder = lua::rawgetp(state, lua::registryIndex, &keys.stack) == lua::Type::Table;
    if (!holder || lua::rawgeti(state, -1, 1) != lua::Type::Table) {
        lua::pop(state, holder ? 3 : 2);
        m_nargs = scopedArgs(state, element, nargs); // no stack to push on: through callIn
        return;
    }
    // scope, holder, stack: stack[#stack + 1] = scope, the DSL's `scopes[#scopes + 1] = scope`
    lua::pushvalue(state, -3);
    lua::rawseti(state, -2, lua::rawlen(state, -2) + 1);
    lua::pop(state, 3);
    m_pushed = true;
}

ElementScope::~ElementScope() {
    if (!m_pushed) return;
    // The innermost scope goes from the stack in use now, like the DSL's POP_SCOPE (`scopes[#scopes] = nil`): the call
    // ran on it whatever coroutines it resumed meanwhile (they swap their own stacks in and back). Only values of our
    // own are pushed, and popped, above whatever the stack holds (while an error unwinds, too).
    lua::State* state = m_state;
    if (!lua::checkstack(state, 2)) return;
    if (lua::rawgetp(state, lua::registryIndex, &keys.stack) == lua::Type::Table) {
        if (lua::rawgeti(state, -1, 1) == lua::Type::Table) {
            if (const lua::Integer n = lua::rawlen(state, -1); n > 0) {
                lua::pushnil(state);
                lua::rawseti(state, -2, n);
            }
        }
        lua::pop(state);
    }
    lua::pop(state);
}

} // namespace imlui::luaui
