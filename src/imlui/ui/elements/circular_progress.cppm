// CircularProgress: the Material 3 circular progress indicator, as a typed element.
module;

export module imlui.ui.elements.circular_progress;

import imlui.widgets.progress;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.props;

export namespace imlui {

/// A Material 3 circular progress indicator.
///
/// Props:   value = 0 (0..1; clamped, NaN shows 0), indeterminate = false, thickness = 4 (dp)
/// Raw:     size (the diameter, default 48dp)
class CircularProgress final : public Element {
public:
    static void declare(TypeBuilder<CircularProgress>& type);

    void render(Frame& frame) override;

    [[nodiscard]] double value() const;
    void setValue(double value);
    [[nodiscard]] bool indeterminate() const;
    void setIndeterminate(bool indeterminate);
    [[nodiscard]] double thickness() const; ///< dp
    void setThickness(double thickness);

private:
    double m_value = 0.0;
    bool m_indeterminate = false;
    double m_thickness = 4.0;
};

} // namespace imlui

module :private;

namespace imlui {

void CircularProgress::declare(TypeBuilder<CircularProgress>& type) {
    type.property("value", &CircularProgress::value, &CircularProgress::setValue)
        .property("indeterminate", &CircularProgress::indeterminate, &CircularProgress::setIndeterminate)
        .property("thickness", &CircularProgress::thickness, &CircularProgress::setThickness);
}

void CircularProgress::render(Frame& frame) {
    // A NaN or out-of-range value is clamped here: the widget reads any negative fraction as indeterminate.
    // `size` goes through the common size path (sizeProp); 0 lets the widget pick its default diameter.
    md::circularProgress(m_indeterminate ? -1.0f : progressFraction(m_value), frame.size("size"),
                         static_cast<float>(m_thickness));
}

double CircularProgress::value() const { return m_value; }
void CircularProgress::setValue(double value) { m_value = value; }
bool CircularProgress::indeterminate() const { return m_indeterminate; }
void CircularProgress::setIndeterminate(bool indeterminate) { m_indeterminate = indeterminate; }
double CircularProgress::thickness() const { return m_thickness; }
void CircularProgress::setThickness(double thickness) { m_thickness = thickness; }

} // namespace imlui
