// ProgressBar: the Material 3 linear progress indicator, as a typed element (TypeBuilder).
module;

export module imlui.ui.elements.progress_bar;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// ProgressBar { value = 0..1, indeterminate = false, width / fillWidth }
class ProgressBar final : public Element {
public:
    static void declare(TypeBuilder<ProgressBar>& type);

    void render(Frame& frame) override;

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
    type.property("value", &ProgressBar::value, &ProgressBar::setValue)
        .property("indeterminate", &ProgressBar::indeterminate, &ProgressBar::setIndeterminate);
}

void ProgressBar::render(Frame& frame) {
    // `width` goes through the common size path (sizeProp), so `fillWidth` and fill-minus sizes work.
    md::progress(m_indeterminate ? -1.0f : static_cast<float>(m_value), frame.size("width"));
}

double ProgressBar::value() const { return m_value; }
void ProgressBar::setValue(double value) { m_value = value; }
bool ProgressBar::indeterminate() const { return m_indeterminate; }
void ProgressBar::setIndeterminate(bool indeterminate) { m_indeterminate = indeterminate; }

} // namespace imlui
