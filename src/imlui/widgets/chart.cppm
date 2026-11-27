// Line / bar chart with "nice" axis, grid, gradient area and hover readout.
export module imlui.widgets.chart;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets.common;
import imlui.widgets.surface;

export namespace imlui::md {

struct ChartOptions {
    float min = 0, max = 1;
    bool autoRange = true;
    bool bars = false;
    bool fill = true;
    float width = -1.0f, height = 0.0f;
    std::optional<Color> color;
    std::string_view title;
};

/// Line / bar chart with grid, gradient area and hover readout.
void chart(const char* id, const std::vector<float>& values, const ChartOptions& o);

/// What a skin's chart draws differently: its colors, the bars' radius and the hover readout. drawChart() does the
/// rest, the same for every skin.
struct ChartPalette {
    Color series;           ///< the line, its area and the bars
    Color title;            ///< the title (titleSmall)
    Color label;            ///< the axis labels (labelSmall)
    Color baseLine;         ///< the grid line at the axis' minimum
    Color gridLine;         ///< the other grid lines
    Color hoverLine;        ///< the vertical line through the hovered point of a line chart
    Color barHover;         ///< a hovered bar
    float areaAlpha = 0.0f; ///< the area's opacity under the line (it fades to 0 at the bottom)
    float barRadius = 0.0f; ///< the bars' top corner radius, pixels (at most half a bar)
    TextStyle readoutStyle = TextStyle::LabelMedium;
    Color readoutText;
    /// Draws the hover readout's background in [a, b].
    std::function<void(ImDrawList* dl, ImVec2 a, ImVec2 b)> readoutBackground;
};
/// chart() in `palette`: the layout, the axis (chartAxis()), the gaps for non-finite values and the hover readout of
/// every skin's chart. A line with more points than pixel columns is drawn through each column's lowest and highest
/// point, so its cost follows the chart's width, not the number of values.
void drawChart(const char* id, const std::vector<float>& values, const ChartOptions& o, const ChartPalette& palette);
/// The size of chart() with the same options (`o.width` resolved like itemWidth(): 0 is the natural 320dp).
[[nodiscard]] ImVec2 chartSize(const ChartOptions& o);

/// The y axis of a chart: its value range (max > min as floats) and how many grid steps divide it.
struct ChartAxis {
    float min, max;
    int divisions;
};
/// The y axis chart() draws for `values` with `o` (another skin's chart uses it too): `o.min`..`o.max`, or with
/// `o.autoRange` a "nice" range around the finite values (from 0 for bars) in steps of 1, 2, 2.5 or 5 x 10^n.
[[nodiscard]] ChartAxis chartAxis(const std::vector<float>& values, const ChartOptions& o);

} // namespace imlui::md

module :private;

namespace imlui::md {

/// Computed in double: for large values a float `max = min + 1` doesn't change max, and the axis math would take
/// log10(0).
ChartAxis chartAxis(const std::vector<float>& values, const ChartOptions& o) {
    double lo = static_cast<double>(o.min), hi = static_cast<double>(o.max);
    if (o.autoRange) {
        lo = std::numeric_limits<double>::infinity();
        hi = -lo;
        for (const float v : values)
            if (std::isfinite(v)) {
                lo = std::min(lo, static_cast<double>(v));
                hi = std::max(hi, static_cast<double>(v));
            }
        if (lo > hi) lo = hi = 0.0; // no finite values
        if (o.bars) lo = std::min(lo, 0.0);
    }
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
        lo = 0.0;
        hi = 1.0;
    }
    if (hi <= lo) hi = lo + std::max(1.0, std::fabs(lo) * 1e-3);
    // A span below float precision at this magnitude would collapse when the range goes back to float.
    hi = std::max(hi, lo + std::max(std::fabs(lo), std::fabs(hi)) * 1e-5);
    int divisions = 4;
    if (o.autoRange) {
        // "Nice" axis: step of 1, 2, 2.5 or 5 x 10^n, range snapped to whole steps.
        const double raw = (hi - lo) / 4.0;
        const double mag = std::pow(10.0, std::floor(std::log10(raw)));
        const double norm = raw / mag;
        const double step = (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5 ? 2.5 : norm <= 5 ? 5 : 10) * mag;
        lo = std::floor(lo / step) * step;
        hi = std::ceil(hi / step) * step;
        if (hi <= lo) hi = lo + step;
        divisions = std::max(1, static_cast<int>(std::lround((hi - lo) / step)));
    }
    constexpr auto floatMax = static_cast<double>(std::numeric_limits<float>::max());
    if (lo < -floatMax || hi > floatMax) return {0.0f, 1.0f, 4}; // beyond float's range
    const auto mn = static_cast<float>(lo), mx = static_cast<float>(hi);
    if (mx <= mn) return {0.0f, 1.0f, 4};
    return {mn, mx, divisions};
}

ImVec2 chartSize(const ChartOptions& o) { return {itemWidth(o.width, dp(320)), o.height > 0 ? o.height : dp(160)}; }

void chart(const char* id, const std::vector<float>& values, const ChartOptions& o) {
    const Scheme& s = scheme();
    const Color c = o.color.value_or(s.primary);
    ChartPalette palette;
    palette.series = c;
    palette.title = s.onSurface;
    palette.label = s.onSurfaceVariant;
    palette.baseLine = s.outlineVariant;
    palette.gridLine = s.outlineVariant.withAlpha(0.5f);
    palette.hoverLine = s.outline.withAlpha(0.6f);
    palette.barHover = mix(c, s.onSurface, 0.15f);
    palette.areaAlpha = 0.28f;
    palette.barRadius = dp(6);
    palette.readoutStyle = TextStyle::LabelMedium;
    palette.readoutText = s.inverseOnSurface;
    palette.readoutBackground = [](ImDrawList* dl, ImVec2 a, ImVec2 b) {
        fillRoundRect(dl, a, b, dp(shape().extraSmall), scheme().inverseSurface);
    };
    drawChart(id, values, o, palette);
}

namespace {

/// The points of the line being drawn, reused from chart to chart (the UI runs on one thread).
std::vector<ImVec2> linePoints;

} // namespace

void drawChart(const char* id, const std::vector<float>& values, const ChartOptions& o, const ChartPalette& palette) {
    const ImVec2 size = chartSize(o);
    const float w = size.x, h = size.y;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The rest only draws: out of view, skip formatting the axis labels and building the series.
    if (!rectVisible(dl, p, {p.x + w, p.y + h})) return;
    const Color c = palette.series;

    const ChartAxis axis = chartAxis(values, o);
    const float mn = axis.min, mx = axis.max;
    const int divisions = axis.divisions;

    const float gutter = dp(40), top = p.y + (o.title.empty() ? dp(8) : dp(32)), bottom = p.y + h - dp(8);
    const float x0 = p.x + gutter, x1 = p.x + w - dp(8);
    if (!o.title.empty()) drawText(dl, TextStyle::TitleSmall, {p.x, p.y}, palette.title, o.title);
    // grid + labels
    for (int i = 0; i <= divisions; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(divisions);
        float y = bottom - (bottom - top) * t;
        hLine(dl, x0, x1, y, i == 0 ? palette.baseLine : palette.gridLine, dp(1));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", std::round(static_cast<double>(mn + (mx - mn) * t) * 1000.0) / 1000.0);
        ImVec2 ts = measure(TextStyle::LabelSmall, buf);
        drawText(dl, TextStyle::LabelSmall, {x0 - dp(8) - ts.x, y - ts.y * 0.5f}, palette.label, buf);
    }
    const int n = static_cast<int>(values.size());
    if (n == 0) return;
    auto px = [&](int i) {
        if (o.bars) return x0 + (x1 - x0) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        return n == 1 ? (x0 + x1) * 0.5f : x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(n - 1);
    };
    auto py = [&](float v) { return bottom - (bottom - top) * std::clamp((v - mn) / (mx - mn), 0.0f, 1.0f); };
    auto value = [&](int i) { return values[static_cast<std::size_t>(i)]; };
    // Non-finite values (NaN, inf) are gaps, as for the axis range: no bar, no point, no hover readout, and the
    // line breaks around them.
    auto finite = [&](int i) { return std::isfinite(value(i)); };

    // The hovered point: the one nearest the mouse (the earlier on a tie), found from the mouse's x rather than by
    // measuring every point, then moved to the nearest finite one.
    int hoverIdx = -1;
    if (hov) {
        const float mxp = ImGui::GetMousePos().x;
        const float span = std::max(x1 - x0, 1e-6f);
        const float at = o.bars ? (mxp - x0) / span * static_cast<float>(n) - 0.5f
                                : (n == 1 ? 0.0f : (mxp - x0) / span * static_cast<float>(n - 1));
        const int below = static_cast<int>(std::clamp(std::floor(at), 0.0f, static_cast<float>(n - 1)));
        // `below` and the next point bracket the mouse; look outwards from them for the nearest finite point.
        for (int lo = below, hi = below + 1; lo >= 0 || hi < n; --lo, ++hi) {
            const bool loOk = lo >= 0 && finite(lo), hiOk = hi < n && finite(hi);
            if (loOk && (!hiOk || std::fabs(px(lo) - mxp) <= std::fabs(px(hi) - mxp))) {
                hoverIdx = lo;
                break;
            }
            if (hiOk) {
                hoverIdx = hi;
                break;
            }
        }
    }

    if (o.bars) {
        float bw = std::max(dp(2), (x1 - x0) / static_cast<float>(n) * 0.6f);
        for (int i = 0; i < n; ++i) {
            if (!finite(i)) continue;
            float x = px(i);
            float y = py(value(i)), y0 = py(std::max(mn, 0.0f));
            fillRoundRect(dl, {x - bw * 0.5f, std::min(y, y0)}, {x + bw * 0.5f, std::max(y, y0)},
                          std::min(bw * 0.5f, palette.barRadius), i == hoverIdx ? palette.barHover : c,
                          ImDrawFlags_RoundCornersTop);
        }
    } else {
        const ImU32 cTop = col(c.withAlpha(palette.areaAlpha)), cBot = col(c.withAlpha(0.0f));
        // More points than pixel columns: each column draws its lowest and its highest point (in their order), which
        // looks the same as all of them.
        const int columns = std::max(1, static_cast<int>(std::ceil(x1 - x0)));
        const bool decimate = n > columns;
        // One polyline per run of finite points; a lone point between gaps is a dot.
        for (int i = 0; i < n;) {
            if (!finite(i)) {
                ++i;
                continue;
            }
            int end = i;
            while (end + 1 < n && finite(end + 1))
                ++end;
            if (end == i) {
                fillCircle(dl, {px(i), py(value(i))}, dp(1.5f), c);
                i = end + 1;
                continue;
            }
            linePoints.clear();
            if (!decimate) {
                for (int k = i; k <= end; ++k)
                    linePoints.push_back({px(k), py(value(k))});
            } else {
                int column = -1, lowAt = 0, highAt = 0;
                const auto flush = [&] {
                    if (column < 0) return;
                    const int first = std::min(lowAt, highAt), second = std::max(lowAt, highAt);
                    linePoints.push_back({px(first), py(value(first))});
                    if (second != first) linePoints.push_back({px(second), py(value(second))});
                };
                for (int k = i; k <= end; ++k) {
                    const int at = std::clamp(static_cast<int>(px(k) - x0), 0, columns - 1);
                    if (at != column) {
                        flush();
                        column = at;
                        lowAt = highAt = k;
                    } else if (value(k) < value(lowAt)) {
                        lowAt = k;
                    } else if (value(k) > value(highAt)) {
                        highAt = k;
                    }
                }
                flush();
            }
            if (o.fill) // a vertical gradient under the line, between neighbouring points
                for (std::size_t k = 0; k + 1 < linePoints.size(); ++k)
                    fillAreaBelow(dl, linePoints[k], linePoints[k + 1], bottom, cTop, cBot);
            dl->AddPolyline(linePoints.data(), static_cast<int>(linePoints.size()), col(c), dp(2));
            i = end + 1;
        }
    }
    if (hoverIdx >= 0) {
        float x = px(hoverIdx), y = py(value(hoverIdx));
        if (!o.bars) {
            vLine(dl, x, top, bottom, palette.hoverLine, dp(1));
            fillCircle(dl, {x, y}, dp(5), c);
            strokeCircle(dl, {x, y}, dp(6), surfaceColor(), dp(2));
        }
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.3g", static_cast<double>(value(hoverIdx)));
        const TextStyle style = palette.readoutStyle;
        ImVec2 ts = measure(style, buf);
        float bw = ts.x + dp(16), bh = dp(24);
        // Inside the chart; a chart narrower than the readout starts it at its left edge.
        const float bx = std::max(p.x, std::min(x - bw * 0.5f, p.x + w - bw)), by = std::max(p.y, y - bh - dp(10));
        if (palette.readoutBackground) palette.readoutBackground(dl, {bx, by}, {bx + bw, by + bh});
        drawText(dl, style, {bx + dp(8), textTop(style, by + bh * 0.5f)}, palette.readoutText, buf);
    }
}

} // namespace imlui::md
