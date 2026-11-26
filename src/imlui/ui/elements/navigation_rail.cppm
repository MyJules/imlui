// NavigationRail: the Material 3 navigation rail, as a typed element.
export module imlui.ui.elements.navigation_rail;

import std;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.ui.navigation_scope;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material 3 navigation rail: 80dp wide, full height by default. The optional header sits fixed at the top;
/// the destinations stack below it, aligned in the rest of the height. When they don't fit they scroll
/// vertically (mouse wheel, and a thin themed scrollbar shown only then).
///
/// Props:   color = unset (surface), spacing = 12, align = "top" ("center", "bottom")
/// Raw:     header (an element), height
class NavigationRail final : public Element {
public:
    static void declare(TypeBuilder<NavigationRail>& type);

    void render(Frame& frame) override;

    /// The container color; unset: surface.
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);
    [[nodiscard]] float spacing() const; ///< dp between destinations
    void setSpacing(float spacing);
    [[nodiscard]] const std::string& align() const;
    void setAlign(std::string align);

private:
    std::optional<Color> m_color;
    float m_spacing = 12.0f;
    std::string m_align = "top";
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Renders `child` horizontally centered in [x, x + width] at `y`, by its intrinsic width or else its width on the
/// last frame (a child that fills: the rail's). Unseen on its first frame without either. Returns its height.
float renderCentered(Frame::Child& child, float x, float width, float y) {
    const bool fills = child.fillsWidth();
    const bool unseen = !fills && !child.expectedSize(width);
    const float w = fills ? width : child.expectedWidth(width, width);
    ImGui::SetCursorScreenPos({x + std::floor((width - w) * 0.5f), y});
    if (unseen)
        child.renderUnseen();
    else
        child.render();
    return ImGui::GetItemRectSize().y;
}

} // namespace

void NavigationRail::declare(TypeBuilder<NavigationRail>& type) {
    type.doc("The Material 3 navigation rail, for medium windows. Its children are `NavigationItem`s, stacked "
             "below an optional `header` element (e.g. a menu `IconButton` or a FAB), which stays fixed at the top. "
             "When the destinations don't fit they scroll, with a thin scrollbar shown only then.\n"
             "Natural size: 80dp wide, the available height (unless `height` is set; at least 80dp).")
        .property("color", &NavigationRail::color, &NavigationRail::setColor)
        .doc("The container color; unset: `surface`.")
        .property("spacing", &NavigationRail::spacing, &NavigationRail::setSpacing)
        .doc("The gap between destinations (dp).")
        .property("align", &NavigationRail::align, &NavigationRail::setAlign)
        .doc("Where the destinations sit in the height below the header while they fit: `\"top\"` (default), "
             "`\"center\"` or `\"bottom\"`.");
    type.rawProperty("header", "Element|false")
        .doc("An element fixed at the top, centered and not scrolled (a menu `IconButton`, a FAB: "
             "`Button { variant = \"fab\" }`). It's in drawer style, not a rail destination.");
}

void NavigationRail::render(Frame& frame) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = frame.dp(80);
    const float h = std::max(frame.resolvedHeight(frame.available().y), frame.dp(80));
    const Color bg = m_color.value_or(frame.scheme().surface);
    md::fillRoundRect(frame.drawList(), p, {p.x + w, p.y + h}, 0.0f, bg);
    md::pushSurface(bg);

    float y = p.y + frame.dp(12);
    frame.withElementProp("header", [&](Frame::Child& header) {
        const NavigationScope scope{{}};
        y += renderCentered(header, p.x, w, y) + frame.dp(28);
    });

    // The destinations scroll in a child region below the header, from there to the rail's bottom edge. While
    // they fit, the region shows no scrollbar and the layout is the plain stack: `align` places them in the
    // height left above the 12dp bottom margin, by their heights (intrinsic, else last frame's). When they don't, a
    // thin themed scrollbar appears in the rail's right gutter; the destinations keep their place and width.
    const float regionH = std::max(1.0f, p.y + h - y);
    const float bottomMargin = frame.dp(12);
    ImGui::SetCursorScreenPos({p.x, y});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::min(ImGui::GetStyle().ScrollbarSize, frame.dp(8)));
    ImGui::BeginChild("##destinations", {w, regionH}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);
    {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        const float spacing = frame.dp(m_spacing);
        // The children's IDs are in the region's ID scope: read their sizes in it too.
        const float align = Frame::alignFactor(m_align);
        // The destinations' scope, also while they are measured: a NavigationItem's size depends on it.
        const NavigationScope scope{{NavigationScope::Style::Rail, w}};
        float total = 0.0f;
        bool first = true;
        if (align > 0.0f) {
            frame.forEachChild([&](Frame::Child& child) {
                if (!child.isVisual()) return;
                total += child.expectedHeight(w) + (first ? 0.0f : spacing);
                first = false;
            });
        }
        float iy = q.y + std::max(0.0f, std::floor((regionH - bottomMargin - total) * align));
        first = true;
        frame.forEachChild([&](Frame::Child& child) {
            if (!child.isVisual()) {
                child.render();
                return;
            }
            if (!first) iy += spacing;
            iy += renderCentered(child, q.x, w, iy);
            first = false;
        });
        // The bottom margin is part of the scrolled content, so the last destination scrolls clear of the edge.
        ImGui::SetCursorScreenPos({q.x, iy});
        ImGui::Dummy({1.0f, bottomMargin});
    }
    ImGui::EndChild();
    md::popSurface();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}

const std::optional<Color>& NavigationRail::color() const { return m_color; }
void NavigationRail::setColor(std::optional<Color> color) { m_color = color; }
float NavigationRail::spacing() const { return m_spacing; }
void NavigationRail::setSpacing(float spacing) { m_spacing = spacing; }
const std::string& NavigationRail::align() const { return m_align; }
void NavigationRail::setAlign(std::string align) { m_align = std::move(align); }

} // namespace imlui
