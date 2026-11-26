// Switch: the Material 3 switch with an optional label, as a typed element.
export module imlui.ui.elements.switch_toggle;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material switch with a label. (`switch` is a C++ keyword and can't be a module name segment, hence the
/// module name switch_toggle.)
///
/// Props:   text = "" (positional), checked = false, icons = true
/// Signals: onToggled(self, checked)
/// Two-way: checked
class Switch final : public Element {
public:
    static void declare(TypeBuilder<Switch>& type);

    void render(Frame& frame) override;
    /// The track and its label.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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
    type.doc("A Material 3 switch with an optional label. A click toggles `checked` (two-way).\n"
             "`Switch { \"Wi-Fi\" }` sets `text`.\n"
             "Natural size: the 52dp track and its label, 40dp high.")
        .property("text", &Switch::text, &Switch::setText)
        .doc("The label. The positional value: `Switch { \"Wi-Fi\" }`.")
        .property("checked", &Switch::checked, &Switch::setChecked)
        .doc("**Two-way.** Whether the switch is on.")
        .property("icons", &Switch::icons, &Switch::setIcons)
        .doc("Shows the check / close icons in the handle (default `true`).")
        .defaultProperty("text")
        .signal<bool>("onToggled", {"checked"})
        .doc("Called when a click toggles the switch, with the new `checked`.");
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

std::optional<ImVec2> Switch::intrinsicSize(const Frame&, float) {
    return md::switchSize(Frame::labelId(m_text, "sw").c_str());
}

} // namespace imlui
