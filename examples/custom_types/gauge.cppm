// Gauge: an example C++ element type for imlui_custom_types.
//
// A Material-styled arc gauge drawn with the md shape helpers. Properties `value`, `min`, `max`,
// `label` and `size` (dp); signal `onPeak(self, value)` when the value rises past 90% of `max`; method
// `reset()`. Clicking it advances the value by a tenth of the range and calls notify("value"), so the
// new value is written back to Lua (into the bound state, through a two-way bind).
module;

#include <algorithm>
#include <format>
#include <string>
#include <utility>

export module example.gauge;

import imgui;
import imlui;

export namespace example {

class Gauge final : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<Gauge>& type);

    void render(imlui::Frame& frame) override;

    [[nodiscard]] double value() const;
    void setValue(double value);
    [[nodiscard]] double min() const;
    void setMin(double min);
    [[nodiscard]] double max() const;
    void setMax(double max);
    [[nodiscard]] const std::string& label() const;
    void setLabel(std::string label);
    [[nodiscard]] double size() const;
    void setSize(double size);

    /// Back to `min`, written back to Lua.
    void reset();

private:
    [[nodiscard]] double fraction() const;

    double m_value = 0.0;
    double m_min = 0.0;
    double m_max = 1.0;
    std::string m_label;
    double m_size = 140.0;
};

} // namespace example

module :private;

namespace example {

namespace md = imlui::md;

void Gauge::declare(imlui::TypeBuilder<Gauge>& type) {
    type.property("value", &Gauge::value, &Gauge::setValue)
        .property("min", &Gauge::min, &Gauge::setMin)
        .property("max", &Gauge::max, &Gauge::setMax)
        .property("label", &Gauge::label, &Gauge::setLabel)
        .property("size", &Gauge::size, &Gauge::setSize)
        .signal("onPeak")
        .method("reset", &Gauge::reset)
        .defaultProperty("value");
}

double Gauge::value() const { return m_value; }

void Gauge::setValue(double value) {
    const double peak = m_max * 0.9;
    const bool crossed = m_value < peak && value >= peak;
    m_value = value;
    if (crossed) emit("onPeak", value);
}

double Gauge::min() const { return m_min; }
void Gauge::setMin(double min) { m_min = min; }
double Gauge::max() const { return m_max; }
void Gauge::setMax(double max) { m_max = max; }
const std::string& Gauge::label() const { return m_label; }
void Gauge::setLabel(std::string label) { m_label = std::move(label); }
double Gauge::size() const { return m_size; }
void Gauge::setSize(double size) { m_size = size; }

void Gauge::reset() {
    setValue(m_min);
    notify("value");
}

double Gauge::fraction() const {
    if (m_max <= m_min) return 0.0;
    return std::clamp((m_value - m_min) / (m_max - m_min), 0.0, 1.0);
}

void Gauge::render(imlui::Frame& frame) {
    const float side = frame.dp(static_cast<float>(m_size));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = side * 0.86f; // the arc opens at the bottom, so the gauge is less tall than wide
    if (ImGui::InvisibleButton("##gauge", {side, h})) {
        const double step = (m_max - m_min) / 10.0;
        setValue(m_value + step > m_max + 1e-9 ? m_min : m_value + step);
        notify("value"); // write back: a bind() updates its source, a plain value the element table
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    const imlui::Scheme& s = frame.scheme();
    ImDrawList* dl = frame.drawList();
    const ImVec2 c{p.x + side * 0.5f, p.y + side * 0.5f};
    const float thickness = frame.dp(12);
    const float r = side * 0.5f - thickness * 0.5f - frame.dp(2);
    constexpr float start = md::pi * 0.75f; // 135 degrees, clockwise from +x: bottom left
    constexpr float sweep = md::pi * 1.5f;

    const ImGuiID id = ImGui::GetID("##gauge");
    const float t = md::animate(id, static_cast<float>(fraction()), 10.0f);
    const bool peak = m_value >= m_max * 0.9;
    const imlui::Color accent = peak ? s.error : s.primary;

    md::strokeArc(dl, c, r, start, start + sweep, s.surfaceContainerHighest, thickness);
    if (t > 0.001f) md::strokeArc(dl, c, r, start, start + sweep * t, accent, thickness);
    const float layer = md::stateLayer(id, hovered, active);
    if (layer > 0) md::fillCircle(dl, c, r - thickness * 0.5f, s.onSurface.withAlpha(layer));

    const std::string number = std::format("{:.0f}%", fraction() * 100.0);
    const ImVec2 nsz = md::measure(imlui::TextStyle::HeadlineMedium, number);
    md::drawText(dl, imlui::TextStyle::HeadlineMedium,
                 {c.x - nsz.x * 0.5f, md::textTop(imlui::TextStyle::HeadlineMedium, c.y - frame.dp(4))},
                 peak ? s.error : s.onSurface, number);
    if (!m_label.empty()) {
        const ImVec2 lsz = md::measure(imlui::TextStyle::LabelLarge, m_label);
        md::drawText(dl, imlui::TextStyle::LabelLarge,
                     {c.x - lsz.x * 0.5f, md::textTop(imlui::TextStyle::LabelLarge, c.y + frame.dp(24))},
                     s.onSurfaceVariant, m_label);
    }
}

} // namespace example
