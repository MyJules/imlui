// NavigationBar: the Material 3 bottom navigation bar, as a typed element.
export module imlui.ui.elements.navigation_bar;

import std;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.ui.navigation_scope;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material 3 bottom navigation bar: 80dp tall; the visible destinations share the width equally.
///
/// Props:   color = unset (surfaceContainer)
/// Raw:     width
class NavigationBar final : public Element {
public:
    static void declare(TypeBuilder<NavigationBar>& type);

    void render(Frame& frame) override;
    /// Its width (a fill of `maxWidth` unless set) and 80dp (taller only for a taller destination). None while a
    /// destination has no intrinsic size.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    /// The container color; unset: surfaceContainer.
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);

private:
    std::optional<Color> m_color;
};

} // namespace imlui

module :private;

namespace imlui {

void NavigationBar::declare(TypeBuilder<NavigationBar>& type) {
    type.doc("The Material 3 bottom navigation bar, for compact windows. Its visible children are "
             "`NavigationItem`s (directly or from a `Repeater`), which share the width equally.\n"
             "Natural size: the available width (unless `width` is set), 80dp high.")
        .fillsWidth(); // an unset width fills
    type.property("color", &NavigationBar::color, &NavigationBar::setColor)
        .doc("The container color; unset: `surfaceContainer`.");
}

void NavigationBar::render(Frame& frame) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = frame.resolvedWidth();
    const float h = frame.dp(80);
    const Color bg = m_color.value_or(frame.scheme().surfaceContainer);
    md::fillRoundRect(frame.drawList(), p, {p.x + w, p.y + h}, 0.0f, bg);
    md::pushSurface(bg);
    int count = 0;
    frame.forEachChild([&](Frame::Child& child) { count += child.isVisual() ? 1 : 0; });
    const float cell = count > 0 ? w / static_cast<float>(count) : w;
    {
        const NavigationScope scope{{NavigationScope::Style::Bar, cell}};
        int i = 0;
        frame.forEachChild([&](Frame::Child& child) {
            if (child.isVisual()) ImGui::SetCursorScreenPos({p.x + std::floor(cell * static_cast<float>(i++)), p.y});
            child.render();
        });
    }
    md::popSurface();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}

std::optional<ImVec2> NavigationBar::intrinsicSize(const Frame& frame, float maxWidth) {
    const float w = frame.widthIn(maxWidth);
    int count = 0;
    frame.forEachChild([&](Frame::Child& child) { count += child.isVisual() ? 1 : 0; });
    const float cell = count > 0 ? w / static_cast<float>(count) : w;
    // The destinations, asked in the scope render() renders them in, each where its cell starts.
    const NavigationScope scope{{NavigationScope::Style::Bar, cell}};
    float h = frame.dp(80);
    bool known = true;
    int i = 0;
    frame.forEachChild([&](Frame::Child& child) {
        if (!child.isVisual() || !known) return;
        const std::optional<ImVec2> size = child.intrinsicSize(w - std::floor(cell * static_cast<float>(i++)));
        known = size.has_value();
        if (known) h = std::max(h, size->y);
    });
    if (!known) return std::nullopt;
    return ImVec2{w, h};
}

const std::optional<Color>& NavigationBar::color() const { return m_color; }
void NavigationBar::setColor(std::optional<Color> color) { m_color = color; }

} // namespace imlui
