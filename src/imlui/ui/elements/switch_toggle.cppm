// Switch: the Material 3 switch with an optional label, as a typed element (TypeBuilder).
module;

#include <string>

export module imlui.ui.elements.switch_toggle;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Switch { "label" | text = ..., checked = false (two-way), icons = true, onToggled(checked) }
/// (`switch` is a C++ keyword and can't be a module name segment, hence the module name.)
class Switch final : public Element {
public:
    static void declare(TypeBuilder<Switch>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] bool checked() const;
    void setChecked(bool checked);
    [[nodiscard]] bool icons() const; ///< the check / close icons in the handle
    void setIcons(bool icons);

private:
    std::string m_text;
    bool m_checked = false;
    bool m_icons = true;
};

} // namespace imlui

module :private;

namespace imlui {

void Switch::declare(TypeBuilder<Switch>& type) {
    type.property("text", &Switch::text, &Switch::setText)
        .property("checked", &Switch::checked, &Switch::setChecked)
        .property("icons", &Switch::icons, &Switch::setIcons)
        .defaultProperty("text")
        .signal("onToggled");
}

void Switch::render(Frame&) {
    const std::string label = Frame::labelId(m_text, "sw");
    bool v = m_checked;
    if (md::switchToggle(label.c_str(), &v, m_icons)) {
        m_checked = v;
        notify("checked");
        emit("onToggled", v);
    }
}

const std::string& Switch::text() const { return m_text; }
void Switch::setText(std::string text) { m_text = std::move(text); }
bool Switch::checked() const { return m_checked; }
void Switch::setChecked(bool checked) { m_checked = checked; }
bool Switch::icons() const { return m_icons; }
void Switch::setIcons(bool icons) { m_icons = icons; }

} // namespace imlui
