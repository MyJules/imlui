// Slider: the Material 3 slider, as a typed element.
export module imlui.ui.elements.slider;

import std;

import imgui;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material slider with a value label while dragging.
///
/// Props:   value = 0, from = 0, to = 1, stepSize = 0 (continuous), format = unset
/// Raw:     width
/// Signals: onMoved(self, value), onReleased(self, value) (only when the value changed during the drag or click)
/// Two-way: value (written only when the user moves the slider: an untouched value keeps its exact double)
///
/// `format` defaults to "%.0f" for stepSize >= 1, else "%.2f"; an invalid format is reported once and replaced
/// (md::slider).
class Slider final : public Element {
public:
    static void declare(TypeBuilder<Slider>& type);

    void render(Frame& frame) override;
    /// Its natural 200dp, a fixed width, or a fill of `maxWidth`; 44dp high.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] double value() const;
    void setValue(double value);
    [[nodiscard]] double from() const;
    void setFrom(double from);
    [[nodiscard]] double to() const;
    void setTo(double to);
    [[nodiscard]] double stepSize() const;
    void setStepSize(double step);
    /// The printf value format; unset picks one from stepSize.
    [[nodiscard]] const std::optional<std::string>& format() const;
    void setFormat(std::optional<std::string> format);

private:
    double m_value = 0.0;
    double m_from = 0.0;
    double m_to = 1.0;
    double m_stepSize = 0.0;
    std::optional<std::string> m_format;
    double m_pressValue = 0.0; ///< the value when the current drag or click started
};

} // namespace imlui

module :private;

namespace imlui {

void Slider::declare(TypeBuilder<Slider>& type) {
    type.doc("A Material 3 slider for `value` (two-way) in `from`..`to`; a value label shows above the handle "
             "while you drag. `value` is written back only when the user moves the slider, so a bound `0.1` stays "
             "`0.1`.\n"
             "Natural size: fills the width (`width`, raw); 44dp high.");
    type.fillsWidth(); // an unset width fills
    type.property("value", &Slider::value, &Slider::setValue)
        .doc("**Two-way.** The value (default 0). A stepped value is `from + k * stepSize` without binary noise, at "
             "most the last whole step in the range; with an integral `stepSize` it is an integer.")
        .property("from", &Slider::from, &Slider::setFrom)
        .doc("The low end of the range (default 0). An empty (`from >= to`) or non-finite range leaves `value` "
             "alone.")
        .property("to", &Slider::to, &Slider::setTo)
        .doc("The high end of the range (default 1).")
        .property("stepSize", &Slider::stepSize, &Slider::setStepSize)
        .doc("The step; `0` (default, or below, or not finite): continuous. Tick marks show with at most 40 steps.")
        .property("format", &Slider::format, &Slider::setFormat)
        .doc("The value label's printf format, with exactly one `%f`, `%e` or `%g` conversion (plus any `%%`); an "
             "invalid one is reported once and replaced by `\"%.2f\"`. Unset: `\"%.0f\"` when `stepSize >= 1`, "
             "else `\"%.2f\"`.")
        .signal<double>("onMoved", {"value"})
        .doc("Called while the user changes the value, with the new `value`.")
        .signal<double>("onReleased", {"value"})
        .doc("Called when the handle is released after the drag or click changed the value, with the final "
             "`value`.");
}

void Slider::render(Frame& frame) {
    double v = m_value;
    const std::string format = m_format.value_or(m_stepSize >= 1.0 ? "%.0f" : "%.2f");
    const bool changed = skin().slider("##slider", &v, m_from, m_to, m_stepSize, frame.size("width"), format.c_str());
    if (ImGui::IsItemActivated()) m_pressValue = m_value;
    if (changed) {
        m_value = v;
        notify("value");
        emit("onMoved", m_value);
    }
    if (ImGui::IsItemDeactivated() && m_value != m_pressValue) emit("onReleased", m_value);
}

double Slider::value() const { return m_value; }
void Slider::setValue(double value) { m_value = value; }
double Slider::from() const { return m_from; }
void Slider::setFrom(double from) { m_from = from; }
double Slider::to() const { return m_to; }
void Slider::setTo(double to) { m_to = to; }
double Slider::stepSize() const { return m_stepSize; }
void Slider::setStepSize(double step) { m_stepSize = step; }
const std::optional<std::string>& Slider::format() const { return m_format; }
void Slider::setFormat(std::optional<std::string> format) { m_format = std::move(format); }

std::optional<ImVec2> Slider::intrinsicSize(const Frame& frame, float maxWidth) {
    return skin().sliderSize(frame.widthIn(maxWidth));
}

} // namespace imlui
