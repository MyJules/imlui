// LuaFunction: a C++ handle to a Lua function, so a script can hand C++ a callback:
// `backend:forEach(function(x) ... end)`.
//
// It converts through LuaConvert like any value (a property, a method argument or result, a struct field, a signal
// argument) and is called with C++ arguments, which convert through LuaConvert too:
//
//   void Backend::forEach(imlui::LuaFunction fn) { for (int x : m_items) fn.call(x); }
//   std::optional<bool> keep = filter.call<bool>(item);   // nullopt: an error, or expired (bool: Lua truthiness)
//
// The function lives in a Lua state, which a hot reload closes. The handle never keeps a state alive and never
// dangles: each state carries a liveness token (a userdata in its registry whose finalizer clears the token when the
// state closes; lua_close runs every finalizer before it frees anything), and a handle holds that token. Once the
// state is closing or closed, the handle is expired(): calling it does nothing and returns false/nullopt, pushing it
// pushes nil, and destroying it doesn't touch the state. While the state lives, destroying the last copy releases
// the function's registry reference.
//
// Main thread only, like everything that touches Lua: create, copy, call and destroy it there. A worker that wants to
// call it posts to the main thread (ScriptObject::Handle::post) and calls a copy the object keeps.
export module imlui.types.lua_function;

import std;

import lua;
import imlui.lua.errors;
import imlui.types.lua_convert;

export namespace imlui {

class LuaFunction {
public:
    /// What call<R>() returns: bool for R = void (whether the function ran without an error), else the result.
    template <class R>
    using CallResult = std::conditional_t<std::is_void_v<R>, bool, std::optional<R>>;

    /// An empty handle: expired, calls do nothing.
    LuaFunction() = default;

    /// Main thread: a handle to the function at `idx` of `state` (any thread of the state: it is kept through the
    /// state's main thread). Empty if the value isn't a function. May raise a Lua memory error.
    LuaFunction(lua::State* state, int idx);

    /// True for an empty handle and once the function's state is closing or closed.
    [[nodiscard]] bool expired() const;
    explicit operator bool() const { return !expired(); }

    /// Calls the function with `args` (converted by LuaConvert) on the main thread of its state. Errors (a Lua
    /// error with its traceback, a C++ exception while converting) go to the error overlay and never propagate.
    /// R = void: returns whether the function ran without an error (false when expired). Otherwise the first result
    /// converted to R, or std::nullopt when expired, after an error, or when the result doesn't convert (reported:
    /// "LuaFunction: bad result (integer expected, got string)"). R = bool reads Lua truthiness, so a function that
    /// returns nothing gives false; call<std::optional<bool>> tells nil (nullopt inside) from false. Doesn't request a
    /// frame: call imlui::requestFrame() if the callback changed what is shown and no frame is coming anyway.
    template <class R = void, class... Args>
    CallResult<R> call(const Args&... args) const {
        CallResult<R> result{};
        lua::State* state = liveState();
        if (!state) return result;
        luaui::protectedCall(state, "LuaFunction", [&] {
            lua::checkstack(state, static_cast<int>(sizeof...(Args)) + 4);
            pushFunction(state);
            (LuaConvert<LuaArgType<Args>>::push(state, args), ...);
            if (!luaui::safeCall(state, static_cast<int>(sizeof...(Args)), std::is_void_v<R> ? 0 : 1)) return;
            if constexpr (std::is_void_v<R>) {
                result = true;
            } else {
                static_assert(LuaReadable<R>, "call<R>: R needs a LuaConvert<R> with check() and get()");
                if (LuaConvert<R>::check(state, -1))
                    result.emplace(LuaConvert<R>::get(state, -1));
                else
                    reportBadResult(luaMismatchOf(LuaConvert<R>::name, luaMismatch<R>(state, -1)));
            }
        });
        return result;
    }

    /// Main thread: pushes the function onto `state`, or nil when expired or when `state` belongs to another Lua
    /// state (a handle from before a hot reload). Returns whether it pushed the function.
    bool push(lua::State* state) const;

    /// The same Lua function (in the same state). Empty handles are equal.
    [[nodiscard]] bool operator==(const LuaFunction& other) const;

private:
    /// A state's liveness token, shared by the handles of its functions.
    struct Life;
    /// The registry reference of one function, shared by the copies of a handle; released by the last one.
    struct Ref;

    /// The main thread of the function's state, or nullptr when expired.
    [[nodiscard]] lua::State* liveState() const;
    /// Pushes the function onto `state` (its state's main thread; the handle isn't expired).
    void pushFunction(lua::State* state) const;
    /// Reports "LuaFunction: bad result (<why>)".
    static void reportBadResult(std::string_view why);
    /// The liveness token of the state `state` belongs to, made at its first use; a closing state's is dead.
    static std::shared_ptr<Life> lifeOf(lua::State* state);
    /// The token's finalizer (__gc).
    static int lifeGc(lua::State* state);

    std::shared_ptr<const Ref> m_ref;
};

/// A Lua function. nil reads as an empty handle only through std::optional<LuaFunction>.
template <>
struct LuaConvert<LuaFunction> {
    static constexpr std::string_view name = "function";
    /// A function field of an element is the property's value, not a binding (see takesLuaFunction).
    static constexpr bool takesFunction = true;
    static void push(lua::State* s, const LuaFunction& f) { f.push(s); }
    static bool check(lua::State* s, int idx) { return lua::isfunction(s, idx); }
    static LuaFunction get(lua::State* s, int idx) { return LuaFunction{s, idx}; }
};

} // namespace imlui

module :private;

namespace imlui {

struct LuaFunction::Life {
    lua::State* state = nullptr; ///< the main thread; valid while `alive`
    bool alive = false;          ///< main thread only, like the state
};

struct LuaFunction::Ref {
    Ref(std::shared_ptr<Life> life, int ref, const void* identity)
        : life{std::move(life)}, ref{ref}, identity{identity} {}
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&&) = delete;
    Ref& operator=(Ref&&) = delete;
    /// Releases the reference while the state lives; a closed state (or one closing, whose token was cleared) isn't
    /// touched.
    ~Ref() {
        if (life->alive) lua::unref(life->state, ref);
    }

    std::shared_ptr<Life> life;
    int ref;
    const void* identity; ///< the function's address, for operator== (the reference keeps it from being reused)
};

namespace {

/// Registry key of the state's liveness token: a full userdata holding a std::shared_ptr<Life>.
constexpr const char* lifeKey = "imlui.LuaFunction.life";

} // namespace

/// The token's finalizer: runs when the state closes (lua_close runs finalizers before freeing anything; the token
/// is reachable from the registry, so it isn't collected earlier). Marks every handle of the state expired and leaves
/// an empty pointer, which a later lifeOf() (a finalizer that runs after this one) reads as "closing".
int LuaFunction::lifeGc(lua::State* state) {
    using LifePtr = std::shared_ptr<Life>;
    // Only the token itself: a script that reached this function (debug.getregistry, debug.getmetatable) may call it
    // on any userdata, and the registry still holds the token while lua_close runs the finalizers.
    lua::getfield(state, lua::registryIndex, lifeKey);
    const bool token =
        lua::rawequal(state, 1, -1) && lua::rawlen(state, 1) == static_cast<lua::Integer>(sizeof(LifePtr));
    lua::pop(state);
    auto* p = token ? static_cast<LifePtr*>(lua::touserdata(state, 1)) : nullptr;
    if (p && *p) {
        (*p)->alive = false;
        p->reset();
    }
    return 0;
}

std::shared_ptr<LuaFunction::Life> LuaFunction::lifeOf(lua::State* state) {
    using LifePtr = std::shared_ptr<Life>;
    lua::checkstack(state, 3);
    if (lua::getfield(state, lua::registryIndex, lifeKey) == lua::Type::UserData) {
        LifePtr life = *static_cast<LifePtr*>(lua::touserdata(state, -1));
        lua::pop(state);
        return life ? life : std::make_shared<Life>(); // finalized: the state is closing
    }
    lua::pop(state);
    // The metatable first: once the userdata holds the pointer it must get its finalizer without another allocation
    // (which could raise) in between.
    lua::createtable(state, 0, 2);
    lua::pushcfunction(state, &lifeGc);
    lua::setfield(state, -2, "__gc");
    lua::pushboolean(state, false);
    lua::setfield(state, -2, "__metatable");
    auto life = std::make_shared<Life>(Life{lua::mainthread(state), true});
    void* memory = lua::newuserdata(state, sizeof(LifePtr));
    std::construct_at(static_cast<LifePtr*>(memory), life);
    lua::insert(state, -2);
    lua::setmetatable(state, -2);
    lua::setfield(state, lua::registryIndex, lifeKey);
    return life;
}

LuaFunction::LuaFunction(lua::State* state, int idx) {
    if (!lua::isfunction(state, idx)) return;
    idx = lua::absindex(state, idx);
    std::shared_ptr<Life> life = lifeOf(state);
    if (!life->alive) return;
    const void* identity = lua::topointer(state, idx);
    lua::pushvalue(state, idx);
    const int ref = lua::ref(state);
    m_ref = std::make_shared<const Ref>(std::move(life), ref, identity);
}

bool LuaFunction::expired() const { return !m_ref || !m_ref->life->alive; }

lua::State* LuaFunction::liveState() const { return expired() ? nullptr : m_ref->life->state; }

void LuaFunction::pushFunction(lua::State* state) const { lua::rawgeti(state, lua::registryIndex, m_ref->ref); }

bool LuaFunction::push(lua::State* state) const {
    if (expired() || lua::mainthread(state) != m_ref->life->state) {
        lua::pushnil(state);
        return false;
    }
    pushFunction(state);
    return true;
}

bool LuaFunction::operator==(const LuaFunction& other) const {
    if (!m_ref || !other.m_ref) return !m_ref && !other.m_ref;
    return m_ref->life == other.m_ref->life && m_ref->identity == other.m_ref->identity;
}

void LuaFunction::reportBadResult(std::string_view why) {
    std::string msg = "LuaFunction: bad result (";
    msg += why;
    msg += ")";
    luaui::reportError(std::move(msg));
}

} // namespace imlui
