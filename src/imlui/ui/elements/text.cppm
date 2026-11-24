// Text (alias Label): a line or paragraph in a Material type-scale style, as a typed element.
export module imlui.ui.elements.text;

import std;

import imgui;
import imlui.color;
import imlui.theme.font_face_scope;
import imlui.theme.typography;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A line or paragraph of text (also registered as Label).
///
/// Props:   text = "" (positional), style = "bodyLarge", color = unset (the theme's text color), wrap = false,
///          align = "start" ("center", "end"; ignored while wrapping), font = "" (a font the theme declares,
///          drawn at the style's size; "" keeps the type scale's face)
class Text final : public Element {
public:
    static void declare(TypeBuilder<Text>& type);

    void render(Frame& frame) override;
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    /// A type-scale role name ("bodyLarge", "titleMedium", ...); unknown names draw as bodyLarge.
    [[nodiscard]] const std::string& style() const;
    void setStyle(std::string style);
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);
    [[nodiscard]] bool wrap() const;
    void setWrap(bool wrap);
    /// "start", "center" or "end": the line's position in the available width.
    [[nodiscard]] const std::string& align() const;
    void setAlign(std::string align);
    /// A font name the theme declares (`ui.theme { fonts = { mono = ... } }`), "regular" or "medium"; "" (the
    /// default) draws in the style's own face, and so do names the theme doesn't declare.
    [[nodiscard]] const std::string& font() const;
    void setFont(std::string font);

    /// The style drawn (`style` resolved).
    [[nodiscard]] TextStyle textStyle() const;

private:
    std::string m_text;
    std::string m_style = "bodyLarge";
    TextStyle m_textStyle = TextStyle::BodyLarge;
    std::optional<Color> m_color;
    bool m_wrap = false;
    std::string m_align = "start";
    std::string m_font;
};

} // namespace imlui

module :private;

namespace imlui {

void Text::declare(TypeBuilder<Text>& type) {
    type.property("text", &Text::text, &Text::setText)
        .property("style", &Text::style, &Text::setStyle)
        .property("color", &Text::color, &Text::setColor)
        .property("wrap", &Text::wrap, &Text::setWrap)
        .property("align", &Text::align, &Text::setAlign)
        .property("font", &Text::font, &Text::setFont)
        .defaultProperty("text")
        .fillsWidthWhen(&Text::wrap); // wrapping text takes a share of a Row and wraps at it
}

void Text::render(Frame&) {
    FontFaceScope face{m_font};
    md::text(m_text, m_textStyle, m_color, m_wrap, Frame::alignFactor(m_align));
}

std::optional<ImVec2> Text::intrinsicSize(const Frame&, float) {
    // Wrapping text fills (its width is the parent's to give); aligned text is offset within the available width.
    if (m_wrap || Frame::alignFactor(m_align) > 0.0f) return std::nullopt;
    FontFaceScope face{m_font};
    return md::textSize(m_text, m_textStyle);
}

const std::string& Text::text() const { return m_text; }
void Text::setText(std::string text) { m_text = std::move(text); }
const std::string& Text::style() const { return m_style; }
void Text::setStyle(std::string style) {
    m_style = std::move(style);
    m_textStyle = textStyleFromName(m_style).value_or(TextStyle::BodyLarge);
}
const std::optional<Color>& Text::color() const { return m_color; }
void Text::setColor(std::optional<Color> color) { m_color = color; }
bool Text::wrap() const { return m_wrap; }
void Text::setWrap(bool wrap) { m_wrap = wrap; }
const std::string& Text::align() const { return m_align; }
void Text::setAlign(std::string align) { m_align = std::move(align); }
const std::string& Text::font() const { return m_font; }
void Text::setFont(std::string font) { m_font = std::move(font); }
TextStyle Text::textStyle() const { return m_textStyle; }

} // namespace imlui
