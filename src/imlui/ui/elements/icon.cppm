// Icon: a Material Symbols glyph, as a typed element.
export module imlui.ui.elements.icon;

import std;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material icon glyph, named by the first of `name`, `icon` and `text` that is set.
///
/// Props:   name = unset, icon = unset, text = "" (positional), size = 24 (dp), color = unset (onSurfaceVariant)
class Icon final : public Element {
public:
    static void declare(TypeBuilder<Icon>& type);

    void render(Frame& frame) override;
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::optional<std::string>& name() const;
    void setName(std::optional<std::string> name);
    [[nodiscard]] const std::optional<std::string>& icon() const;
    void setIcon(std::optional<std::string> icon);
    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] float size() const; ///< dp
    void setSize(float size);
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);

    /// The glyph drawn: `name`, else `icon`, else `text`.
    [[nodiscard]] const std::string& glyph() const;

private:
    std::optional<std::string> m_name;
    std::optional<std::string> m_icon;
    std::string m_text;
    float m_size = 24.0f;
    std::optional<Color> m_color;
};

} // namespace imlui

module :private;

namespace imlui {

void Icon::declare(TypeBuilder<Icon>& type) {
    type.doc("A Material icon glyph, named by the first of `name`, `icon` and `text` that is set.\n"
             "`Icon { \"home\" }` sets `text`. Natural size: a `size` square.")
        .property("name", &Icon::name, &Icon::setName)
        .doc("The Material icon name (`\"home\"`, `\"settings\"`, ...).")
        .property("icon", &Icon::icon, &Icon::setIcon)
        .doc("The icon name, when `name` isn't set.")
        .property("text", &Icon::text, &Icon::setText)
        .doc("The icon name, when neither `name` nor `icon` is set. The positional value: `Icon { \"home\" }`.")
        .property("size", &Icon::size, &Icon::setSize)
        .doc("The glyph's size (dp).")
        .property("color", &Icon::color, &Icon::setColor)
        .doc("The tint; unset: `onSurfaceVariant`.")
        .defaultProperty("text");
}

void Icon::render(Frame&) { md::icon(glyph(), m_size, m_color); }
std::optional<ImVec2> Icon::intrinsicSize(const Frame&, float) { return md::iconSize(m_size); }

const std::optional<std::string>& Icon::name() const { return m_name; }
void Icon::setName(std::optional<std::string> name) { m_name = std::move(name); }
const std::optional<std::string>& Icon::icon() const { return m_icon; }
void Icon::setIcon(std::optional<std::string> icon) { m_icon = std::move(icon); }
const std::string& Icon::text() const { return m_text; }
void Icon::setText(std::string text) { m_text = std::move(text); }
float Icon::size() const { return m_size; }
void Icon::setSize(float size) { m_size = size; }
const std::optional<Color>& Icon::color() const { return m_color; }
void Icon::setColor(std::optional<Color> color) { m_color = color; }

const std::string& Icon::glyph() const { return m_name ? *m_name : m_icon ? *m_icon : m_text; }

} // namespace imlui
