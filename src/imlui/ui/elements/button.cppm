// Button: the Material 3 common button, as a typed element.
export module imlui.ui.elements.button;

import std;

import imgui;
import imlui.theme.font_face_scope;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material 3 common button.
///
/// Props:   text = "" (positional), variant = "filled" ("tonal", "outlined", "text", "elevated", "fab"), icon = "",
///          font = "" (a font the theme declares for the label; see Text)
/// Raw:     width, height, menu (a Menu opened on click)
/// Signals: onClicked(self)
class Button final : public Element {
public:
    static void declare(TypeBuilder<Button>& type);

    void render(Frame& frame) override;
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::string& variant() const; ///< "filled", "tonal", "outlined", "text", "elevated", "fab"
    void setVariant(std::string variant);
    [[nodiscard]] const std::string& icon() const; ///< optional leading Material icon
    void setIcon(std::string icon);
    [[nodiscard]] const std::string& font() const; ///< the label's font (see Text::font)
    void setFont(std::string font);

private:
    std::string m_text;
    std::string m_variant = "filled";
    std::string m_icon;
    std::string m_font;
};

} // namespace imlui

module :private;

namespace imlui {

void Button::declare(TypeBuilder<Button>& type) {
    type.property("text", &Button::text, &Button::setText)
        .property("variant", &Button::variant, &Button::setVariant)
        .property("icon", &Button::icon, &Button::setIcon)
        .property("font", &Button::font, &Button::setFont)
        .defaultProperty("text")
        .signal("onClicked");
}

void Button::render(Frame& frame) {
    const std::string label = Frame::labelId(m_text, "btn");
    bool clicked = false;
    {
        FontFaceScope face{m_font};
        clicked = md::button(label.c_str(), md::buttonVariant(m_variant),
                             {frame.size("width"), frame.resolvedHeight(0.0f)}, m_icon);
    }
    if (clicked) emit("onClicked");
    frame.attachMenu(clicked);
}

std::optional<ImVec2> Button::intrinsicSize(const Frame& frame, float maxWidth) {
    // A fill width is resolved against `maxWidth` (it is at least the button's minimum); a fill height depends on
    // where it renders.
    const float h = frame.size("height");
    if (h < 0.0f) return std::nullopt;
    const std::string label = Frame::labelId(m_text, "btn");
    FontFaceScope face{m_font};
    return md::buttonSize(label.c_str(), md::buttonVariant(m_variant), {frame.widthIn(maxWidth), h}, m_icon);
}

const std::string& Button::text() const { return m_text; }
void Button::setText(std::string text) { m_text = std::move(text); }
const std::string& Button::variant() const { return m_variant; }
void Button::setVariant(std::string variant) { m_variant = std::move(variant); }
const std::string& Button::icon() const { return m_icon; }
void Button::setIcon(std::string icon) { m_icon = std::move(icon); }
const std::string& Button::font() const { return m_font; }
void Button::setFont(std::string font) { m_font = std::move(font); }

} // namespace imlui
