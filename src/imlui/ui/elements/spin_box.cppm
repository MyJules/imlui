// SpinBox: a Material numeric field with decrement / increment buttons, as a typed element.
export module imlui.ui.elements.spin_box;

import std;

import imgui;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.enum_names;

export namespace imlui {

/// A numeric field with decrement and increment buttons.
///
/// Props:   value = 0, from = -1e9, to = 1e9, stepSize = 1, decimals = 0 (0..15), label = "",
///          variant = "filled" ("outlined")
/// Raw:     width
/// Signals: onValueModified(self, value)
/// Two-way: value
class SpinBox final : public Element {
public:
    static void declare(TypeBuilder<SpinBox>& type);

    void render(Frame& frame) override;
    /// Its width (natural 200dp, fixed, or a fill of `maxWidth`) and the field's height.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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
    [[nodiscard]] FieldVariant variant() const;
    void setVariant(FieldVariant variant);

private:
    double m_value = 0.0;
    double m_from = -1e9;
    double m_to = 1e9;
    double m_stepSize = 1.0;
    int m_decimals = 0;
    std::string m_label;
    FieldVariant m_variant = FieldVariant::Filled;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr int maxDecimals = 15;

} // namespace

void SpinBox::declare(TypeBuilder<SpinBox>& type) {
    type.doc("A Material 3 numeric field with decrement and increment icon buttons, editing `value` (two-way) "
             "in `from`..`to`.\n"
             "Natural size: 200dp wide (`width`, raw), 56dp high.")
        .property("value", &SpinBox::value, &SpinBox::setValue)
        .doc("**Two-way.** The value (default 0), clamped to the range when the user changes it.")
        .property("from", &SpinBox::from, &SpinBox::setFrom)
        .doc("The low end of the range (default `-1e9`). With `from > to` the range is empty and nothing clamps.")
        .property("to", &SpinBox::to, &SpinBox::setTo)
        .doc("The high end of the range (default `1e9`).")
        .property("stepSize", &SpinBox::stepSize, &SpinBox::setStepSize)
        .doc("What the buttons add or subtract (default 1).")
        .property("decimals", &SpinBox::decimals, &SpinBox::setDecimals)
        .doc("The decimals shown, clamped to 0..15 (default 0).")
        .property("label", &SpinBox::label, &SpinBox::setLabel)
        .doc("The floating label.")
        .property("variant", &SpinBox::variant, &SpinBox::setVariant)
        .doc("`\"filled\"` (default) or `\"outlined\"`.")
        .signal<double>("onValueModified", {"value"})
        .doc("Called when the user changes the value (typing or the buttons), with the new `value`.");
}

void SpinBox::render(Frame& frame) {
    double v = m_value;
    // Clamped here: the widget builds a "%.<decimals>f" format, and a double has no more than 15 significant
    // decimal digits anyway.
    const int decimals = std::clamp(m_decimals, 0, maxDecimals);
    if (skin().spinBox("##spin", &v, m_from, m_to, m_stepSize, decimals, m_label, frame.size("width"),
                       m_variant == FieldVariant::Outlined)) {
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
FieldVariant SpinBox::variant() const { return m_variant; }
void SpinBox::setVariant(FieldVariant variant) { m_variant = variant; }

std::optional<ImVec2> SpinBox::intrinsicSize(const Frame& frame, float maxWidth) {
    return skin().spinBoxSize(m_label, frame.widthIn(maxWidth), m_variant == FieldVariant::Outlined);
}

} // namespace imlui
