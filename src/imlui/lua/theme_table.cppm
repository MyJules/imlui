// Conversion between Lua theme tables and imlui::Theme / Scheme.
export module imlui.lua.theme_table;

import std;

import lua;
import lua.stack_guard;
import imlui.color;
import imlui.theme.font_library;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.resources.name;

export namespace imlui::luaui {

/// Builds a Theme from a Lua table (see docs in src/resources/imlui.lua `ui.theme`). Relative font names resolve
/// against the folder of `script` (a full resource name, the script that declared the theme; see readFonts()).
Theme themeFromTable(lua::State* state, int idx, std::string_view script = {});

/// Reads the `fonts` field of the theme table at `idx` into `fonts` (names only; nothing is loaded), reporting
/// malformed entries. A relative name (`fonts/Inter.ttf`) is resolved against the folder of `script`; full names are
/// normalized. A name that doesn't resolve (or any relative name without `script`) is kept as given, so loading it
/// reports it as invalid.
void readFonts(lua::State* state, int idx, FontSet& fonts, std::string_view script = {});

/// Pushes { primary = "#RRGGBB", ... } for `s`.
void pushScheme(lua::State* state, const Scheme& s);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

Theme themeFromTable(lua::State* state, int idx, std::string_view script) {
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
    // Strings may be hex, a role name or "role/alpha"; roles resolve against `scheme`.
    auto colorAt = [&](int i, const Scheme& scheme) -> std::optional<Color> {
        if (lua::type(state, i) == lua::Type::String) return resolveColor(lua::tostring(state, i), scheme);
        if (lua::type(state, i) == lua::Type::Table) return Node::colorFromTable(state, i);
        return std::nullopt;
    };

    if (auto p = str("preset")) t.preset = *p;
    // The seed has no scheme of its own yet, so its roles come from the current theme.
    if (lua::getfield(state, idx, "seed") != lua::Type::Nil) {
        if (auto c = colorAt(lua::gettop(state), theme().scheme)) t.seed = *c;
        else reportError("theme: invalid seed color '" + toDisplayString(state, -1) + "'");
    }
    lua::pop(state);
    if (lua::getfield(state, idx, "dark") != lua::Type::Nil) t.dark = lua::toboolean(state, -1);
    lua::pop(state);
    num(idx, "spacing", t.spacing);
    num(idx, "fontScale", t.fontScale);
    t.scheme = generateScheme(t.seed, t.dark);
    // `colors` / `imgui` role names refer to this table's generated scheme, before overrides.
    const Scheme generated = t.scheme;

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
            // lua::tostring would convert a number key in place, which breaks lua::next.
            if (lua::type(state, -2) != lua::Type::String) {
                reportError("theme.colors: keys must be role names");
                lua::pop(state);
                continue;
            }
            std::string role{lua::tostring(state, -2)};
            Color* target = findRole(t.scheme, role);
            auto c = colorAt(lua::gettop(state), generated);
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
            // lua::tostring would convert a number key in place, which breaks lua::next.
            if (lua::type(state, -2) != lua::Type::String) {
                reportError("theme.imgui: keys must be ImGuiCol names");
                lua::pop(state);
                continue;
            }
            std::string name{lua::tostring(state, -2)};
            int ci = imguiColorIndex(name);
            auto c = colorAt(lua::gettop(state), generated);
            if (ci < 0) reportError("theme.imgui: unknown ImGuiCol '" + name + "'");
            else if (!c) reportError("theme.imgui." + name + ": invalid color");
            else t.imguiColors.emplace_back(ci, *c);
            lua::pop(state);
        }
    }
    lua::pop(state);

    // Raw ImGuiStyle overrides: style = { FrameRounding = 6, ItemSpacing = {8, 4} }
    if (lua::getfield(state, idx, "style") == lua::Type::Table) {
        int ct = lua::gettop(state);
        lua::pushnil(state);
        while (lua::next(state, ct)) {
            // lua::tostring would convert a number key in place, which breaks lua::next.
            if (lua::type(state, -2) != lua::Type::String) {
                reportError("theme.style: keys must be ImGuiStyle field names");
                lua::pop(state);
                continue;
            }
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

    readFonts(state, idx, t.fonts, script);
    return t;
}

void readFonts(lua::State* state, int idx, FontSet& fonts, std::string_view script) {
    idx = lua::absindex(state, idx);
    lua::StackGuard guard{state};
    const auto name = [&](int at) -> std::string {
        const std::string_view given = lua::tostring(state, at);
        if (const auto full = script.empty() ? normalizeName(given) : resolveName(script, given)) return *full;
        return std::string{given};
    };
    // fonts = { regular = "app:/...", medium = ..., fallback = { ... }, mono = "app:/..." }. Only the names are
    // read here; native.setTheme loads the fonts (loadFontSet()).
    const lua::Type type = lua::getfield(state, idx, "fonts");
    if (type == lua::Type::Nil) return;
    if (type != lua::Type::Table) {
        reportError("theme.fonts: expected a table");
        return;
    }
    const int ft = lua::gettop(state);
    lua::pushnil(state);
    while (lua::next(state, ft)) {
        if (lua::type(state, -2) != lua::Type::String) {
            reportError("theme.fonts: keys must be font names");
            lua::pop(state);
            continue;
        }
        std::string key{lua::tostring(state, -2)};
        const bool isName = lua::type(state, -1) == lua::Type::String;
        if (key == "fallback") {
            if (isName) {
                fonts.fallback.push_back(name(-1));
            } else if (lua::istable(state, -1)) {
                const lua::Integer n = lua::rawlen(state, -1);
                for (lua::Integer i = 1; i <= n; ++i) {
                    if (lua::rawgeti(state, -1, i) == lua::Type::String)
                        fonts.fallback.push_back(name(-1));
                    else
                        reportError("theme.fonts.fallback[" + std::to_string(i) + "]: expected a resource name");
                    lua::pop(state);
                }
            } else {
                reportError("theme.fonts.fallback: expected a resource name or a list of them");
            }
        } else if (!isName) {
            reportError("theme.fonts." + key + ": expected a resource name such as \"app:/fonts/Inter.ttf\"");
        } else if (key == "regular") {
            fonts.regular = name(-1);
        } else if (key == "medium") {
            fonts.medium = name(-1);
        } else {
            fonts.named.insert_or_assign(std::move(key), name(-1));
        }
        lua::pop(state);
    }
}

void pushScheme(lua::State* state, const Scheme& s) {
    lua::createtable(state, 0, static_cast<int>(roles().size()));
    for (const auto& r : roles()) {
        lua::pushstring(state, toHex(s.*r.member));
        lua::setfield(state, -2, std::string{r.name}.c_str());
    }
}

} // namespace imlui::luaui
