// Slider: the Material 3 slider, as a typed element.
module;

#include <optional>
#include <string>
#include <utility>

export module imlui.ui.elements.slider;

import imgui;
import imlui.widgets.slider;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material slider with a value label while dragging.
///
/// Props:   value = 0, from = 0, to = 1, stepSize = 0 (continuous), format = unset
/// Raw:     width
/// Signals: onMoved(self, value), onReleased(self, value)
/// Two-way: value
///
/// `format` defaults to "%.0f" for stepSize >= 1, else "%.2f"; an invalid format is reported once and replaced
/// (md::slider).
class Slider final : public Element {
public:
    static void declare(TypeBuilder<Slider>& type);

    void render(Frame& frame) override;

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
};

} // namespace imlui

module :private;

namespace imlui {

void Slider::declare(TypeBuilder<Slider>& type) {
    type.property("value", &Slider::value, &Slider::setValue)
        .property("from", &Slider::from, &Slider::setFrom)
        .property("to", &Slider::to, &Slider::setTo)
        .property("stepSize", &Slider::stepSize, &Slider::setStepSize)
        .property("format", &Slider::format, &Slider::setFormat)
        .signal("onMoved")
        .signal("onReleased");
}

void Slider::render(Frame& frame) {
    float v = static_cast<float>(m_value);
    const float step = static_cast<float>(m_stepSize);
    const std::string format = m_format.value_or(step >= 1.0f ? "%.0f" : "%.2f");
    if (md::slider("##slider", &v, static_cast<float>(m_from), static_cast<float>(m_to), step, frame.size("width"),
                   format.c_str())) {
        m_value = static_cast<double>(v);
        notify("value");
        emit("onMoved", m_value);
    }
    if (ImGui::IsItemDeactivated()) emit("onReleased", static_cast<double>(v));
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

} // namespace imlui
