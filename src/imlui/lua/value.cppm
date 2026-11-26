// Plain values exchanged with Lua, and detection of ui.bind() objects.
export module imlui.lua.value;

import std;

import lua;

export namespace imlui::luaui {

using Value = std::variant<std::monostate, bool, double, std::string>;

/// Pushes `v` (nil, boolean, integer when integral, number or string).
void pushValue(lua::State* state, const Value& v);

/// Is the value at `idx` a binding object created by ui.bind()? Read raw: a table's __index doesn't run.
[[nodiscard]] bool isBind(lua::State* state, int idx);

/// A Lua number narrowed to float, safely: NaN reads as 0 (like an integer property), a finite value beyond float's
/// range clamps to its largest finite value (casting it is undefined behaviour), and an infinity stays one.
[[nodiscard]] float toFloat(double v);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

void pushValue(lua::State* state, const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) {
        lua::pushboolean(state, *b);
    } else if (const auto* d = std::get_if<double>(&v)) {
        // Whole numbers push as integers. The range check comes first: casting NaN, an infinity or a value
        // outside lua::Integer's range is undefined behaviour (same check as LuaConvert<floating>::push).
        constexpr auto limit = static_cast<lua::Number>(std::numeric_limits<lua::Integer>::max());
        if (*d >= -limit && *d < limit && static_cast<lua::Number>(static_cast<lua::Integer>(*d)) == *d)
            lua::pushinteger(state, static_cast<lua::Integer>(*d));
        else
            lua::pushnumber(state, *d);
    } else if (const auto* s = std::get_if<std::string>(&v)) {
        lua::pushstring(state, *s);
    } else {
        lua::pushnil(state);
    }
}

float toFloat(double v) {
    if (std::isnan(v)) return 0.0f;
    if (std::isinf(v)) return v > 0 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();
    constexpr auto max = static_cast<double>(std::numeric_limits<float>::max());
    return static_cast<float>(std::clamp(v, -max, max));
}

bool isBind(lua::State* state, int idx) {
    if (!lua::istable(state, idx)) return false;
    idx = lua::absindex(state, idx);
    static const lua::Key bindKey{"__bind"}; // read for every table-valued prop, every frame
    lua::rawget(state, idx, bindKey);
    bool b = lua::toboolean(state, -1);
    lua::pop(state);
    return b;
}

} // namespace imlui::luaui
