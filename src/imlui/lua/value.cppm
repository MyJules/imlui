// Plain values exchanged with Lua, and detection of ui.bind() objects.
module;

#include <string>
#include <variant>

export module imlui.lua.value;

import lua;

export namespace imlui::luaui {

using Value = std::variant<std::monostate, bool, double, std::string>;

/// Pushes `v` (nil, boolean, integer when integral, number or string).
void pushValue(lua::State* state, const Value& v);

/// Is the value at `idx` a binding object created by ui.bind()?
[[nodiscard]] bool isBind(lua::State* state, int idx);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

void pushValue(lua::State* state, const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) {
        lua::pushboolean(state, *b);
    } else if (const auto* d = std::get_if<double>(&v)) {
        double i = static_cast<double>(static_cast<long long>(*d));
        if (i == *d) lua::pushinteger(state, static_cast<lua::Integer>(*d));
        else lua::pushnumber(state, *d);
    } else if (const auto* s = std::get_if<std::string>(&v)) {
        lua::pushstring(state, *s);
    } else {
        lua::pushnil(state);
    }
}

bool isBind(lua::State* state, int idx) {
    if (!lua::istable(state, idx)) return false;
    lua::getfield(state, idx, "__bind");
    bool b = lua::toboolean(state, -1);
    lua::pop(state);
    return b;
}

} // namespace imlui::luaui
