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
import imlui.ui.enum_names;

export namespace imlui {

/// A line or paragraph of text (also registered as Label).
///
/// Props:   text = "" (positional), style = "bodyLarge", color = unset (the theme's text color), wrap = false,
///          align = "start" ("center", "end"; ignored while wrapping; aligned text fills), font = "" (a font the
///          theme declares, drawn at the style's size; "" keeps the type scale's face)
class Text final : public Element {
public:
    static void declare(TypeBuilder<Text>& type);

    void render(Frame& frame) override;
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    /// A type-scale role ("bodyLarge", "titleMedium", ... in Lua).
    [[nodiscard]] TextStyle style() const;
    void setStyle(TextStyle style);
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);
    [[nodiscard]] bool wrap() const;
    void setWrap(bool wrap);
    /// Wrapping or aligned (not "start"): its size depends on the available width, so it fills (a Row gives it a
    /// share, and it wraps or aligns in that).
    [[nodiscard]] bool followsWidth() const;
    /// "start", "center" or "end": the line's position in the available width.
    [[nodiscard]] const std::string& align() const;
    void setAlign(std::string align);
    /// A font name the theme declares (`ui.theme { fonts = { mono = ... } }`), "regular" or "medium"; "" (the
    /// default) draws in the style's own face, and so do names the theme doesn't declare.
    [[nodiscard]] const std::string& font() const;
    void setFont(std::string font);

private:
    std::string m_text;
    TextStyle m_style = TextStyle::BodyLarge;
    std::optional<Color> m_color;
    bool m_wrap = false;
    std::string m_align = "start";
    std::string m_font;
};

} // namespace imlui

module :private;

namespace imlui {

void Text::declare(TypeBuilder<Text>& type) {
    type.doc("A line or paragraph of text in a Material type-scale `style` (`Label` is the same element).\n"
             "`Text { \"Hello\" }` sets `text`. Natural size: the text's; wrapping or aligned text fills the "
             "available width (in a `Row`, a share of the free width).")
        .property("text", &Text::text, &Text::setText)
        .doc("The content. The positional value: `Text { \"Hello\" }`.")
        .property("style", &Text::style, &Text::setStyle)
        .doc("A type-scale role: `\"bodyLarge\"` (default), `\"titleMedium\"`, `\"headlineSmall\"`, ... (a "
             "`TextStyle`).")
        .property("color", &Text::color, &Text::setColor)
        .doc("The text color; unset: the theme's text color (`onSurface`).")
        .property("wrap", &Text::wrap, &Text::setWrap)
        .doc("Wraps at the available width (a fixed `Column`'s, a `Grid` cell's, a `Row` share, ...).")
        .property("align", &Text::align, &Text::setAlign)
        .doc("The line's position in the available width: `\"start\"` (default), `\"center\"` or `\"end\"`. "
             "Ignored while `wrap` is on.")
        .property("font", &Text::font, &Text::setFont)
        .doc("A font the theme declares (`\"mono\"`, or `\"regular\"` / `\"medium\"`), drawn at the style's size; "
             "`\"\"` or an undeclared name: the style's own face.")
        .defaultProperty("text")
        .fillsWidthWhen(&Text::followsWidth); // takes a share of a Row, and wraps or aligns in it
}

void Text::render(Frame&) {
    FontFaceScope face{m_font};
    md::text(m_text, m_style, m_color, m_wrap, Frame::alignFactor(m_align));
}

std::optional<ImVec2> Text::intrinsicSize(const Frame&, float maxWidth) {
    FontFaceScope face{m_font};
    return md::textSize(m_text, m_style, m_wrap, Frame::alignFactor(m_align), maxWidth);
}

const std::string& Text::text() const { return m_text; }
void Text::setText(std::string text) { m_text = std::move(text); }
TextStyle Text::style() const { return m_style; }
void Text::setStyle(TextStyle style) { m_style = style; }
const std::optional<Color>& Text::color() const { return m_color; }
void Text::setColor(std::optional<Color> color) { m_color = color; }
bool Text::wrap() const { return m_wrap; }
bool Text::followsWidth() const { return m_wrap || Frame::alignFactor(m_align) > 0.0f; }
void Text::setWrap(bool wrap) { m_wrap = wrap; }
const std::string& Text::align() const { return m_align; }
void Text::setAlign(std::string align) { m_align = std::move(align); }
const std::string& Text::font() const { return m_font; }
void Text::setFont(std::string font) { m_font = std::move(font); }

} // namespace imlui
