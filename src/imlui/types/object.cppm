// Object: the base class of every C++ object exposed to Lua (registered element types and singletons).
//
// An Object is linked to at most one Lua state at a time. The link is the Lua value that stands for
// the object there: the element table for an element instance, the global userdata for a singleton.
// It is held weakly (a weak-valued registry table keyed by the object's address), so it never keeps
// the element table alive.
//
//  * notify("value") tells Lua that property `value` changed on the C++ side: it is written back to
//    the element table (honouring two-way binds, like a widget edit), `onValueChanged` is emitted, and
//    a frame is requested.
//  * emit("onPeak", args...) calls the `onPeak` handler.
//
// Main thread only, like everything that touches Lua or ImGui.
module;

#include <string>
#include <string_view>

export module imlui.types.object;

import lua;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.types.lua_convert;
import imlui.ui.frame_request;

export namespace imlui {

class Object {
public:
    Object() = default;
    virtual ~Object();
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

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
        lua::State* state = m_state;
        lua::checkstack(state, static_cast<int>(sizeof...(Args)) + 8);
        const int top = lua::gettop(state);
        if (!pushSelf()) return false;
        const std::string key{signal};
        lua::getfield(state, top + 1, key.c_str());
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
        const bool ok = luaui::safeCall(state, nargs, 0);
        lua::settop(state, top);
        requestFrame();
        return ok;
    }

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
    lua::State* m_state = nullptr;
    bool m_singleton = false;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Registry key of the weak-valued table { [lightuserdata Object*] = linked value }.
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

/// "value" -> "onValueChanged"
std::string changedSignal(std::string_view property) {
    std::string s = "on";
    s += property;
    if (s.size() > 2 && s[2] >= 'a' && s[2] <= 'z') s[2] = static_cast<char>(s[2] - 'a' + 'A');
    s += "Changed";
    return s;
}

} // namespace

Object::~Object() {
    if (!m_state) return;
    // Destroyed while still linked (e.g. a singleton that dies before the engine): forget the link.
    pushLinks(m_state);
    lua::pushnil(m_state);
    lua::rawsetp(m_state, -2, this);
    lua::pop(m_state);
}

lua::State* Object::luaState() const { return m_state; }
bool Object::isSingleton() const { return m_singleton; }

bool Object::pushSelf() const {
    if (!m_state) return false;
    pushLinks(m_state);
    const lua::Type t = lua::rawgetp(m_state, -1, this);
    lua::remove(m_state, -2);
    if (t == lua::Type::Nil) {
        lua::pop(m_state);
        return false;
    }
    return true;
}

void Object::attach(lua::State* state, int idx, bool singleton) {
    idx = lua::absindex(state, idx);
    m_state = state;
    m_singleton = singleton;
    pushLinks(state);
    lua::pushvalue(state, idx);
    lua::rawsetp(state, -2, this);
    lua::pop(state);
}

void Object::relink(lua::State* state) { m_state = state; }

void Object::detach() { m_state = nullptr; }

void Object::notify(std::string_view property) {
    requestFrame();
    if (!m_state) return;
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
    if (lua::getfield(state, self, signal.c_str()) == lua::Type::Function) {
        int nargs = 1;
        if (!m_singleton) {
            lua::pushvalue(state, self);
            ++nargs;
        }
        lua::pushvalue(state, value);
        luaui::safeCall(state, nargs, 0);
    }
    lua::settop(state, top);
}

} // namespace imlui
