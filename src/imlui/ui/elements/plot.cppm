// Plot: a Material line / bar chart, as a typed element.
export module imlui.ui.elements.plot;

import std;

import imlui.color;
import imlui.widgets.chart;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A line or bar chart with a hover readout.
///
/// Props:   values = {}, kind = "lines" ("bars", "histogram"), fill = true, title = unset (the label), label = "",
///          color = unset (primary), min = unset, max = unset (either one turns off the automatic range)
/// Raw:     width, height
class Plot final : public Element {
public:
    static void declare(TypeBuilder<Plot>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::vector<float>& values() const;
    void setValues(std::vector<float> values);
    [[nodiscard]] const std::string& kind() const;
    void setKind(std::string kind);
    [[nodiscard]] bool fill() const;
    void setFill(bool fill);
    /// Wins over `label` when set.
    [[nodiscard]] const std::optional<std::string>& title() const;
    void setTitle(std::optional<std::string> title);
    [[nodiscard]] const std::string& label() const;
    void setLabel(std::string label);
    /// Unset uses the theme's primary color.
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);
    [[nodiscard]] const std::optional<double>& min() const;
    void setMin(std::optional<double> min);
    [[nodiscard]] const std::optional<double>& max() const;
    void setMax(std::optional<double> max);

private:
    std::vector<float> m_values;
    std::string m_kind = "lines";
    bool m_fill = true;
    std::optional<std::string> m_title;
    std::string m_label;
    std::optional<Color> m_color;
    std::optional<double> m_min;
    std::optional<double> m_max;
};

} // namespace imlui

module :private;

namespace imlui {

void Plot::declare(TypeBuilder<Plot>& type) {
    type.fillsWidth(); // an unset width fills
    type.property("values", &Plot::values, &Plot::setValues)
        .property("kind", &Plot::kind, &Plot::setKind)
        .property("fill", &Plot::fill, &Plot::setFill)
        .property("title", &Plot::title, &Plot::setTitle)
        .property("label", &Plot::label, &Plot::setLabel)
        .property("color", &Plot::color, &Plot::setColor)
        .property("min", &Plot::min, &Plot::setMin)
        .property("max", &Plot::max, &Plot::setMax);
}

void Plot::render(Frame& frame) {
    md::ChartOptions o;
    o.bars = m_kind == "bars" || m_kind == "histogram";
    o.fill = m_fill;
    o.width = frame.size("width");
    o.height = frame.resolvedHeight(0.0f);
    o.title = m_title ? *m_title : m_label;
    o.color = m_color;
    if (m_min || m_max) {
        o.autoRange = false;
        o.min = static_cast<float>(m_min.value_or(0.0));
        o.max = static_cast<float>(m_max.value_or(1.0));
    }
    md::chart("##chart", m_values, o);
}

const std::vector<float>& Plot::values() const { return m_values; }
void Plot::setValues(std::vector<float> values) { m_values = std::move(values); }
const std::string& Plot::kind() const { return m_kind; }
void Plot::setKind(std::string kind) { m_kind = std::move(kind); }
bool Plot::fill() const { return m_fill; }
void Plot::setFill(bool fill) { m_fill = fill; }
const std::optional<std::string>& Plot::title() const { return m_title; }
void Plot::setTitle(std::optional<std::string> title) { m_title = std::move(title); }
const std::string& Plot::label() const { return m_label; }
void Plot::setLabel(std::string label) { m_label = std::move(label); }
const std::optional<Color>& Plot::color() const { return m_color; }
void Plot::setColor(std::optional<Color> color) { m_color = color; }
const std::optional<double>& Plot::min() const { return m_min; }
void Plot::setMin(std::optional<double> min) { m_min = min; }
const std::optional<double>& Plot::max() const { return m_max; }
void Plot::setMax(std::optional<double> max) { m_max = max; }

} // namespace imlui
