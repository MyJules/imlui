// ScriptObject: the base class of every C++ object exposed to Lua (registered element types and singletons).
//
// A ScriptObject is linked to at most one Lua state at a time. The link is the Lua value that stands for
// the object there: the element table for an element instance, the global userdata for a singleton.
// It is held weakly (a weak-valued registry table keyed by the object's id), so it never keeps
// the element table alive.
//
//  * notify("value") tells Lua that property `value` changed on the C++ side: it is written back to
//    the element table (honouring two-way binds, like a widget edit), `onValueChanged` is emitted, and
//    a frame is requested.
//  * emit("onPeak", args...) calls the `onPeak` handler.
//
// Every object has an id (objectId()): a number no other object of the process has had or will have, so unlike its
// address it is never reused after the object dies. It is the identity of an element (its ImGui ID scope, its key in
// the layout cache, its place in the ticking list).
//
// Main thread only, like everything that touches Lua or ImGui. The exception is a Handle (handle()): a weak,
// copyable token a worker thread holds to post work back to the object, which runs only if the object is still
// alive by then (Lua may collect an element, or close its state on hot reload, at any time).
export module imlui.types.script_object;

import std;

import lua;
import imlui.lua.element_scope;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.types.lua_convert;
import imlui.ui.frame_request;
import imlui.ui.main_thread;

export namespace imlui {

class ScriptObject {
public:
    /// A weak, thread-safe reference to a ScriptObject for worker threads. Copy it freely, to any thread. It
    /// never keeps the object alive and never gives access to it off the main thread: it only says whether the
    /// object still lives, and posts functions that run on the main thread while it does.
    class Handle {
    public:
        /// An empty handle: never alive, posts are dropped.
        Handle() = default;

        /// Thread-safe. True until the object's destructor runs (ScriptObject's, so after a derived destructor);
        /// false for an empty handle. A worker polls it as its "cancelled" flag.
        [[nodiscard]] bool alive() const;

        /// Thread-safe: queues `fn` on the main thread (imlui::post, which wakes the loop). There, `fn` runs only if
        /// the object is still alive; otherwise it is destroyed without running. So `fn` may capture the object's
        /// `this` and call notify()/emit(). Returns false (and drops `fn`) when the object is already gone.
        bool post(std::function<void()> fn) const;

    private:
        friend class ScriptObject;
        explicit Handle(std::shared_ptr<const std::atomic<bool>> alive);
        std::shared_ptr<const std::atomic<bool>> m_alive;
    };

    /// Takes the next object id.
    ScriptObject();
    virtual ~ScriptObject();
    ScriptObject(const ScriptObject&) = delete;
    ScriptObject& operator=(const ScriptObject&) = delete;
    ScriptObject(ScriptObject&&) = delete;
    ScriptObject& operator=(ScriptObject&&) = delete;

    /// Property `property` changed in C++: writes it back to Lua (through Node::set, so a two-way bind
    /// writes its source and a function binding is left alone), emits `on<Property>Changed` with the new
    /// value, and requests a frame. Without a Lua link it only requests a frame.
    void notify(std::string_view property);

    /// Calls the handler `signal` (e.g. "onPeak"). Element handlers get `(self, args...)`, singleton
    /// handlers `(args...)`. Errors go to the error overlay. Requests a frame (a handler usually changes
    /// state). Returns false when there is no handler, no Lua link, or the handler raised an error.
    template <class... Args>
    bool emit(std::string_view signal, const Args&... args) {
        if (!m_state) return false;
        bool called = false;
        // Protected: emit() may run outside every Lua call (a posted function, a tick), where a Lua error raised by
        // an API call below (a script's __index, out of memory) would abort instead of being reported.
        luaui::protectedCall(m_state, "emit", [&] { called = callHandler(signal, args...); });
        requestFrame();
        return called;
    }

    /// Main thread: a Handle that worker threads use to post back to this object safely (see Handle::post).
    [[nodiscard]] Handle handle();

    /// This object's id: unique for the process's lifetime, never 0 and never reused (a monotonic 64-bit count), so
    /// a stale id can't name a newer object that happens to live at a dead one's address.
    [[nodiscard]] std::uint64_t objectId() const;

    /// The Lua state this object is linked to, or nullptr.
    [[nodiscard]] lua::State* luaState() const;
    [[nodiscard]] bool isSingleton() const;

    /// Pushes the linked Lua value (element table or singleton userdata). Pushes nothing and returns
    /// false without a link, or when the element table was already collected.
    bool pushSelf() const;

    // ---- linking, used by TypeRegistry

    /// Links the object to the value at `idx` in `state`.
    void attach(lua::State* state, int idx, bool singleton);
    /// Points the link at `state` again, where attach() was already called (a reload that failed).
    void relink(lua::State* state);
    /// Drops the link (the state is closing or the instance is being destroyed).
    void detach();

private:
    /// emit()'s body, inside its protected call. Returns whether the handler ran without an error.
    template <class... Args>
    bool callHandler(std::string_view signal, const Args&... args) {
        lua::State* state = m_state;
        lua::checkstack(state, static_cast<int>(sizeof...(Args)) + 8);
        const int top = lua::gettop(state);
        if (!pushSelf()) return false;
        const std::string key{signal};
        // A singleton's handlers are behind its userdata's __index; an element's are fields of its table.
        if (m_singleton)
            lua::getfield(state, top + 1, key.c_str());
        else
            luaui::Node{state, top + 1}.pushRaw(key.c_str());
        if (!lua::isfunction(state, -1)) {
            lua::settop(state, top);
            return false;
        }
        int nargs = 0;
        if (!m_singleton) {
            lua::pushvalue(state, top + 1);
            nargs = 1;
        }
        (LuaConvert<LuaArgType<Args>>::push(state, args), ...);
        nargs += static_cast<int>(sizeof...(Args));
        if (!m_singleton) nargs = luaui::scopedArgs(state, top + 1, nargs);
        const bool ok = luaui::safeCall(state, nargs, 0);
        lua::settop(state, top);
        return ok;
    }

    /// notify()'s Lua part, inside its protected call.
    void writeBack(std::string_view property);

    std::uint64_t m_objectId;
    lua::State* m_state = nullptr;
    bool m_singleton = false;
    /// Shared with every Handle, created by the first handle(). Set to false by the destructor.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Registry key of the weak-valued table { [objectId()] = linked value }. Keyed by id, not address: an entry
/// a dead object left behind can't link a new object at its address.
constexpr const char* linksKey = "imlui.objects";

/// Pushes the links table, creating it on first use.
void pushLinks(lua::State* state) {
    if (lua::getfield(state, lua::registryIndex, linksKey) == lua::Type::Table) return;
    lua::pop(state);
    lua::createtable(state, 0, 8);
    lua::createtable(state, 0, 1);
    lua::pushstring(state, "v");
    lua::setfield(state, -2, "__mode");
    lua::setmetatable(state, -2);
    lua::pushvalue(state, -1);
    lua::setfield(state, lua::registryIndex, linksKey);
}

/// The last object id handed out. Atomic: a host may construct a singleton on any thread.
std::atomic<std::uint64_t> lastObjectId{0};

/// The links table key of an object id (Lua integers are 64-bit; ids stay far below 2^63).
lua::Integer linkKey(std::uint64_t id) { return static_cast<lua::Integer>(id); }

/// "value" -> "onValueChanged"
std::string changedSignal(std::string_view property) {
    std::string s = "on";
    s += property;
    if (s.size() > 2 && s[2] >= 'a' && s[2] <= 'z') s[2] = static_cast<char>(s[2] - 'a' + 'A');
    s += "Changed";
    return s;
}

} // namespace

ScriptObject::ScriptObject() : m_objectId{lastObjectId.fetch_add(1, std::memory_order_relaxed) + 1} {}

std::uint64_t ScriptObject::objectId() const { return m_objectId; }

ScriptObject::~ScriptObject() {
    // Destructors run on the main thread, like the posted functions that check it: none can run past this point.
    if (m_alive) m_alive->store(false, std::memory_order_release);
    if (!m_state) return;
    // Destroyed while still linked (e.g. a singleton that dies before the engine): forget the link.
    pushLinks(m_state);
    lua::pushnil(m_state);
    lua::rawseti(m_state, -2, linkKey(m_objectId));
    lua::pop(m_state);
}

ScriptObject::Handle::Handle(std::shared_ptr<const std::atomic<bool>> alive) : m_alive{std::move(alive)} {}

bool ScriptObject::Handle::alive() const { return m_alive && m_alive->load(std::memory_order_acquire); }

bool ScriptObject::Handle::post(std::function<void()> fn) const {
    if (!fn || !alive()) return false;
    imlui::post([alive = m_alive, fn = std::move(fn)] {
        // On the main thread, where the object is destroyed too: it can't die between this check and fn().
        if (alive->load(std::memory_order_acquire)) fn();
    });
    return true;
}

ScriptObject::Handle ScriptObject::handle() {
    if (!m_alive) m_alive = std::make_shared<std::atomic<bool>>(true);
    return Handle{m_alive};
}

lua::State* ScriptObject::luaState() const { return m_state; }
bool ScriptObject::isSingleton() const { return m_singleton; }

bool ScriptObject::pushSelf() const {
    if (!m_state) return false;
    pushLinks(m_state);
    const lua::Type t = lua::rawgeti(m_state, -1, linkKey(m_objectId));
    lua::remove(m_state, -2);
    if (t == lua::Type::Nil) {
        lua::pop(m_state);
        return false;
    }
    return true;
}

void ScriptObject::attach(lua::State* state, int idx, bool singleton) {
    idx = lua::absindex(state, idx);
    m_state = state;
    m_singleton = singleton;
    pushLinks(state);
    lua::pushvalue(state, idx);
    lua::rawseti(state, -2, linkKey(m_objectId));
    lua::pop(state);
}

void ScriptObject::relink(lua::State* state) { m_state = state; }

void ScriptObject::detach() { m_state = nullptr; }

void ScriptObject::notify(std::string_view property) {
    requestFrame();
    if (!m_state) return;
    // Protected: notify() may run outside every Lua call (a posted function, a tick), where a Lua error raised by an
    // API call (a script's __index/__newindex on the element table, out of memory) would abort instead of being
    // reported.
    luaui::protectedCall(m_state, "notify", [&] { writeBack(property); });
}

void ScriptObject::writeBack(std::string_view property) {
    lua::State* state = m_state;
    lua::checkstack(state, 16);
    const int top = lua::gettop(state);
    if (!pushSelf()) return;
    const int self = top + 1;
    const std::string name{property};

    // The current value comes from the getter, through the box metatable's __get(box, name) (installed
    // by TypeRegistry), so this module needs no compile-time knowledge of the type.
    if (m_singleton) {
        lua::pushvalue(state, self);
    } else {
        lua::pushstring(state, "__native");
        lua::rawget(state, self);
    }
    const int box = lua::gettop(state);
    bool ok = lua::isuserdata(state, box) && lua::getmetatable(state, box) != 0;
    if (ok) {
        lua::getfield(state, -1, "__get");
        lua::remove(state, -2);
        lua::pushvalue(state, box);
        lua::pushstring(state, name);
        ok = luaui::safeCall(state, 2, 1);
    }
    if (!ok) {
        lua::settop(state, top);
        return;
    }
    const int value = lua::gettop(state);

    if (!m_singleton) {
        lua::pushvalue(state, value);
        luaui::Node{state, self}.setTop(name.c_str());
    }

    const std::string signal = changedSignal(property);
    const lua::Type handler =
        m_singleton ? lua::getfield(state, self, signal.c_str()) : luaui::Node{state, self}.pushRaw(signal.c_str());
    if (handler == lua::Type::Function) {
        int nargs = 1;
        if (!m_singleton) {
            lua::pushvalue(state, self);
            ++nargs;
        }
        lua::pushvalue(state, value);
        if (!m_singleton) nargs = luaui::scopedArgs(state, self, nargs);
        luaui::safeCall(state, nargs, 0);
    }
    lua::settop(state, top);
}

} // namespace imlui
