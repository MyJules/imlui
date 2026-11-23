// Lua <-> UI bridge helpers.
//
// A UI element on the Lua side is a plain table: { __type = "Button",
// text = "OK", onClicked = function(self) ... end, [1..n] = children }.
// Property values may be:
//   * plain values           text = "Hello"
//   * bindings (functions)   text = function(self) return "n = " .. s.n end
//                            re-evaluated every frame, like a QML binding
//   * two-way bindings       value = ui.bind(state, "volume")
// `Node` reads/writes properties honouring those three forms and emits
// signals (on* handlers) safely.
module;

#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module imlui.luaui;

import lua;
import imlui.color;
import imlui.theme;
import imlui.log;

export namespace imlui::luaui {

// ------------------------------------------------------------- errors

namespace detail {
inline std::vector<std::string> g_errors;
}

/// Records a script error (deduplicated, capped) and prints it once.
void reportError(std::string msg) {
    for (const auto& e : detail::g_errors)
        if (e == msg) return;
    log::write(log::Level::Error, msg, "lua");
    if (detail::g_errors.size() >= 32) detail::g_errors.erase(detail::g_errors.begin());
    detail::g_errors.push_back(std::move(msg));
}
const std::vector<std::string>& errors() { return detail::g_errors; }
void clearErrors() { detail::g_errors.clear(); }

int tracebackHandler(lua::State* L) {
    std::string msg = lua::isstring(L, 1) ? std::string{lua::tostring(L, 1)} : lua::tostring_any(L, 1);
    lua::traceback(L, L, msg.c_str(), 1);
    return 1;
}

/// Calls the function below `nargs` arguments at the top of the stack in
/// protected mode. On success leaves `nresults` values; on failure reports
/// the error (with traceback) and leaves nothing.
bool safeCall(lua::State* L, int nargs, int nresults) {
    int base = lua::gettop(L) - nargs;
    lua::pushcfunction(L, &tracebackHandler);
    lua::insert(L, base);
    int rc = lua::pcall(L, nargs, nresults, base);
    lua::remove(L, base);
    if (rc != lua::OK) {
        reportError(std::string{lua::tostring(L, -1)});
        lua::pop(L);
        return false;
    }
    return true;
}

// ------------------------------------------------------------- values

using Value = std::variant<std::monostate, bool, double, std::string>;

void pushValue(lua::State* L, const Value& v) {
    if (auto* b = std::get_if<bool>(&v)) lua::pushboolean(L, *b);
    else if (auto* d = std::get_if<double>(&v)) {
        double i = static_cast<double>(static_cast<long long>(*d));
        if (i == *d) lua::pushinteger(L, static_cast<lua::Integer>(*d));
        else lua::pushnumber(L, *d);
    } else if (auto* s = std::get_if<std::string>(&v)) lua::pushstring(L, *s);
    else lua::pushnil(L);
}

/// Is the value at `idx` a binding object created by ui.bind()?
bool isBind(lua::State* L, int idx) {
    if (!lua::istable(L, idx)) return false;
    lua::getfield(L, idx, "__bind");
    bool b = lua::toboolean(L, -1);
    lua::pop(L);
    return b;
}

// ------------------------------------------------------------- Node

class Node {
public:
    Node(lua::State* L, int idx) : L_{L}, idx_{lua::absindex(L, idx)} {}

    [[nodiscard]] lua::State* state() const { return L_; }
    [[nodiscard]] int index() const { return idx_; }
    [[nodiscard]] const void* ptr() const { return lua::topointer(L_, idx_); }

    [[nodiscard]] std::string type() const {
        lua::getfield(L_, idx_, "__type");
        std::string t{lua::tostring(L_, -1)};
        lua::pop(L_);
        return t;
    }

    /// Pushes the raw (unresolved) field.
    int pushRaw(const char* key) const { return lua::getfield(L_, idx_, key); }

    /// Pushes the resolved value of property `key`; returns its Lua type.
    int push(const char* key) const {
        lua::getfield(L_, idx_, key);
        if (lua::isfunction(L_, -1)) {
            lua::pushvalue(L_, idx_);
            if (!safeCall(L_, 1, 1)) { lua::pushnil(L_); }
        } else if (isBind(L_, -1)) {
            resolveBind(-1);
        }
        return lua::type(L_, -1);
    }

    [[nodiscard]] bool has(const char* key) const {
        bool r = pushRaw(key) != lua::TNIL;
        lua::pop(L_);
        return r;
    }

    [[nodiscard]] std::optional<double> number(const char* key) const {
        std::optional<double> r;
        if (push(key) == lua::TNUMBER || lua::isnumber(L_, -1)) r = lua::tonumber(L_, -1);
        lua::pop(L_);
        return r;
    }
    [[nodiscard]] double number(const char* key, double def) const { return number(key).value_or(def); }
    [[nodiscard]] float numf(const char* key, float def) const { return static_cast<float>(number(key, def)); }

    [[nodiscard]] bool boolean(const char* key, bool def) const {
        int t = push(key);
        bool r = t == lua::TNIL ? def : lua::toboolean(L_, -1);
        lua::pop(L_);
        return r;
    }

    [[nodiscard]] std::string string(const char* key, std::string_view def = {}) const {
        int t = push(key);
        std::string r = t == lua::TNIL ? std::string{def}
                      : (t == lua::TSTRING || t == lua::TNUMBER) ? std::string{lua::tostring(L_, -1)}
                      : lua::tostring_any(L_, -1);
        lua::pop(L_);
        return r;
    }

    [[nodiscard]] std::optional<Color> color(const char* key) const {
        std::optional<Color> r;
        int t = push(key);
        if (t == lua::TSTRING) r = resolveColor(lua::tostring(L_, -1));
        else if (t == lua::TTABLE) r = colorFromTable(L_, -1);
        lua::pop(L_);
        return r;
    }

    /// The element's main text: `text` property, or the first positional string.
    [[nodiscard]] std::string text(const char* key = "text") const {
        if (has(key)) return string(key);
        lua::rawgeti(L_, idx_, 1);
        std::string r = lua::type(L_, -1) == lua::TSTRING || lua::type(L_, -1) == lua::TNUMBER
                            ? std::string{lua::tostring(L_, -1)} : std::string{};
        lua::pop(L_);
        return r;
    }

    /// Reads a list model of strings (or values convertible to string).
    [[nodiscard]] std::vector<std::string> strings(const char* key) const {
        std::vector<std::string> out;
        if (push(key) == lua::TTABLE) {
            lua::Integer n = lua::rawlen(L_, -1);
            for (lua::Integer i = 1; i <= n; ++i) {
                lua::rawgeti(L_, -1, i);
                out.push_back(lua::tostring_any(L_, -1));
                lua::pop(L_);
            }
        }
        lua::pop(L_);
        return out;
    }

    [[nodiscard]] std::vector<float> floats(const char* key) const {
        std::vector<float> out;
        if (push(key) == lua::TTABLE) {
            lua::Integer n = lua::rawlen(L_, -1);
            out.reserve(static_cast<std::size_t>(n));
            for (lua::Integer i = 1; i <= n; ++i) {
                lua::rawgeti(L_, -1, i);
                out.push_back(static_cast<float>(lua::tonumber(L_, -1)));
                lua::pop(L_);
            }
        }
        lua::pop(L_);
        return out;
    }

    /// Writes a property, honouring bindings:
    ///  * two-way bind -> writes the bound source
    ///  * function binding -> left alone (the source owns the value)
    ///  * otherwise -> stored on the element itself
    void set(const char* key, const Value& v) const {
        pushRaw(key);
        if (isBind(L_, -1)) {
            lua::getfield(L_, -1, "t");
            if (lua::istable(L_, -1)) {
                lua::getfield(L_, -2, "k");
                pushValue(L_, v);
                lua::settable(L_, -3);
                lua::pop(L_, 2);
                return;
            }
            lua::pop(L_);
            lua::getfield(L_, -1, "set");
            if (lua::isfunction(L_, -1)) {
                pushValue(L_, v);
                safeCall(L_, 1, 0);
            } else {
                lua::pop(L_);
            }
            lua::pop(L_);
            return;
        }
        bool binding = lua::isfunction(L_, -1);
        lua::pop(L_);
        if (binding) return;
        pushValue(L_, v);
        lua::setfield(L_, idx_, key);
    }

    /// Calls handler `signal` (e.g. "onClicked") as handler(self, args...).
    bool emit(const char* signal, std::initializer_list<Value> args = {}) const {
        lua::getfield(L_, idx_, signal);
        if (!lua::isfunction(L_, -1)) { lua::pop(L_); return false; }
        lua::pushvalue(L_, idx_);
        for (const auto& a : args) pushValue(L_, a);
        return safeCall(L_, 1 + static_cast<int>(args.size()), 0);
    }

    static std::optional<Color> colorFromTable(lua::State* L, int idx) {
        idx = lua::absindex(L, idx);
        float c[4] = {0, 0, 0, 1};
        for (int i = 0; i < 4; ++i) {
            lua::rawgeti(L, idx, i + 1);
            if (lua::isnumber(L, -1)) c[i] = static_cast<float>(lua::tonumber(L, -1));
            lua::pop(L);
        }
        return Color{c[0], c[1], c[2], c[3]};
    }

private:
    /// Replaces the bind object at `idx` (relative) with the bound value.
    void resolveBind(int idx) const {
        int b = lua::absindex(L_, idx);
        lua::getfield(L_, b, "t");
        if (lua::istable(L_, -1)) {
            lua::getfield(L_, b, "k");
            lua::gettable(L_, -2);
            lua::remove(L_, -2);
        } else {
            lua::pop(L_);
            lua::getfield(L_, b, "get");
            if (!lua::isfunction(L_, -1) || !safeCall(L_, 0, 1)) {
                if (lua::gettop(L_) > b) lua::settop(L_, b);
                lua::pushnil(L_);
            }
        }
        lua::remove(L_, b);
    }

    lua::State* L_;
    int idx_;
};

// ------------------------------------------------------------- theme <-> lua

/// Builds a Theme from a Lua table (see docs in lua/imlui.lua `ui.theme`).
Theme themeFromTable(lua::State* L, int idx) {
    idx = lua::absindex(L, idx);
    lua::StackGuard guard{L};
    Theme t;
    auto str = [&](const char* k) -> std::optional<std::string> {
        std::optional<std::string> r;
        if (lua::getfield(L, idx, k) == lua::TSTRING) r = std::string{lua::tostring(L, -1)};
        lua::pop(L);
        return r;
    };
    auto num = [&](int tbl, const char* k, float& out) {
        if (lua::getfield(L, tbl, k) == lua::TNUMBER) out = static_cast<float>(lua::tonumber(L, -1));
        lua::pop(L);
    };
    auto colorAt = [&](int i) -> std::optional<Color> {
        if (lua::type(L, i) == lua::TSTRING) return parseHex(lua::tostring(L, i));
        if (lua::type(L, i) == lua::TTABLE) return Node::colorFromTable(L, i);
        return std::nullopt;
    };

    if (auto p = str("preset")) t.preset = *p;
    if (auto s = str("seed")) {
        if (auto c = parseHex(*s)) t.seed = *c;
        else reportError("theme: invalid seed color '" + *s + "'");
    }
    if (lua::getfield(L, idx, "dark") != lua::TNIL) t.dark = lua::toboolean(L, -1);
    lua::pop(L);
    num(idx, "spacing", t.spacing);
    num(idx, "fontScale", t.fontScale);
    t.scheme = generateScheme(t.seed, t.dark);

    if (lua::getfield(L, idx, "shape") == lua::TTABLE) {
        int s = lua::gettop(L);
        num(s, "extraSmall", t.shape.extraSmall);
        num(s, "small", t.shape.small);
        num(s, "medium", t.shape.medium);
        num(s, "large", t.shape.large);
        num(s, "extraLarge", t.shape.extraLarge);
    }
    lua::pop(L);

    // Role overrides: colors = { primary = "#...", surface = "#..." }
    if (lua::getfield(L, idx, "colors") == lua::TTABLE) {
        int ct = lua::gettop(L);
        lua::pushnil(L);
        while (lua::next(L, ct)) {
            std::string role{lua::tostring(L, -2)};
            Color* target = findRole(t.scheme, role);
            auto c = colorAt(lua::gettop(L));
            if (!target) reportError("theme.colors: unknown role '" + role + "'");
            else if (!c) reportError("theme.colors." + role + ": invalid color");
            else *target = *c;
            lua::pop(L);
        }
    }
    lua::pop(L);

    // Raw ImGui color overrides: imgui = { WindowBg = "#..." }
    if (lua::getfield(L, idx, "imgui") == lua::TTABLE) {
        int ct = lua::gettop(L);
        lua::pushnil(L);
        while (lua::next(L, ct)) {
            std::string name{lua::tostring(L, -2)};
            int ci = imguiColorIndex(name);
            auto c = colorAt(lua::gettop(L));
            if (ci < 0) reportError("theme.imgui: unknown ImGuiCol '" + name + "'");
            else if (c) t.imguiColors.emplace_back(ci, *c);
            lua::pop(L);
        }
    }
    lua::pop(L);

    // Raw ImGuiStyle overrides: style = { FrameRounding = 6, ItemSpacing = {8, 4} }
    if (lua::getfield(L, idx, "style") == lua::TTABLE) {
        int ct = lua::gettop(L);
        lua::pushnil(L);
        while (lua::next(L, ct)) {
            std::string name{lua::tostring(L, -2)};
            if (!isStyleField(name)) {
                reportError("theme.style: unknown ImGuiStyle field '" + name + "'");
            } else if (lua::isnumber(L, -1)) {
                float v = static_cast<float>(lua::tonumber(L, -1));
                t.style.push_back({name, {v, v}});
            } else if (lua::istable(L, -1)) {
                lua::rawgeti(L, -1, 1);
                lua::rawgeti(L, -2, 2);
                t.style.push_back({name, {static_cast<float>(lua::tonumber(L, -2)), static_cast<float>(lua::tonumber(L, -1))}});
                lua::pop(L, 2);
            }
            lua::pop(L);
        }
    }
    lua::pop(L);
    return t;
}

/// Pushes { primary = "#RRGGBB", ... } for the current scheme.
void pushScheme(lua::State* L, const Scheme& s) {
    lua::createtable(L, 0, static_cast<int>(kRoles.size()));
    for (const auto& r : kRoles) {
        lua::pushstring(L, toHex(s.*r.member));
        lua::setfield(L, -2, std::string{r.name}.c_str());
    }
}

} // namespace imlui::luaui
