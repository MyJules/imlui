// Canvas: custom drawing from Lua, as a typed element. Every frame it calls the `onPaint`
// handler with the painter object (imlui.lua.painter, registry "imlui.painter"), which draws into the canvas
// area in dp relative to its top-left corner.
export module imlui.ui.elements.canvas;

import std;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.theme.typography;
import imlui.lua.element_scope;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.painter;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.lua_convert;
import imlui.types.lua_types;
import imlui.types.type_builder;
import imlui.ui.clock;
import imlui.ui.enum_names;

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
    /// Its width (a fill of `maxWidth`, or fixed) and height (200dp unless set). None for a fill height.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] bool animated() const;
    void setAnimated(bool animated);

private:
    bool m_animated = false;
};

} // namespace imlui

module :private;

namespace imlui {

/// The painter `p` of `onPaint`, only to type the signal in the Lua definitions (it is built in render()).
struct CanvasPainter {};

/// The `CanvasPainter` class of the Lua definitions: the fields render() sets and the methods of imlui.lua.painter.
template <>
struct LuaConvert<CanvasPainter> {
    static std::string luaType(LuaTypes& types) {
        constexpr std::string_view name = "CanvasPainter";
        if (types.claim(name)) {
            luaTypeOf<TextStyle>(types); // declares the TextStyle alias, the type of p:text's style
            types.define(
                name,
                "---The painter `p` of `Canvas.onPaint`: draws into the canvas, clipped to it, only while `onPaint` "
                "runs (else it raises).\n"
                "---Coordinates and sizes are in dp from the canvas's top-left corner; a color is a color spec or an "
                "`{r, g, b, a}` table (anything else: the default).\n"
                "---@class CanvasPainter\n"
                "---@field width number The canvas's width (dp).\n"
                "---@field height number The canvas's height (dp).\n"
                "---@field mouseX number The mouse position relative to the canvas (dp).\n"
                "---@field mouseY number The mouse position relative to the canvas (dp).\n"
                "---@field hovered boolean The mouse is over the canvas.\n"
                "---@field pressed boolean The canvas is held down.\n"
                "---@field time number Seconds since start (needs `animated = true` to keep moving).\n"
                "---@field line (fun(self: CanvasPainter, x1: number, y1: number, x2: number, y2: number, "
                "color?: Color, thickness?: number)) A line (`onSurface`, 1dp); a horizontal or vertical one is a "
                "pixel-snapped band.\n"
                "---@field rect (fun(self: CanvasPainter, x: number, y: number, w: number, h: number, color?: Color, "
                "rounding?: number, outline?: boolean, thickness?: number)) A rectangle, filled (`primary`) or with "
                "`outline` a `thickness` stroke.\n"
                "---@field circle (fun(self: CanvasPainter, cx: number, cy: number, r: number, color?: Color, "
                "outline?: boolean, thickness?: number)) A circle, filled (`primary`) or with `outline` a stroke.\n"
                "---@field text (fun(self: CanvasPainter, x: number, y: number, text: string, color?: Color, "
                "style?: TextStyle, font?: string)) Text with its top-left at (x, y): `onSurface`, `style` "
                "`\"bodyMedium\"`, `font` a named font (unset: the style's face).\n"
                "---@field icon (fun(self: CanvasPainter, cx: number, cy: number, name: string, size?: number, "
                "color?: Color)) A Material icon centered on (cx, cy): `size` 24, `onSurfaceVariant`.\n"
                "---@field polyline (fun(self: CanvasPainter, points: number[], color?: Color, thickness?: number, "
                "closed?: boolean)) An anti-aliased polyline through `{x1, y1, x2, y2, ...}` (`primary`, 1dp).\n");
        }
        return std::string{name};
    }
};

void Canvas::declare(TypeBuilder<Canvas>& type) {
    type.doc("Custom drawing from Lua: `onPaint(self, p)` draws with the painter `p` on every rendered frame, clipped "
             "to the canvas, which is an invisible button.\n"
             "Frames render on demand: drawing that changes over time (reads `p.time`) needs `animated = true`.\n"
             "Natural size: the available width (fills), 200dp high (`height = -1` fills the remaining height).");
    type.fillsWidth(); // an unset width fills
    type.property("animated", &Canvas::animated, &Canvas::setAnimated)
        .doc("Requests a frame every frame while the canvas is rendered, so time-based drawing keeps moving.")
        .signal<double, double>("onClicked", {"x", "y"})
        .doc("Called on a click, with the position in dp relative to the canvas's top-left corner.")
        .signal<CanvasPainter>("onPaint", {"p"})
        .doc("Draws the canvas with the painter `p`, on every rendered frame (only then can `p` be used).");
}

void Canvas::render(Frame& frame) {
    // Unset: the available width, 200dp high.
    const float w = frame.resolvedWidth();
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
    lua::pushnumber(state, clockTime());
    lua::setfield(state, -2, "time");
    lua::getfield(state, lua::registryIndex, "imlui.painter");
    lua::setmetatable(state, -2);
    ImDrawList* dl = frame.drawList();
    dl->PushClipRect(p, {p.x + w, p.y + h}, true);
    luaui::beginPaint(dl, p);
    luaui::safeCall(state, luaui::scopedArgs(state, n.index(), 2), 0);
    luaui::endPaint();
    dl->PopClipRect();
}

bool Canvas::animated() const { return m_animated; }
void Canvas::setAnimated(bool animated) { m_animated = animated; }

std::optional<ImVec2> Canvas::intrinsicSize(const Frame& frame, float maxWidth) {
    const float height = frame.size("height");
    if (height < 0) return std::nullopt; // fills a height known only where it renders
    return ImVec2{frame.widthIn(maxWidth), height > 0 ? height : frame.dp(200)};
}

} // namespace imlui
