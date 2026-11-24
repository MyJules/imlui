// Text (alias Label): a line or paragraph in a Material type-scale style, as a typed element.
module;

#include <optional>
#include <string>

export module imlui.ui.elements.text;

import imlui.color;
import imlui.theme.typography;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Text { text | positional string, style = "bodyLarge", color, wrap = false, align = "start" }. Registered as
/// Text and Label.
class Text final : public Element {
public:
    static void declare(TypeBuilder<Text>& type);

    void render(Frame& frame) override;

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

    /// The style drawn (`style` resolved).
    [[nodiscard]] TextStyle textStyle() const;

private:
    std::string m_text;
    std::string m_style = "bodyLarge";
    TextStyle m_textStyle = TextStyle::BodyLarge;
    std::optional<Color> m_color;
    bool m_wrap = false;
    std::string m_align = "start";
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
        .defaultProperty("text");
}

void Text::render(Frame&) { md::text(m_text, m_textStyle, m_color, m_wrap, Frame::alignFactor(m_align)); }

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
TextStyle Text::textStyle() const { return m_textStyle; }

} // namespace imlui
