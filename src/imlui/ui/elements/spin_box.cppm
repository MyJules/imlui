// SpinBox: a Material numeric field with decrement / increment buttons, as a typed element.
export module imlui.ui.elements.spin_box;

import std;

import imlui.widgets.spin_box;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A numeric field with decrement and increment buttons.
///
/// Props:   value = 0, from = -1e9, to = 1e9, stepSize = 1, decimals = 0, label = "",
///          variant = "filled" ("outlined")
/// Raw:     width
/// Signals: onValueModified(self, value)
/// Two-way: value
class SpinBox final : public Element {
public:
    static void declare(TypeBuilder<SpinBox>& type);

    void render(Frame& frame) override;

    [[nodiscard]] double value() const;
    void setValue(double value);
    [[nodiscard]] double from() const;
    void setFrom(double from);
    [[nodiscard]] double to() const;
    void setTo(double to);
    [[nodiscard]] double stepSize() const;
    void setStepSize(double step);
    [[nodiscard]] int decimals() const;
    void setDecimals(int decimals);
    [[nodiscard]] const std::string& label() const;
    void setLabel(std::string label);
    [[nodiscard]] const std::string& variant() const;
    void setVariant(std::string variant);

private:
    double m_value = 0.0;
    double m_from = -1e9;
    double m_to = 1e9;
    double m_stepSize = 1.0;
    int m_decimals = 0;
    std::string m_label;
    std::string m_variant = "filled";
};

} // namespace imlui

module :private;

namespace imlui {

void SpinBox::declare(TypeBuilder<SpinBox>& type) {
    type.property("value", &SpinBox::value, &SpinBox::setValue)
        .property("from", &SpinBox::from, &SpinBox::setFrom)
        .property("to", &SpinBox::to, &SpinBox::setTo)
        .property("stepSize", &SpinBox::stepSize, &SpinBox::setStepSize)
        .property("decimals", &SpinBox::decimals, &SpinBox::setDecimals)
        .property("label", &SpinBox::label, &SpinBox::setLabel)
        .property("variant", &SpinBox::variant, &SpinBox::setVariant)
        .signal("onValueModified");
}

void SpinBox::render(Frame& frame) {
    double v = m_value;
    if (md::spinBox("##spin", &v, m_from, m_to, m_stepSize, m_decimals, m_label, frame.size("width"),
                    m_variant == "outlined")) {
        m_value = v;
        notify("value");
        emit("onValueModified", m_value);
    }
}

double SpinBox::value() const { return m_value; }
void SpinBox::setValue(double value) { m_value = value; }
double SpinBox::from() const { return m_from; }
void SpinBox::setFrom(double from) { m_from = from; }
double SpinBox::to() const { return m_to; }
void SpinBox::setTo(double to) { m_to = to; }
double SpinBox::stepSize() const { return m_stepSize; }
void SpinBox::setStepSize(double step) { m_stepSize = step; }
int SpinBox::decimals() const { return m_decimals; }
void SpinBox::setDecimals(int decimals) { m_decimals = decimals; }
const std::string& SpinBox::label() const { return m_label; }
void SpinBox::setLabel(std::string label) { m_label = std::move(label); }
const std::string& SpinBox::variant() const { return m_variant; }
void SpinBox::setVariant(std::string variant) { m_variant = std::move(variant); }

} // namespace imlui
