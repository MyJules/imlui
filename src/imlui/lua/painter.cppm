// The painter object handed to Canvas.onPaint: immediate drawing into the
// canvas' ImGui draw list, in dp relative to the canvas origin. Lines, rects and
// circles use the Material shape helpers (imlui.widgets.common): true radii, smooth
// arcs and pixel-snapped strokes.
export module imlui.lua.painter;

import std;

import lua;
import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.font_face_scope;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.value;
import imlui.types.lua_convert;
import imlui.ui.enum_names;
import imlui.widgets.common;

export namespace imlui::luaui {

/// Registers the painter metatable (registry "imlui.painter"). Call once per state.
void registerPainter(lua::State* state);

/// Directs painter calls to `drawList`, with (0, 0) at `origin`, until endPaint().
void beginPaint(ImDrawList* drawList, ImVec2 origin);
void endPaint();

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

ImDrawList* target = nullptr;
ImVec2 paintOrigin{};

/// A color spec's text as a key that unordered_map looks up by string_view (no std::string made for a lookup).
struct SpecHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

/// Role specs ("primary", "onSurface/0.6") resolved under the theme of `generation` (themeGeneration()): a painter
/// call looks its color up by the spec's text instead of searching the scheme's roles on every primitive. Emptied
/// when the theme changes, and when it holds maxCachedSpecs (specs a script makes on the fly, "primary/" .. alpha).
/// Hex specs ("#rrggbb") aren't kept: they parse as fast as a lookup.
struct SpecCache {
    std::uint64_t generation = 0;
    std::unordered_map<std::string, std::optional<Color>, SpecHash, std::equal_to<>> colors;
};
SpecCache specCache; // main thread only, like all painting
constexpr std::size_t maxCachedSpecs = 256;

std::optional<Color> specColor(std::string_view spec) {
    if (spec.empty() || spec.front() == '#') return resolveColor(spec);
    if (specCache.generation != themeGeneration()) {
        specCache.colors.clear();
        specCache.generation = themeGeneration();
    }
    if (const auto it = specCache.colors.find(spec); it != specCache.colors.end()) return it->second;
    if (specCache.colors.size() >= maxCachedSpecs) specCache.colors.clear();
    const std::optional<Color> c = resolveColor(spec);
    specCache.colors.emplace(spec, c);
    return c;
}

Color colorArg(lua::State* state, int idx, Color def) {
    if (lua::type(state, idx) == lua::Type::String)
        if (auto c = specColor(lua::tostring(state, idx))) return *c;
    if (lua::istable(state, idx))
        if (auto c = Node::colorFromTable(state, idx)) return *c;
    return def;
}

ImU32 u32(Color c) { return ImGui::ColorConvertFloat4ToU32(c.vec4()); }

float num(lua::State* state, int idx, float def = 0.0f) {
    return lua::isnumber(state, idx) ? luaui::toFloat(lua::tonumber(state, idx)) : def;
}

ImVec2 pt(lua::State* state, int idx) {
    return {paintOrigin.x + dp(num(state, idx)), paintOrigin.y + dp(num(state, idx + 1))};
}

void ready(lua::State* state) {
    if (!target) lua::error(state, "painter used outside of Canvas.onPaint");
}

// All methods are called as p:method(...), so argument 1 is the painter.
int line(lua::State* state) {
    ready(state);
    const ImVec2 a = pt(state, 2), b = pt(state, 4);
    const Color c = colorArg(state, 6, theme().scheme.onSurface);
    const float thickness = dp(num(state, 7, 1.0f));
    if (a.y != b.y && a.x != b.x) {
        target->AddLine(a, b, u32(c), thickness);
        return 0;
    }
    // Axis-aligned: a pixel-snapped band. ImGui's AddLine centers its stroke on (x + 0.5, y + 0.5), the
    // middle of the pixel at (x, y); the band is centered there too, `px` whole pixels wide.
    const float px = std::max(1.0f, std::floor(thickness + 0.5f));
    const float offset = 0.5f - px * 0.5f;
    if (a.y == b.y)
        md::hLine(target, std::min(a.x, b.x), std::max(a.x, b.x), a.y + offset, c, px);
    else
        md::vLine(target, a.x + offset, std::min(a.y, b.y), std::max(a.y, b.y), c, px);
    return 0;
}

int rect(lua::State* state) {
    ready(state);
    const ImVec2 a = pt(state, 2);
    const ImVec2 b{a.x + dp(num(state, 4)), a.y + dp(num(state, 5))};
    const Color c = colorArg(state, 6, theme().scheme.primary);
    const float r = dp(num(state, 7));
    if (lua::toboolean(state, 8))
        md::strokeRoundRect(target, a, b, r, c, dp(num(state, 9, 1.0f)));
    else
        md::fillRoundRect(target, a, b, r, c);
    return 0;
}

int circle(lua::State* state) {
    ready(state);
    const ImVec2 c = pt(state, 2);
    const float r = dp(num(state, 4));
    const Color col = colorArg(state, 5, theme().scheme.primary);
    if (lua::toboolean(state, 6))
        md::strokeCircle(target, c, r, col, dp(num(state, 7, 1.0f)));
    else
        md::fillCircle(target, c, r, col);
    return 0;
}

int text(lua::State* state) {
    ready(state);
    ImVec2 p = pt(state, 2);
    std::string_view s = lua::tostring(state, 4);
    Color c = colorArg(state, 5, theme().scheme.onSurface);
    TextStyle st = TextStyle::BodyMedium;
    if (!lua::isnoneornil(state, 6)) {
        if (!LuaConvert<TextStyle>::check(state, 6))
            lua::error(state, "painter:text: style: " + LuaConvert<TextStyle>::mismatch(state, 6));
        st = LuaConvert<TextStyle>::get(state, 6);
    }
    const FontFaceScope face{lua::type(state, 7) == lua::Type::String ? lua::tostring(state, 7) : ""};
    target->AddText(font(st), fontPixels(st), p, u32(c), s.data(), s.data() + s.size());
    return 0;
}

/// p:icon(cx, cy, name, size = 24, color) — centered at (cx, cy). md::drawIcon: nothing below a pixel, at most
/// md::maxIconPixels.
int icon(lua::State* state) {
    ready(state);
    const ImVec2 c = pt(state, 2);
    const std::string_view name = lua::type(state, 4) == lua::Type::String ? lua::tostring(state, 4) : "";
    const float size = num(state, 5, 24.0f);
    const Color col = colorArg(state, 6, theme().scheme.onSurfaceVariant);
    md::drawIcon(target, name, c, size, col);
    return 0;
}

int polyline(lua::State* state) {
    ready(state);
    std::vector<ImVec2> pts;
    if (lua::istable(state, 2)) {
        lua::Integer n = lua::rawlen(state, 2);
        for (lua::Integer i = 1; i + 1 <= n; i += 2) {
            lua::rawgeti(state, 2, i);
            lua::rawgeti(state, 2, i + 1);
            pts.push_back({paintOrigin.x + dp(num(state, -2)), paintOrigin.y + dp(num(state, -1))});
            lua::pop(state, 2);
        }
    }
    if (pts.size() >= 2)
        target->AddPolyline(pts.data(), static_cast<int>(pts.size()), u32(colorArg(state, 3, theme().scheme.primary)),
                            dp(num(state, 4, 1.0f)), lua::toboolean(state, 5) ? ImDrawFlags_Closed : ImDrawFlags_None);
    return 0;
}

struct PainterFn {
    const char* name;
    lua::CFunction fn;
};

constexpr PainterFn painterFns[] = {
    {"line", &line}, {"rect", &rect}, {"circle", &circle}, {"text", &text}, {"icon", &icon}, {"polyline", &polyline},
};

/// Upvalue 1: the index of the method in painterFns. Runs it with a C++ exception turned into a Lua error that names
/// the method ("imlui.painter.polyline: what"); a Lua error passes through.
int callPainter(lua::State* state) {
    const PainterFn& f = painterFns[static_cast<std::size_t>(lua::tointeger(state, lua::upvalueindex(1)))];
    return catchCxx(state, [&] { return f.fn(state); }, "imlui.painter.", f.name);
}

} // namespace

void registerPainter(lua::State* state) {
    lua::createtable(state, 0, 2);                                        // metatable
    lua::createtable(state, 0, static_cast<int>(std::size(painterFns))); // methods
    for (std::size_t i = 0; i < std::size(painterFns); ++i) {
        lua::pushinteger(state, static_cast<lua::Integer>(i));
        lua::pushcclosure(state, &callPainter, 1);
        lua::setfield(state, -2, painterFns[i].name);
    }
    lua::setfield(state, -2, "__index");
    lua::setfield(state, lua::registryIndex, "imlui.painter");
}

void beginPaint(ImDrawList* drawList, ImVec2 origin) {
    target = drawList;
    paintOrigin = origin;
}

void endPaint() { target = nullptr; }

} // namespace imlui::luaui
