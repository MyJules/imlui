// BusyIndicator: an indeterminate circular progress indicator that can be paused, as a typed element
// (TypeBuilder).
module;

export module imlui.ui.elements.busy_indicator;

import imgui;
import imlui.widgets.progress;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// BusyIndicator { running = true, size (diameter, default 48 dp), thickness = 4 }. While not running it keeps
/// its space.
class BusyIndicator final : public Element {
public:
    static void declare(TypeBuilder<BusyIndicator>& type);

    void render(Frame& frame) override;

    [[nodiscard]] bool running() const;
    void setRunning(bool running);
    [[nodiscard]] double thickness() const; ///< dp
    void setThickness(double thickness);

private:
    bool m_running = true;
    double m_thickness = 4.0;
};

} // namespace imlui

module :private;

namespace imlui {

void BusyIndicator::declare(TypeBuilder<BusyIndicator>& type) {
    type.property("running", &BusyIndicator::running, &BusyIndicator::setRunning)
        .property("thickness", &BusyIndicator::thickness, &BusyIndicator::setThickness);
}

void BusyIndicator::render(Frame& frame) {
    const float size = frame.size("size");
    if (m_running) {
        md::circularProgress(-1.0f, size, static_cast<float>(m_thickness));
    } else {
        const float side = size > 0 ? size : frame.dp(48);
        ImGui::Dummy({side, side});
    }
}

bool BusyIndicator::running() const { return m_running; }
void BusyIndicator::setRunning(bool running) { m_running = running; }
double BusyIndicator::thickness() const { return m_thickness; }
void BusyIndicator::setThickness(double thickness) { m_thickness = thickness; }

} // namespace imlui
