// Conversion between Lua theme tables and imlui::Theme / Scheme.
module;

#include <optional>
#include <string>

export module imlui.lua.theme_table;

import lua;
import lua.stack_guard;
import imlui.color;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.lua.errors;
import imlui.lua.node;

export namespace imlui::luaui {

/// Builds a Theme from a Lua table (see docs in lua/imlui.lua `ui.theme`).
Theme themeFromTable(lua::State* state, int idx);

/// Pushes { primary = "#RRGGBB", ... } for `s`.
void pushScheme(lua::State* state, const Scheme& s);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

Theme themeFromTable(lua::State* state, int idx) {
    idx = lua::absindex(state, idx);
    lua::StackGuard guard{state};
    Theme t;
    auto str = [&](const char* k) -> std::optional<std::string> {
        std::optional<std::string> r;
        if (lua::getfield(state, idx, k) == lua::Type::String) r = std::string{lua::tostring(state, -1)};
        lua::pop(state);
        return r;
    };
    auto num = [&](int tbl, const char* k, float& out) {
        if (lua::getfield(state, tbl, k) == lua::Type::Number) out = static_cast<float>(lua::tonumber(state, -1));
        lua::pop(state);
    };
    auto colorAt = [&](int i) -> std::optional<Color> {
        if (lua::type(state, i) == lua::Type::String) return parseHex(lua::tostring(state, i));
        if (lua::type(state, i) == lua::Type::Table) return Node::colorFromTable(state, i);
        return std::nullopt;
    };

    if (auto p = str("preset")) t.preset = *p;
    if (auto s = str("seed")) {
        if (auto c = parseHex(*s)) t.seed = *c;
        else reportError("theme: invalid seed color '" + *s + "'");
    }
    if (lua::getfield(state, idx, "dark") != lua::Type::Nil) t.dark = lua::toboolean(state, -1);
    lua::pop(state);
    num(idx, "spacing", t.spacing);
    num(idx, "fontScale", t.fontScale);
    t.scheme = generateScheme(t.seed, t.dark);

    if (lua::getfield(state, idx, "shape") == lua::Type::Table) {
        int s = lua::gettop(state);
        num(s, "extraSmall", t.shape.extraSmall);
        num(s, "small", t.shape.small);
        num(s, "medium", t.shape.medium);
        num(s, "large", t.shape.large);
        num(s, "extraLarge", t.shape.extraLarge);
    }
    lua::pop(state);

    // Role overrides: colors = { primary = "#...", surface = "#..." }
    if (lua::getfield(state, idx, "colors") == lua::Type::Table) {
        int ct = lua::gettop(state);
        lua::pushnil(state);
        while (lua::next(state, ct)) {
            std::string role{lua::tostring(state, -2)};
            Color* target = findRole(t.scheme, role);
            auto c = colorAt(lua::gettop(state));
            if (!target) reportError("theme.colors: unknown role '" + role + "'");
            else if (!c) reportError("theme.colors." + role + ": invalid color");
            else *target = *c;
            lua::pop(state);
        }
    }
    lua::pop(state);

    // Raw ImGui color overrides: imgui = { WindowBg = "#..." }
    if (lua::getfield(state, idx, "imgui") == lua::Type::Table) {
        int ct = lua::gettop(state);
        lua::pushnil(state);
        while (lua::next(state, ct)) {
            std::string name{lua::tostring(state, -2)};
            int ci = imguiColorIndex(name);
            auto c = colorAt(lua::gettop(state));
            if (ci < 0) reportError("theme.imgui: unknown ImGuiCol '" + name + "'");
            else if (c) t.imguiColors.emplace_back(ci, *c);
            lua::pop(state);
        }
    }
    lua::pop(state);

    // Raw ImGuiStyle overrides: style = { FrameRounding = 6, ItemSpacing = {8, 4} }
    if (lua::getfield(state, idx, "style") == lua::Type::Table) {
        int ct = lua::gettop(state);
        lua::pushnil(state);
        while (lua::next(state, ct)) {
            std::string name{lua::tostring(state, -2)};
            if (!isStyleField(name)) {
                reportError("theme.style: unknown ImGuiStyle field '" + name + "'");
            } else if (lua::isnumber(state, -1)) {
                float v = static_cast<float>(lua::tonumber(state, -1));
                t.style.push_back({name, {v, v}});
            } else if (lua::istable(state, -1)) {
                lua::rawgeti(state, -1, 1);
                lua::rawgeti(state, -2, 2);
                t.style.push_back(
                    {name, {static_cast<float>(lua::tonumber(state, -2)), static_cast<float>(lua::tonumber(state, -1))}});
                lua::pop(state, 2);
            }
            lua::pop(state);
        }
    }
    lua::pop(state);
    return t;
}

void pushScheme(lua::State* state, const Scheme& s) {
    lua::createtable(state, 0, static_cast<int>(roles().size()));
    for (const auto& r : roles()) {
        lua::pushstring(state, toHex(s.*r.member));
        lua::setfield(state, -2, std::string{r.name}.c_str());
    }
}

} // namespace imlui::luaui
