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
    const ImVec2 size = chartSize(look, o);
    const float w = size.x, h = size.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The rest only draws: out of view, skip formatting the axis labels and building the series.
    if (!md::rectVisible(dl, p, {p.x + w, p.y + h})) return;
    const Color c = o.color.value_or(cs.accent);

    const md::ChartAxis axis = md::chartAxis(values, o);
    const float mn = axis.min, mx = axis.max;
    const int divisions = axis.divisions;

    const float gutter = dp(40), top = p.y + (o.title.empty() ? dp(8) : dp(32)), bottom = p.y + h - dp(8);
    const float x0 = p.x + gutter, x1 = p.x + w - dp(8);
    if (!o.title.empty()) md::drawText(dl, TextStyle::TitleSmall, {p.x, p.y}, cs.text, o.title);
    const float line = std::max(1.0f, std::round(dp(1)));
    for (int i = 0; i <= divisions; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(divisions);
        const float y = bottom - (bottom - top) * t;
        md::hLine(dl, x0, x1, y, i == 0 ? cs.controlStrongStroke : cs.divider, line);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", std::round(static_cast<double>(mn + (mx - mn) * t) * 1000.0) / 1000.0);
        const ImVec2 ts = md::measure(TextStyle::LabelSmall, buf);
        md::drawText(dl, TextStyle::LabelSmall, {x0 - dp(8) - ts.x, y - ts.y * 0.5f}, cs.textSecondary, buf);
    }
    const int n = static_cast<int>(values.size());
    if (n == 0) return;
    auto px = [&](int i) {
        if (o.bars) return x0 + (x1 - x0) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        return n == 1 ? (x0 + x1) * 0.5f : x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(n - 1);
    };
    auto py = [&](float v) { return bottom - (bottom - top) * std::clamp((v - mn) / (mx - mn), 0.0f, 1.0f); };
    auto value = [&](int i) { return values[static_cast<std::size_t>(i)]; };
    auto finite = [&](int i) { return std::isfinite(value(i)); };

    int hoverIdx = -1;
    if (hov) {
        const float mxp = ImGui::GetMousePos().x;
        float best = md::hugeExtent;
        for (int i = 0; i < n; ++i)
            if (finite(i) && std::fabs(px(i) - mxp) < best) {
                best = std::fabs(px(i) - mxp);
                hoverIdx = i;
            }
    }

    if (o.bars) {
        const float bw = std::max(dp(2), (x1 - x0) / static_cast<float>(n) * 0.6f);
        for (int i = 0; i < n; ++i) {
            if (!finite(i)) continue;
            const float x = px(i);
            const float y = py(value(i)), y0 = py(std::max(mn, 0.0f));
            // Hover: Fluent's accent hover (slightly translucent), as an accent button's.
            const Color bc = i == hoverIdx ? c.withAlpha(c.a * 0.9f) : c;
            md::fillRoundRect(dl, {x - bw * 0.5f, std::min(y, y0)}, {x + bw * 0.5f, std::max(y, y0)},
                              std::min(bw * 0.5f, controlRadius(look)), bc, ImDrawFlags_RoundCornersTop);
        }
    } else {
        std::vector<ImVec2> pts(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            if (finite(i)) pts[static_cast<std::size_t>(i)] = {px(i), py(value(i))};
        if (o.fill && n >= 2) {
            // A vertical gradient under the line, between neighbouring finite points.
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const ImU32 cTop = md::col(c.withAlpha(0.24f)), cBot = md::col(c.withAlpha(0.0f));
            for (int i = 0; i + 1 < n; ++i) {
                if (!finite(i) || !finite(i + 1)) continue;
                const ImVec2 a = pts[static_cast<std::size_t>(i)], b = pts[static_cast<std::size_t>(i + 1)];
                dl->PrimReserve(6, 4);
                const auto base = static_cast<unsigned>(dl->_VtxCurrentIdx);
                dl->PrimWriteVtx(a, uv, cTop);
                dl->PrimWriteVtx(b, uv, cTop);
                dl->PrimWriteVtx({b.x, bottom}, uv, cBot);
                dl->PrimWriteVtx({a.x, bottom}, uv, cBot);
                dl->PrimWriteIdx(static_cast<unsigned short>(base));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 1));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 2));
                dl->PrimWriteIdx(static_cast<unsigned short>(base));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 2));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 3));
            }
        }
        // One polyline per run of finite points; a lone point between gaps is a dot.
        for (int i = 0; i < n;) {
            if (!finite(i)) {
                ++i;
                continue;
            }
            int end = i;
            while (end + 1 < n && finite(end + 1))
                ++end;
            if (end > i)
                dl->AddPolyline(&pts[static_cast<std::size_t>(i)], end - i + 1, md::col(c), dp(2));
            else
                md::fillCircle(dl, pts[static_cast<std::size_t>(i)], dp(1.5f), c);
            i = end + 1;
        }
    }
    if (hoverIdx >= 0) {
        const float x = px(hoverIdx), y = py(value(hoverIdx));
        if (!o.bars) {
            md::vLine(dl, x, top, bottom, cs.controlStrongStroke, line);
            md::fillCircle(dl, {x, y}, dp(5), c);
            md::strokeCircle(dl, {x, y}, dp(6), md::surfaceColor(), dp(2));
        }
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.3g", static_cast<double>(value(hoverIdx)));
        const ImVec2 ts = md::measure(TextStyle::BodySmall, buf);
        const float bw = ts.x + dp(16), bh = dp(24);
        // Inside the chart; a chart narrower than the readout starts it at its left edge.
        const float bx = std::max(p.x, std::min(x - bw * 0.5f, p.x + w - bw)), by = std::max(p.y, y - bh - dp(10));
        drawFlyout(dl, {bx, by}, {bx + bw, by + bh}, controlRadius(look), look);
        md::drawText(dl, TextStyle::BodySmall, {bx + dp(8), md::textTop(TextStyle::BodySmall, by + bh * 0.5f)}, cs.text,
                     buf);
    }
}

} // namespace imlui::fluent
