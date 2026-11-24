// ColorPicker: a Material color field (swatch + hex, opening a picker popup), as a typed element.
export module imlui.ui.elements.color_picker;

import std;

import imgui;
import imlui.color;
import imlui.widgets.color_field;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A color swatch with its hex value that opens a picker popup.
///
/// Props:   color = unset (shows white; any color spec), label = "", alpha = false
/// Raw:     width
/// Signals: onColorChanged(self, hex)
/// Two-way: color (written back as "#RRGGBB[AA]")
class ColorPicker final : public Element {
public:
    static void declare(TypeBuilder<ColorPicker>& type);

    void render(Frame& frame) override;
    /// The swatch field: its width (fixed, a fill of `maxWidth`, or its hex and label) and 56dp.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    /// Unset shows white.
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);
    [[nodiscard]] const std::string& label() const;
    void setLabel(std::string label);
    [[nodiscard]] bool alpha() const;
    void setAlpha(bool alpha);

private:
    std::optional<Color> m_color;
    std::string m_label;
    bool m_alpha = false;
};

} // namespace imlui

module :private;

namespace imlui {

void ColorPicker::declare(TypeBuilder<ColorPicker>& type) {
    type.property("color", &ColorPicker::color, &ColorPicker::setColor)
        .property("label", &ColorPicker::label, &ColorPicker::setLabel)
        .property("alpha", &ColorPicker::alpha, &ColorPicker::setAlpha)
        .signal("onColorChanged");
}

void ColorPicker::render(Frame& frame) {
    Color c = m_color.value_or(Color{1, 1, 1, 1});
    if (md::colorField("##cf", &c, m_label, m_alpha, frame.size("width"))) {
        m_color = c;
        // Writes the color back as "#RRGGBB[AA]" (LuaConvert<Color>) and emits onColorChanged(hex) with it.
        notify("color");
    }
}

const std::optional<Color>& ColorPicker::color() const { return m_color; }
void ColorPicker::setColor(std::optional<Color> color) { m_color = color; }
const std::string& ColorPicker::label() const { return m_label; }
void ColorPicker::setLabel(std::string label) { m_label = std::move(label); }
bool ColorPicker::alpha() const { return m_alpha; }
void ColorPicker::setAlpha(bool alpha) { m_alpha = alpha; }

std::optional<ImVec2> ColorPicker::intrinsicSize(const Frame& frame, float maxWidth) {
    return md::colorFieldSize(m_color.value_or(Color{1, 1, 1, 1}), m_label, frame.widthIn(maxWidth));
}

} // namespace imlui
