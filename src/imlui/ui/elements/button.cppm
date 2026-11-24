// Button: the Material 3 common button, as a typed element.
module;

#include <string>

export module imlui.ui.elements.button;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material 3 common button.
///
/// Props:   text = "" (positional), variant = "filled" ("tonal", "outlined", "text", "elevated", "fab"), icon = ""
/// Raw:     width, height, menu (a Menu opened on click)
/// Signals: onClicked(self)
class Button final : public Element {
public:
    static void declare(TypeBuilder<Button>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::string& variant() const; ///< "filled", "tonal", "outlined", "text", "elevated", "fab"
    void setVariant(std::string variant);
    [[nodiscard]] const std::string& icon() const; ///< optional leading Material icon
    void setIcon(std::string icon);

private:
    std::string m_text;
    std::string m_variant = "filled";
    std::string m_icon;
};

} // namespace imlui

module :private;

namespace imlui {

void Button::declare(TypeBuilder<Button>& type) {
    type.property("text", &Button::text, &Button::setText)
        .property("variant", &Button::variant, &Button::setVariant)
        .property("icon", &Button::icon, &Button::setIcon)
        .defaultProperty("text")
        .signal("onClicked");
}

void Button::render(Frame& frame) {
    const std::string label = Frame::labelId(m_text, "btn");
    const bool clicked =
        md::button(label.c_str(), md::buttonVariant(m_variant), {frame.size("width"), frame.size("height")}, m_icon);
    if (clicked) emit("onClicked");
    frame.attachMenu(clicked);
}

const std::string& Button::text() const { return m_text; }
void Button::setText(std::string text) { m_text = std::move(text); }
const std::string& Button::variant() const { return m_variant; }
void Button::setVariant(std::string variant) { m_variant = std::move(variant); }
const std::string& Button::icon() const { return m_icon; }
void Button::setIcon(std::string icon) { m_icon = std::move(icon); }

} // namespace imlui
