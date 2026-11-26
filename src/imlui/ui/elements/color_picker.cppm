// ColorPicker: a Material color field (swatch + hex, opening a picker popup), as a typed element.
export module imlui.ui.elements.color_picker;

import std;

import imgui;
import imlui.color;
import imlui.skin;
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
    type.doc("A color field: a swatch with the hex value of `color` (two-way). Clicking it opens a picker popup "
             "with a hue bar and a hex field.\n"
             "Natural size: as wide as the swatch, hex value and label (`width`, raw); 56dp high.")
        .property("color", &ColorPicker::color, &ColorPicker::setColor)
        .doc("**Two-way.** Any color spec; written back as `\"#RRGGBB\"` (`\"#RRGGBBAA\"` with alpha). Unset shows "
             "white.")
        .property("label", &ColorPicker::label, &ColorPicker::setLabel)
        .doc("The field's label.")
        .property("alpha", &ColorPicker::alpha, &ColorPicker::setAlpha)
        .doc("Adds an alpha bar. Without it the color stays opaque, also when the hex field gets an alpha.")
        .signal<std::string>("onColorChanged", {"hex"})
        .doc("Called on every edit, with the new color as `\"#RRGGBB\"` (`\"#RRGGBBAA\"` with alpha).");
}

void ColorPicker::render(Frame& frame) {
    Color c = m_color.value_or(Color{1, 1, 1, 1});
    if (skin().colorField("##cf", &c, m_label, m_alpha, frame.size("width"))) {
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
    return skin().colorFieldSize(m_color.value_or(Color{1, 1, 1, 1}), m_label, frame.widthIn(maxWidth));
}

} // namespace imlui
