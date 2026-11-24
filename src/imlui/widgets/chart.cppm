// Line / bar chart with "nice" axis, grid, gradient area and hover readout.
module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

export module imlui.widgets.chart;

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

} // namespace imlui::md

module :private;

namespace imlui::md {

void chart(const char* id, const std::vector<float>& values, const ChartOptions& o) {
    const Scheme& s = scheme();
    const float w = itemWidth(o.width, dp(320));
    const float h = o.height > 0 ? o.height : dp(160);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {w, h});
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color c = o.color.value_or(s.primary);

    float mn = o.min, mx = o.max;
    if (o.autoRange && !values.empty()) {
        mn = *std::min_element(values.begin(), values.end());
        mx = *std::max_element(values.begin(), values.end());
        if (o.bars) mn = std::min(mn, 0.0f);
    }
    if (mx <= mn) mx = mn + 1;
    // "Nice" axis: step of 1, 2, 2.5 or 5 x 10^n, range snapped to whole steps.
    int divisions = 4;
    if (o.autoRange) {
        float raw = (mx - mn) / 4.0f;
        float mag = std::pow(10.0f, std::floor(std::log10(raw)));
        float norm = raw / mag;
        float step = (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5f ? 2.5f : norm <= 5 ? 5 : 10) * mag;
        mn = std::floor(mn / step) * step;
        mx = std::ceil(mx / step) * step;
        if (mx <= mn) mx = mn + step;
        divisions = std::max(1, static_cast<int>(std::lround((mx - mn) / step)));
    }

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

    int hoverIdx = -1;
    if (hov) {
        float mxp = ImGui::GetMousePos().x;
        float best = hugeExtent;
        for (int i = 0; i < n; ++i)
            if (std::fabs(px(i) - mxp) < best) { best = std::fabs(px(i) - mxp); hoverIdx = i; }
    }

    if (o.bars) {
        float bw = std::max(dp(2), (x1 - x0) / static_cast<float>(n) * 0.6f);
        for (int i = 0; i < n; ++i) {
            float x = px(i);
            float y = py(values[static_cast<std::size_t>(i)]), y0 = py(std::max(mn, 0.0f));
            Color bc = i == hoverIdx ? mix(c, s.onSurface, 0.15f) : c;
            fillRoundRect(dl, {x - bw * 0.5f, std::min(y, y0)}, {x + bw * 0.5f, std::max(y, y0)},
                          std::min(bw * 0.5f, dp(6)), bc, ImDrawFlags_RoundCornersTop);
        }
    } else {
        std::vector<ImVec2> pts(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) pts[static_cast<std::size_t>(i)] = {px(i), py(values[static_cast<std::size_t>(i)])};
        if (o.fill && n >= 2) {
            // Vertical gradient under the line.
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const ImU32 cTop = col(c.withAlpha(0.28f)), cBot = col(c.withAlpha(0.0f));
            for (int i = 0; i + 1 < n; ++i) {
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
        dl->AddPolyline(pts.data(), n, col(c), dp(2));
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
        float bx = std::clamp(x - bw * 0.5f, p.x, p.x + w - bw), by = std::max(p.y, y - bh - dp(10));
        fillRoundRect(dl, {bx, by}, {bx + bw, by + bh}, dp(shape().extraSmall), s.inverseSurface);
        drawText(dl, TextStyle::LabelMedium, {bx + dp(8), textTop(TextStyle::LabelMedium, by + bh * 0.5f)},
                 s.inverseOnSurface, buf);
    }
}

} // namespace imlui::md
