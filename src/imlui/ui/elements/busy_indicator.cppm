// BusyIndicator: an indeterminate circular progress indicator that can be paused, as a typed element
//.
module;

export module imlui.ui.elements.busy_indicator;

import std;

import imgui;
import imlui.widgets.progress;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// An indeterminate circular indicator. While not running it keeps its space.
///
/// Props:   running = true, thickness = 4 (dp)
/// Raw:     size (the diameter, default 48dp)
class BusyIndicator final : public Element {
public:
    static void declare(TypeBuilder<BusyIndicator>& type);

    void render(Frame& frame) override;
    /// Its diameter (`size`, else 48dp) squared, running or not.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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

std::optional<ImVec2> BusyIndicator::intrinsicSize(const Frame& frame, float) {
    // render() draws the indicator, or reserves its square while it isn't running.
    return md::circularProgressSize(frame.size("size"));
}

} // namespace imlui
