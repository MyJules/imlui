// MenuItem: one Material menu row, as a typed element.
module;

#include <string>

export module imlui.ui.elements.menu_item;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// MenuItem { text | positional string, icon, shortcut, checked (two-way), onTriggered }.
///
/// An item that has a `checked` prop at all (a value, a binding or a bind()) is checkable: it shows a check
/// mark in the icon slot while checked (instead of `icon`), toggles `checked` when chosen and keeps the menu
/// open. Other items close the menu. The icon slot is reserved when the item is checkable or has an `icon`
/// prop, so labels line up.
class MenuItem final : public Element {
public:
    static void declare(TypeBuilder<MenuItem>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::string& icon() const;
    void setIcon(std::string icon);
    /// Trailing text, e.g. "Ctrl+S".
    [[nodiscard]] const std::string& shortcut() const;
    void setShortcut(std::string shortcut);
    [[nodiscard]] bool checked() const;
    void setChecked(bool checked);

private:
    std::string m_text;
    std::string m_icon;
    std::string m_shortcut;
    bool m_checked = false;
};

} // namespace imlui

module :private;

namespace imlui {

void MenuItem::declare(TypeBuilder<MenuItem>& type) {
    type.property("text", &MenuItem::text, &MenuItem::setText)
        .property("icon", &MenuItem::icon, &MenuItem::setIcon)
        .property("shortcut", &MenuItem::shortcut, &MenuItem::setShortcut)
        .property("checked", &MenuItem::checked, &MenuItem::setChecked)
        .signal("onTriggered")
        .defaultProperty("text");
}

void MenuItem::render(Frame& frame) {
    // Whether the props exist at all (not their values) decides checkability and the icon slot.
    const bool checkable = frame.node().has("checked");
    const bool reserveIcon = checkable || frame.node().has("icon");
    const std::string icon = checkable ? (m_checked ? "check" : "") : m_icon;
    if (md::menuItem("##mi", m_text, icon, m_shortcut, false, !checkable, reserveIcon)) {
        if (checkable) {
            m_checked = !m_checked;
            notify("checked");
        }
        emit("onTriggered");
    }
}

const std::string& MenuItem::text() const { return m_text; }
void MenuItem::setText(std::string text) { m_text = std::move(text); }
const std::string& MenuItem::icon() const { return m_icon; }
void MenuItem::setIcon(std::string icon) { m_icon = std::move(icon); }
const std::string& MenuItem::shortcut() const { return m_shortcut; }
void MenuItem::setShortcut(std::string shortcut) { m_shortcut = std::move(shortcut); }
bool MenuItem::checked() const { return m_checked; }
void MenuItem::setChecked(bool checked) { m_checked = checked; }

} // namespace imlui
