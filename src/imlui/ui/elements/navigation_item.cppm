// NavigationItem: the one destination element of every navigation container, as a typed element. It draws in the
// style of the current NavigationScope (imlui.ui.navigation_scope): a drawer pill by default, a bar or rail
// destination inside a NavigationBar or NavigationRail.
export module imlui.ui.elements.navigation_item;

import std;

import imgui;
import imlui.widgets;
import imlui.ui.navigation_scope;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A navigation destination. It draws in the style of the innermost NavigationScope: a drawer item by default,
/// a bar destination in a NavigationBar, a rail destination in a NavigationRail.
///
/// Props:   text = "" (positional), icon = "", selected = false, badge = ""
/// Raw:     width (drawer style only, default fill; bar and rail destinations get their container's width)
/// Signals: onClicked(self)
class NavigationItem final : public Element {
public:
    static void declare(TypeBuilder<NavigationItem>& type);

    void render(Frame& frame) override;
    /// A bar or rail destination: the width its container gives it; a drawer item: its width (a fill of `maxWidth`
    /// unless set), 56dp.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& icon() const;
    void setIcon(std::string icon);
    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] bool selected() const;
    void setSelected(bool selected);
    /// Badge text on the icon; empty for none.
    [[nodiscard]] const std::string& badge() const;
    void setBadge(std::string badge);

private:
    std::string m_icon;
    std::string m_text;
    bool m_selected = false;
    std::string m_badge;
};

} // namespace imlui

module :private;

namespace imlui {

void NavigationItem::declare(TypeBuilder<NavigationItem>& type) {
    type.fillsWidth(); // an unset width fills
    type.property("icon", &NavigationItem::icon, &NavigationItem::setIcon)
        .property("text", &NavigationItem::text, &NavigationItem::setText)
        .property("selected", &NavigationItem::selected, &NavigationItem::setSelected)
        .property("badge", &NavigationItem::badge, &NavigationItem::setBadge)
        .signal("onClicked")
        .defaultProperty("text");
}

void NavigationItem::render(Frame& frame) {
    bool clicked = false;
    const NavigationScope::Context& context = NavigationScope::current();
    switch (context.style) {
    case NavigationScope::Style::Bar:
        clicked = md::navigationBarItem("##nav", m_icon, m_text, m_selected, m_badge, context.itemWidth);
        break;
    case NavigationScope::Style::Rail:
        clicked = md::navigationRailItem("##nav", m_icon, m_text, m_selected, m_badge, context.itemWidth);
        break;
    case NavigationScope::Style::Drawer:
        clicked = md::navigationItem("##nav", m_icon, m_text, m_selected, m_badge, frame.size("width"));
        break;
    }
    if (clicked) emit("onClicked");
}

const std::string& NavigationItem::icon() const { return m_icon; }
void NavigationItem::setIcon(std::string icon) { m_icon = std::move(icon); }
const std::string& NavigationItem::text() const { return m_text; }
void NavigationItem::setText(std::string text) { m_text = std::move(text); }
bool NavigationItem::selected() const { return m_selected; }
void NavigationItem::setSelected(bool selected) { m_selected = selected; }
const std::string& NavigationItem::badge() const { return m_badge; }
void NavigationItem::setBadge(std::string badge) { m_badge = std::move(badge); }

std::optional<ImVec2> NavigationItem::intrinsicSize(const Frame& frame, float maxWidth) {
    // The style of the scope it is asked in, which is the one it renders in: its container asks inside its scope.
    const NavigationScope::Context& context = NavigationScope::current();
    switch (context.style) {
    case NavigationScope::Style::Bar: return md::navigationBarItemSize(context.itemWidth);
    case NavigationScope::Style::Rail: return md::navigationRailItemSize(context.itemWidth);
    case NavigationScope::Style::Drawer: break;
    }
    return md::navigationItemSize(frame.widthIn(maxWidth));
}

} // namespace imlui
