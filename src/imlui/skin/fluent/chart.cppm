// Fluent chart: md::chart's line and bar chart (the same axis, geometry, gaps for non-finite values and hover
// readout) in Fluent's colors. The series in the accent (or `color`), the gridlines in the divider color (the base
// line in the strong stroke), the axis labels in the secondary text color, the title in body-strong text (titleSmall)
// in the primary text color; bars have the control radius at the top; the hover readout is a flyout.
export module imlui.skin.fluent.chart;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.typography;
import imlui.widgets.chart;
import imlui.widgets.common;
import imlui.widgets.surface;

export namespace imlui::fluent {

/// Draws a chart (md::chart's parameters).
void chart(const Look& look, const char* id, const std::vector<float>& values, const md::ChartOptions& o);
/// The size of chart() with the same options: md::chartSize (320dp x 160dp natural).
[[nodiscard]] ImVec2 chartSize(const Look& look, const md::ChartOptions& o);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

ImVec2 chartSize(const Look&, const md::ChartOptions& o) { return md::chartSize(o); }

void chart(const Look& look, const char* id, const std::vector<float>& values, const md::ChartOptions& o) {
    const Colors& cs = look.colors;
    const Color c = o.color.value_or(cs.accent);
    md::ChartPalette palette;
    palette.series = c;
    palette.title = cs.text;
    palette.label = cs.textSecondary;
    palette.baseLine = cs.controlStrongStroke;
    palette.gridLine = cs.divider;
    palette.hoverLine = cs.controlStrongStroke;
    // Hover: Fluent's accent hover (slightly translucent), as an accent button's.
    palette.barHover = c.withAlpha(c.a * 0.9f);
    palette.areaAlpha = 0.24f;
    palette.barRadius = controlRadius(look);
    palette.readoutStyle = TextStyle::BodySmall;
    palette.readoutText = cs.text;
    palette.readoutBackground = [&look](ImDrawList* dl, ImVec2 a, ImVec2 b) {
        drawFlyout(dl, a, b, controlRadius(look), look);
    };
    md::drawChart(id, values, o, palette);
}

} // namespace imlui::fluent
