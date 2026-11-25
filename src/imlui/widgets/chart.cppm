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
/// The size of chart() with the same options (`o.width` resolved like itemWidth(): 0 is the natural 320dp).
[[nodiscard]] ImVec2 chartSize(const ChartOptions& o);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

struct AxisRange {
    float min, max;
    int divisions;
};

/// The value range of the y axis, always with max > min as floats. Computed in double: for large values a float
/// `max = min + 1` doesn't change max, and the axis math would take log10(0).
AxisRange axisRange(const std::vector<float>& values, const ChartOptions& o) {
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

} // namespace

ImVec2 chartSize(const ChartOptions& o) { return {itemWidth(o.width, dp(320)), o.height > 0 ? o.height : dp(160)}; }

void chart(const char* id, const std::vector<float>& values, const ChartOptions& o) {
    const Scheme& s = scheme();
    const ImVec2 size = chartSize(o);
    const float w = size.x, h = size.y;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The rest only draws: out of view, skip formatting the axis labels and building the series.
    if (!rectVisible(dl, p, {p.x + w, p.y + h})) return;
    const Color c = o.color.value_or(s.primary);

    const AxisRange axis = axisRange(values, o);
    const float mn = axis.min, mx = axis.max;
    const int divisions = axis.divisions;

    const float gutter = dp(40), top = p.y + (o.title.empty() ? dp(8) : dp(32)), bottom = p.y + h - dp(8);
    const float x0 = p.x + gutter, x1 = p.x + w - dp(8);
    if (!o.title.empty()) drawText(dl, TextStyle::TitleSmall, {p.x, p.y}, s.onSurface, o.title);
    // grid + labels
    for (int i = 0; i <= divisions; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(divisions);
        float y = bottom - (bottom - top) * t;
        hLine(dl, x0, x1, y, s.outlineVariant.withAlpha(i == 0 ? 1.0f : 0.5f), dp(1));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", std::round(static_cast<double>(mn + (mx - mn) * t) * 1000.0) / 1000.0);
        ImVec2 ts = measure(TextStyle::LabelSmall, buf);
        drawText(dl, TextStyle::LabelSmall, {x0 - dp(8) - ts.x, y - ts.y * 0.5f}, s.onSurfaceVariant, buf);
    }
    const int n = static_cast<int>(values.size());
    if (n == 0) return;
    auto px = [&](int i) {
        if (o.bars) return x0 + (x1 - x0) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        return n == 1 ? (x0 + x1) * 0.5f : x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(n - 1);
    };
    auto py = [&](float v) { return bottom - (bottom - top) * std::clamp((v - mn) / (mx - mn), 0.0f, 1.0f); };
    // Non-finite values (NaN, inf) are gaps, as for the axis range: no bar, no point, no hover readout, and the
    // line breaks around them.
    auto finite = [&](int i) { return std::isfinite(values[static_cast<std::size_t>(i)]); };

    int hoverIdx = -1;
    if (hov) {
        float mxp = ImGui::GetMousePos().x;
        float best = hugeExtent;
        for (int i = 0; i < n; ++i)
            if (finite(i) && std::fabs(px(i) - mxp) < best) {
                best = std::fabs(px(i) - mxp);
                hoverIdx = i;
            }
    }

    if (o.bars) {
        float bw = std::max(dp(2), (x1 - x0) / static_cast<float>(n) * 0.6f);
        for (int i = 0; i < n; ++i) {
            if (!finite(i)) continue;
            float x = px(i);
            float y = py(values[static_cast<std::size_t>(i)]), y0 = py(std::max(mn, 0.0f));
            Color bc = i == hoverIdx ? mix(c, s.onSurface, 0.15f) : c;
            fillRoundRect(dl, {x - bw * 0.5f, std::min(y, y0)}, {x + bw * 0.5f, std::max(y, y0)},
                          std::min(bw * 0.5f, dp(6)), bc, ImDrawFlags_RoundCornersTop);
        }
    } else {
        std::vector<ImVec2> pts(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            if (finite(i)) pts[static_cast<std::size_t>(i)] = {px(i), py(values[static_cast<std::size_t>(i)])};
        if (o.fill && n >= 2) {
            // Vertical gradient under the line, between neighbouring finite points.
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const ImU32 cTop = col(c.withAlpha(0.28f)), cBot = col(c.withAlpha(0.0f));
            for (int i = 0; i + 1 < n; ++i) {
                if (!finite(i) || !finite(i + 1)) continue;
                ImVec2 a = pts[static_cast<std::size_t>(i)], b = pts[static_cast<std::size_t>(i + 1)];
                dl->PrimReserve(6, 4);
                auto base = static_cast<unsigned>(dl->_VtxCurrentIdx);
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
                dl->AddPolyline(&pts[static_cast<std::size_t>(i)], end - i + 1, col(c), dp(2));
            else
                fillCircle(dl, pts[static_cast<std::size_t>(i)], dp(1.5f), c);
            i = end + 1;
        }
    }
    if (hoverIdx >= 0) {
        float x = px(hoverIdx), y = py(values[static_cast<std::size_t>(hoverIdx)]);
        if (!o.bars) {
            vLine(dl, x, top, bottom, s.outline.withAlpha(0.6f), dp(1));
            fillCircle(dl, {x, y}, dp(5), c);
            strokeCircle(dl, {x, y}, dp(6), surfaceColor(), dp(2));
        }
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.3g", static_cast<double>(values[static_cast<std::size_t>(hoverIdx)]));
        ImVec2 ts = measure(TextStyle::LabelMedium, buf);
        float bw = ts.x + dp(16), bh = dp(24);
        // Inside the chart; a chart narrower than the readout starts it at its left edge.
        const float bx = std::max(p.x, std::min(x - bw * 0.5f, p.x + w - bw)), by = std::max(p.y, y - bh - dp(10));
        fillRoundRect(dl, {bx, by}, {bx + bw, by + bh}, dp(shape().extraSmall), s.inverseSurface);
        drawText(dl, TextStyle::LabelMedium, {bx + dp(8), textTop(TextStyle::LabelMedium, by + bh * 0.5f)},
                 s.inverseOnSurface, buf);
    }
}

} // namespace imlui::md
