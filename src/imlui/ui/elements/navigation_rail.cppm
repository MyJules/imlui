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

/// Renders `child` horizontally centered in [x, x + width] at `y`, by its width on the last frame. Returns its
/// height.
float renderCentered(Frame::Child& child, float x, float width, float y) {
    ImGui::SetCursorScreenPos({x + std::floor((width - child.lastWidth(width)) * 0.5f), y});
    child.render();
    return ImGui::GetItemRectSize().y;
}

} // namespace

void NavigationRail::declare(TypeBuilder<NavigationRail>& type) {
    type.property("color", &NavigationRail::color, &NavigationRail::setColor)
        .property("spacing", &NavigationRail::spacing, &NavigationRail::setSpacing)
        .property("align", &NavigationRail::align, &NavigationRail::setAlign);
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
    // height left above the 12dp bottom margin, using their total height on the last frame. When they don't, a
    // thin themed scrollbar appears in the rail's right gutter; the destinations keep their place and width.
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID totalKey = ImGui::GetID("##railItemsH");
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
        float iy = q.y + std::max(0.0f, std::floor((regionH - bottomMargin - st->GetFloat(totalKey, 0.0f)) *
                                                   Frame::alignFactor(m_align)));
        const float top = iy;
        const NavigationScope scope{{NavigationScope::Style::Rail, w}};
        bool first = true;
        frame.forEachChild([&](Frame::Child& child) {
            if (!child.isVisual()) {
                child.render();
                return;
            }
            if (!first) iy += spacing;
            iy += renderCentered(child, q.x, w, iy);
            first = false;
        });
        st->SetFloat(totalKey, iy - top);
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
