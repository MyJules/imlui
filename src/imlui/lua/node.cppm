// Node: a UI element on the Lua side, seen from C++.
//
// An element is a plain table: { __type = "Button", text = "OK",
// onClicked = function(self) ... end, [1..n] = children }.
// Property values may be:
//   * plain values           text = "Hello"
//   * bindings (functions)   text = function(self) return "n = " .. s.n end
//                            re-evaluated every frame, like a QML binding
//   * two-way bindings       value = ui.bind(state, "volume")
// `Node` reads/writes properties honouring those three forms and emits
// signals (on* handlers) safely.
export module imlui.lua.node;

import std;

import lua;
import imlui.color;
import imlui.theme.theme;
import imlui.lua.element_scope;
import imlui.lua.errors;
import imlui.lua.value;

export namespace imlui::luaui {

/// A field of a metatable shared by all element tables of a type (the registered types' and the DSL's plain one):
/// such a metatable only adds methods and `parent`, never a property, so property reads skip it (see pushRaw).
inline constexpr const char* sharedMetaMarker = "__imlui_shared";

class Node {
public:
    /// Wraps the table at stack index `idx` (converted to an absolute index).
    Node(lua::State* state, int idx);

    [[nodiscard]] lua::State* state() const;
    [[nodiscard]] int index() const;
    [[nodiscard]] const void* ptr() const;

    /// The element type (`__type`), e.g. "Button".
    [[nodiscard]] std::string type() const;

    /// Pushes the raw (unresolved) field: the table's own, else what its metatable's __index gives, unless that
    /// is a metatable shared by a type's elements (sharedMetaMarker), which holds no properties.
    lua::Type pushRaw(const char* key) const;

    /// Pushes the resolved value of property `key`; returns its Lua type.
    lua::Type push(const char* key) const;

    [[nodiscard]] bool has(const char* key) const;

    [[nodiscard]] std::optional<double> number(const char* key) const;
    [[nodiscard]] double number(const char* key, double def) const;
    [[nodiscard]] float numf(const char* key, float def) const;
    [[nodiscard]] bool boolean(const char* key, bool def) const;
    [[nodiscard]] std::string string(const char* key, std::string_view def = {}) const;
    [[nodiscard]] std::optional<Color> color(const char* key) const;

    /// The element's main text: `text` property, or the first positional string.
    [[nodiscard]] std::string text(const char* key = "text") const;

    /// Reads a list model of strings (or values convertible to string).
    [[nodiscard]] std::vector<std::string> strings(const char* key) const;
    [[nodiscard]] std::vector<float> floats(const char* key) const;

    /// Writes a property, honouring bindings:
    ///  * two-way bind -> writes the bound source
    ///  * function binding -> left alone (the source owns the value)
    ///  * otherwise -> stored on the element itself
    void set(const char* key, const Value& v) const;

    /// Like set(), with the value on top of the stack (any Lua value); pops it.
    void setTop(const char* key) const;

    /// Calls handler `signal` (e.g. "onClicked") as handler(self, args...).
    bool emit(const char* signal, std::initializer_list<Value> args = {}) const;

    /// Reads {r, g, b[, a]} at `idx`; nullopt unless r, g and b are numbers.
    static std::optional<Color> colorFromTable(lua::State* state, int idx);

private:
    /// Replaces the bind object at `idx` (relative) with the bound value.
    void resolveBind(int idx) const;

    /// What the table's metatable is to property reads, found at the first read of a field the table lacks.
    enum class Meta : std::uint8_t { Unknown, Skip, Index };

    lua::State* m_state;
    int m_index;
    mutable Meta m_meta = Meta::Unknown;
};

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

// bindGet(t, k) -> t[k] and bindSet(t, k, v): a two-way bind's source access, called through safeCall because
// the source's metamethods (a ui.state proxy's __newindex runs watchers) can raise.
int bindGet(lua::State* state) {
    lua::gettable(state, 1);
    return 1;
}

int bindSet(lua::State* state) {
    lua::settable(state, 1);
    return 0;
}

/// Pushes t[key] of the table at `idx`, raw: a bind object's fields are read without metamethods, so a table with
/// an __index (one that raises, too) isn't taken for a bind object.
lua::Type pushRawField(lua::State* state, int idx, const char* key) {
    idx = lua::absindex(state, idx);
    lua::pushstring(state, key);
    return lua::rawget(state, idx);
}

} // namespace

Node::Node(lua::State* state, int idx) : m_state{state}, m_index{lua::absindex(state, idx)} {}

lua::State* Node::state() const { return m_state; }
int Node::index() const { return m_index; }
const void* Node::ptr() const { return lua::topointer(m_state, m_index); }

std::string Node::type() const {
    lua::getfield(m_state, m_index, "__type");
    std::string t{lua::tostring(m_state, -1)};
    lua::pop(m_state);
    return t;
}

lua::Type Node::pushRaw(const char* key) const {
    lua::pushcstring(m_state, key);
    const lua::Type t = lua::rawget(m_state, m_index);
    if (t != lua::Type::Nil || m_meta == Meta::Skip) return t;
    if (m_meta == Meta::Unknown) {
        m_meta = Meta::Skip; // no metatable, or a shared one
        if (lua::getmetatable(m_state, m_index) != 0) {
            lua::pushcstring(m_state, sharedMetaMarker); // nil, meta, marker
            if (lua::rawget(m_state, -2) == lua::Type::Nil) m_meta = Meta::Index;
            lua::pop(m_state, 2); // nil
        }
        if (m_meta == Meta::Skip) return t;
    }
    lua::pop(m_state);
    return lua::getfield(m_state, m_index, key); // the table's own metatable (a script's): its __index runs
}

lua::Type Node::push(const char* key) const {
    pushRaw(key);
    if (lua::isfunction(m_state, -1)) {
        lua::pushvalue(m_state, m_index);
        if (!safeRead(m_state, scopedArgs(m_state, m_index, 1), 1)) lua::pushnil(m_state);
    } else if (isBind(m_state, -1)) {
        resolveBind(-1);
    }
    return lua::type(m_state, -1);
}

bool Node::has(const char* key) const {
    bool r = pushRaw(key) != lua::Type::Nil;
    lua::pop(m_state);
    return r;
}

std::optional<double> Node::number(const char* key) const {
    std::optional<double> r;
    if (push(key) == lua::Type::Number || lua::isnumber(m_state, -1)) r = lua::tonumber(m_state, -1);
    lua::pop(m_state);
    return r;
}

double Node::number(const char* key, double def) const { return number(key).value_or(def); }
float Node::numf(const char* key, float def) const { return toFloat(number(key, static_cast<double>(def))); }

bool Node::boolean(const char* key, bool def) const {
    lua::Type t = push(key);
    bool r = t == lua::Type::Nil ? def : lua::toboolean(m_state, -1);
    lua::pop(m_state);
    return r;
}

std::string Node::string(const char* key, std::string_view def) const {
    lua::Type t = push(key);
    // Anything else converts like tostring(); a __tostring that raises is reported (toDisplayString).
    std::string r = t == lua::Type::Nil                                  ? std::string{def}
                    : (t == lua::Type::String || t == lua::Type::Number) ? std::string{lua::tostring(m_state, -1)}
                                                                         : toDisplayString(m_state, -1);
    lua::pop(m_state);
    return r;
}

std::optional<Color> Node::color(const char* key) const {
    std::optional<Color> r;
    lua::Type t = push(key);
    if (t == lua::Type::String)
        r = resolveColor(lua::tostring(m_state, -1));
    else if (t == lua::Type::Table)
        r = colorFromTable(m_state, -1);
    lua::pop(m_state);
    return r;
}

std::string Node::text(const char* key) const {
    if (has(key)) return string(key);
    lua::rawgeti(m_state, m_index, 1);
    lua::Type t = lua::type(m_state, -1);
    std::string r =
        t == lua::Type::String || t == lua::Type::Number ? std::string{lua::tostring(m_state, -1)} : std::string{};
    lua::pop(m_state);
    return r;
}

std::vector<std::string> Node::strings(const char* key) const {
    std::vector<std::string> out;
    if (push(key) == lua::Type::Table) {
        lua::Integer n = lua::rawlen(m_state, -1);
        for (lua::Integer i = 1; i <= n; ++i) {
            lua::rawgeti(m_state, -1, i);
            out.push_back(toDisplayString(m_state, -1)); // a __tostring that raises is reported
            lua::pop(m_state);
        }
    }
    lua::pop(m_state);
    return out;
}

std::vector<float> Node::floats(const char* key) const {
    std::vector<float> out;
    if (push(key) == lua::Type::Table) {
        lua::Integer n = lua::rawlen(m_state, -1);
        out.reserve(static_cast<std::size_t>(n));
        for (lua::Integer i = 1; i <= n; ++i) {
            lua::rawgeti(m_state, -1, i);
            out.push_back(toFloat(lua::tonumber(m_state, -1)));
            lua::pop(m_state);
        }
    }
    lua::pop(m_state);
    return out;
}

void Node::set(const char* key, const Value& v) const {
    pushValue(m_state, v);
    setTop(key);
}

void Node::setTop(const char* key) const {
    const int value = lua::gettop(m_state);
    pushRaw(key);
    if (isBind(m_state, -1)) {
        pushRawField(m_state, -1, "t");
        if (lua::toboolean(m_state, -1)) {
            // t[k] = v may run a metamethod (a ui.state proxy's __newindex runs its watchers), so it runs protected.
            lua::pushcfunction(m_state, &bindSet);
            lua::insert(m_state, -2);
            pushRawField(m_state, -3, "k");
            lua::pushvalue(m_state, value);
            safeCall(m_state, 3, 0);
        } else {
            lua::pop(m_state);
            pushRawField(m_state, -1, "set");
            if (lua::isfunction(m_state, -1)) {
                lua::pushvalue(m_state, value);
                safeCall(m_state, 1, 0);
            }
        }
        lua::settop(m_state, value - 1);
        return;
    }
    const bool binding = lua::isfunction(m_state, -1);
    lua::pop(m_state);
    if (binding) {
        lua::pop(m_state); // the binding owns the value
        return;
    }
    lua::setfield(m_state, m_index, key); // pops the value
    noteChange();
}

bool Node::emit(const char* signal, std::initializer_list<Value> args) const {
    pushRaw(signal);
    if (!lua::isfunction(m_state, -1)) {
        lua::pop(m_state);
        return false;
    }
    lua::pushvalue(m_state, m_index);
    for (const auto& a : args)
        pushValue(m_state, a);
    return safeCall(m_state, scopedArgs(m_state, m_index, 1 + static_cast<int>(args.size())), 0);
}

std::optional<Color> Node::colorFromTable(lua::State* state, int idx) {
    idx = lua::absindex(state, idx);
    float c[4] = {0, 0, 0, 1};
    for (int i = 0; i < 4; ++i) {
        lua::rawgeti(state, idx, i + 1);
        const bool number = lua::isnumber(state, -1);
        if (number) c[i] = toFloat(lua::tonumber(state, -1));
        lua::pop(state);
        if (!number && i < 3) return std::nullopt; // r, g and b are required; alpha defaults to 1
    }
    return Color{c[0], c[1], c[2], c[3]};
}

void Node::resolveBind(int idx) const {
    int b = lua::absindex(m_state, idx);
    pushRawField(m_state, b, "t");
    if (lua::toboolean(m_state, -1)) {
        // t[k] may run an __index metamethod, so it runs protected (like Lua's resolve, any truthy t is a source).
        lua::pushcfunction(m_state, &bindGet);
        lua::insert(m_state, -2);
        pushRawField(m_state, b, "k");
        if (!safeRead(m_state, 2, 1)) lua::pushnil(m_state);
    } else {
        lua::pop(m_state);
        pushRawField(m_state, b, "get");
        if (!lua::isfunction(m_state, -1) || !safeRead(m_state, 0, 1)) {
            if (lua::gettop(m_state) > b) lua::settop(m_state, b);
            lua::pushnil(m_state);
        }
    }
    lua::remove(m_state, b);
}

} // namespace imlui::luaui
