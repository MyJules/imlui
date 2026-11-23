// The painter object handed to Canvas.onPaint: immediate drawing into the
// canvas' ImGui draw list, in dp relative to the canvas origin.
module;

#include <string>
#include <string_view>
#include <vector>

export module imlui.lua.painter;

import lua;
import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.theme.theme;
import imlui.lua.node;

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

Color colorArg(lua::State* state, int idx, Color def) {
    if (lua::type(state, idx) == lua::Type::String)
        if (auto c = resolveColor(lua::tostring(state, idx))) return *c;
    if (lua::istable(state, idx))
        if (auto c = Node::colorFromTable(state, idx)) return *c;
    return def;
}

ImU32 u32(Color c) { return ImGui::ColorConvertFloat4ToU32(c.vec4()); }

float num(lua::State* state, int idx, float def = 0.0f) {
    return lua::isnumber(state, idx) ? static_cast<float>(lua::tonumber(state, idx)) : def;
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
    target->AddLine(pt(state, 2), pt(state, 4), u32(colorArg(state, 6, theme().scheme.onSurface)),
                    dp(num(state, 7, 1.0f)));
    return 0;
}

int rect(lua::State* state) {
    ready(state);
    ImVec2 a = pt(state, 2);
    ImVec2 b{a.x + dp(num(state, 4)), a.y + dp(num(state, 5))};
    ImU32 c = u32(colorArg(state, 6, theme().scheme.primary));
    float r = dp(num(state, 7));
    if (lua::toboolean(state, 8))
        target->AddRect(a, b, c, r, dp(num(state, 9, 1.0f)));
    else
        target->AddRectFilled(a, b, c, r);
    return 0;
}

int circle(lua::State* state) {
    ready(state);
    ImVec2 c = pt(state, 2);
    float r = dp(num(state, 4));
    ImU32 col = u32(colorArg(state, 5, theme().scheme.primary));
    if (lua::toboolean(state, 6))
        target->AddCircle(c, r, col, 0, dp(num(state, 7, 1.0f)));
    else
        target->AddCircleFilled(c, r, col);
    return 0;
}

int text(lua::State* state) {
    ready(state);
    ImVec2 p = pt(state, 2);
    std::string_view s = lua::tostring(state, 4);
    Color c = colorArg(state, 5, theme().scheme.onSurface);
    TextStyle st = TextStyle::BodyMedium;
    if (lua::type(state, 6) == lua::Type::String)
        if (auto t = textStyleFromName(lua::tostring(state, 6))) st = *t;
    target->AddText(font(st), fontPixels(st), p, u32(c), s.data(), s.data() + s.size());
    return 0;
}

/// p:icon(cx, cy, name, size = 24, color) — centered at (cx, cy)
int icon(lua::State* state) {
    ready(state);
    ImVec2 c = pt(state, 2);
    std::string_view name = lua::tostring(state, 4);
    float size = num(state, 5, 24.0f);
    Color col = colorArg(state, 6, theme().scheme.onSurfaceVariant);
    ImFont* f = iconFont();
    std::string t = iconText(name);
    if (!f || t.empty()) return 0;
    float px = dp(size);
    ImVec2 sz = f->CalcTextSizeA(px, 3.4e38f, 0.0f, t.c_str());
    target->AddText(f, px, {c.x - sz.x * 0.5f, c.y - sz.y * 0.5f}, u32(col), t.c_str());
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

} // namespace

void registerPainter(lua::State* state) {
    lua::createtable(state, 0, 2); // metatable
    lua::createtable(state, 0, 6); // methods
    lua::pushcfunction(state, &line);
    lua::setfield(state, -2, "line");
    lua::pushcfunction(state, &rect);
    lua::setfield(state, -2, "rect");
    lua::pushcfunction(state, &circle);
    lua::setfield(state, -2, "circle");
    lua::pushcfunction(state, &text);
    lua::setfield(state, -2, "text");
    lua::pushcfunction(state, &icon);
    lua::setfield(state, -2, "icon");
    lua::pushcfunction(state, &polyline);
    lua::setfield(state, -2, "polyline");
    lua::setfield(state, -2, "__index");
    lua::setfield(state, lua::registryIndex, "imlui.painter");
}

void beginPaint(ImDrawList* drawList, ImVec2 origin) {
    target = drawList;
    paintOrigin = origin;
}

void endPaint() { target = nullptr; }

} // namespace imlui::luaui
