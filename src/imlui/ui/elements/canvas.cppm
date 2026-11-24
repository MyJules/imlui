// Canvas: custom drawing from Lua, as a typed element. Every frame it calls the `onPaint`
// handler with the painter object (imlui.lua.painter, registry "imlui.painter"), which draws into the canvas
// area in dp relative to its top-left corner.
module;

#include <algorithm>

export module imlui.ui.elements.canvas;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.painter;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Custom drawing from Lua: onPaint(self, p) draws with the painter (imlui.lua.painter) on every rendered frame.
///
/// Props:   animated = false (render continuously, for time-dependent painting)
/// Raw:     width (size, default fill), height (size, default 200dp)
/// Signals: onPaint(self, p), onClicked(self, x, y)
class Canvas final : public Element {
public:
    static void declare(TypeBuilder<Canvas>& type);

    void render(Frame& frame) override;

    [[nodiscard]] bool animated() const;
    void setAnimated(bool animated);

private:
    bool m_animated = false;
};

} // namespace imlui

module :private;

namespace imlui {

void Canvas::declare(TypeBuilder<Canvas>& type) {
    type.property("animated", &Canvas::animated, &Canvas::setAnimated).signal("onClicked").signal("onPaint");
}

void Canvas::render(Frame& frame) {
    // Unset: the available width, 200dp high.
    const float w = frame.resolvedWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x));
    const float h = frame.resolvedHeight(frame.dp(200));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##canvas", {w, h});
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const ImVec2 m = ImGui::GetMousePos();
    const float s = dpiScale();
    if (clicked) emit("onClicked", static_cast<double>((m.x - p.x) / s), static_cast<double>((m.y - p.y) / s));

    if (m_animated) frame.requestFrame();

    // onPaint gets a table, not values LuaConvert can build, so it is called through the element table.
    const luaui::Node& n = frame.node();
    lua::State* state = n.state();
    n.pushRaw("onPaint");
    if (!lua::isfunction(state, -1)) {
        lua::pop(state);
        return;
    }
    lua::pushvalue(state, n.index());
    lua::createtable(state, 0, 8);
    lua::pushnumber(state, static_cast<double>(w / s));
    lua::setfield(state, -2, "width");
    lua::pushnumber(state, static_cast<double>(h / s));
    lua::setfield(state, -2, "height");
    lua::pushnumber(state, static_cast<double>((m.x - p.x) / s));
    lua::setfield(state, -2, "mouseX");
    lua::pushnumber(state, static_cast<double>((m.y - p.y) / s));
    lua::setfield(state, -2, "mouseY");
    lua::pushboolean(state, hovered);
    lua::setfield(state, -2, "hovered");
    lua::pushboolean(state, active);
    lua::setfield(state, -2, "pressed");
    lua::pushnumber(state, ImGui::GetTime());
    lua::setfield(state, -2, "time");
    lua::getfield(state, lua::registryIndex, "imlui.painter");
    lua::setmetatable(state, -2);
    ImDrawList* dl = frame.drawList();
    dl->PushClipRect(p, {p.x + w, p.y + h}, true);
    luaui::beginPaint(dl, p);
    luaui::safeCall(state, 2, 0);
    luaui::endPaint();
    dl->PopClipRect();
}

bool Canvas::animated() const { return m_animated; }
void Canvas::setAnimated(bool animated) { m_animated = animated; }

} // namespace imlui
