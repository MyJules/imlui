// Checkbox: the Material 3 checkbox with an optional label, as a typed element.
export module imlui.ui.elements.checkbox;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material checkbox with a label.
///
/// Props:   text = "" (positional), checked = false
/// Signals: onToggled(self, checked)
/// Two-way: checked
class Checkbox final : public Element {
public:
    static void declare(TypeBuilder<Checkbox>& type);

    void render(Frame& frame) override;
    /// The box and its label.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] bool checked() const;
    void setChecked(bool checked);

private:
    std::string m_text;
    bool m_checked = false;
};

} // namespace imlui

module :private;

namespace imlui {

void Checkbox::declare(TypeBuilder<Checkbox>& type) {
    type.doc("A Material 3 checkbox with an optional label. A click toggles `checked` (two-way).\n"
             "`Checkbox { \"Remember me\" }` sets `text`.\n"
             "Natural size: the box and its label, 40dp high.")
        .property("text", &Checkbox::text, &Checkbox::setText)
        .doc("The label. The positional value: `Checkbox { \"Remember me\" }`.")
        .property("checked", &Checkbox::checked, &Checkbox::setChecked)
        .doc("**Two-way.** Whether the box is checked.")
        .defaultProperty("text")
        .signal<bool>("onToggled", {"checked"})
        .doc("Called when a click toggles the box, with the new `checked`.");
}

void Checkbox::render(Frame&) {
    const std::string label = Frame::labelId(m_text, "cb");
    bool v = m_checked;
    if (md::checkbox(label.c_str(), &v)) {
        m_checked = v;
        notify("checked");
        emit("onToggled", v);
    }
}

const std::string& Checkbox::text() const { return m_text; }
void Checkbox::setText(std::string text) { m_text = std::move(text); }
bool Checkbox::checked() const { return m_checked; }
void Checkbox::setChecked(bool checked) { m_checked = checked; }

std::optional<ImVec2> Checkbox::intrinsicSize(const Frame&, float) {
    return md::checkboxSize(Frame::labelId(m_text, "cb").c_str());
}

} // namespace imlui
