// ProgressBar: the Material 3 linear progress indicator, as a typed element.
module;

export module imlui.ui.elements.progress_bar;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.props;

export namespace imlui {

/// A Material 3 linear progress indicator.
///
/// Props:   value = 0 (0..1; clamped, NaN shows 0), indeterminate = false
/// Raw:     width
class ProgressBar final : public Element {
public:
    static void declare(TypeBuilder<ProgressBar>& type);

    void render(Frame& frame) override;
    /// Its natural 240dp, a fixed width, or a fill of `maxWidth`; 12dp high.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] double value() const;
    void setValue(double value);
    [[nodiscard]] bool indeterminate() const;
    void setIndeterminate(bool indeterminate);

private:
    double m_value = 0.0;
    bool m_indeterminate = false;
};

} // namespace imlui

module :private;

namespace imlui {

void ProgressBar::declare(TypeBuilder<ProgressBar>& type) {
    type.doc("A Material 3 linear progress indicator, with a gap and a stop indicator.\n"
             "Natural size: 240dp wide (raw `width`).")
        .property("value", &ProgressBar::value, &ProgressBar::setValue)
        .doc("The progress, 0..1 (0). Clamped; NaN (`done / total` with `total == 0`) shows 0.")
        .property("indeterminate", &ProgressBar::indeterminate, &ProgressBar::setIndeterminate)
        .doc("Animates instead of showing `value` (`false`).");
}

void ProgressBar::render(Frame& frame) {
    // A NaN or out-of-range value is clamped here: the widget reads any negative fraction as indeterminate.
    // `width` goes through the common size path (sizeProp), so `fillWidth` and fill-minus sizes work.
    md::progress(m_indeterminate ? -1.0f : progressFraction(m_value), frame.size("width"));
}

double ProgressBar::value() const { return m_value; }
void ProgressBar::setValue(double value) { m_value = value; }
bool ProgressBar::indeterminate() const { return m_indeterminate; }
void ProgressBar::setIndeterminate(bool indeterminate) { m_indeterminate = indeterminate; }

std::optional<ImVec2> ProgressBar::intrinsicSize(const Frame& frame, float maxWidth) {
    return md::progressSize(frame.widthIn(maxWidth));
}

} // namespace imlui
