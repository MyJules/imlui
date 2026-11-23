// Material slider with value label while dragging.
module;

#include <algorithm>
#include <cmath>
#include <cstdio>

export module imlui.widgets.slider;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets.common;

export namespace imlui::md {

/// Material slider. `step` <= 0 means continuous. Returns true when the value changed.
bool slider(const char* id, float* v, float vmin, float vmax, float step = 0.0f, float width = 0.0f,
            const char* valueFormat = "%.2f");

} // namespace imlui::md

module :private;

namespace imlui::md {

bool slider(const char* id, float* v, float vmin, float vmax, float step, float width, const char* valueFormat) {
    const Scheme& s = scheme();
    const float w = itemWidth(width == 0.0f ? -1.0f : width, dp(200));
    const float h = dp(44);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {w, h});
    const bool dis = itemDisabled();
    ImGuiID iid = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    const float r = dp(10);
    const float x0 = p.x + r, x1 = p.x + w - r;
    bool changed = false;
    if (act && vmax > vmin) {
        float t = std::clamp((ImGui::GetMousePos().x - x0) / std::max(1.0f, x1 - x0), 0.0f, 1.0f);
        float nv = vmin + t * (vmax - vmin);
        if (step > 0) nv = vmin + std::round((nv - vmin) / step) * step;
        nv = std::clamp(nv, vmin, vmax);
        if (nv != *v) { *v = nv; changed = true; }
    }
    float t = vmax > vmin ? std::clamp((*v - vmin) / (vmax - vmin), 0.0f, 1.0f) : 0.0f;
    float tv = animate(iid + 2, t, 30.0f);

    Color active = dis ? s.onSurface.withAlpha(0.38f) : s.primary;
    Color inactive = dis ? s.onSurface.withAlpha(0.12f) : s.secondaryContainer;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f, th = dp(4);
    const float hx = x0 + (x1 - x0) * tv;
    dl->AddRectFilled({x0, cy - th * 0.5f}, {x1, cy + th * 0.5f}, col(inactive), th);
    if (hx > x0) dl->AddRectFilled({x0, cy - th * 0.5f}, {hx, cy + th * 0.5f}, col(active), th);
    if (step > 0 && vmax > vmin && (vmax - vmin) / step <= 40) {
        for (float sv = vmin; sv <= vmax + step * 0.5f; sv += step) {
            float sx = x0 + (x1 - x0) * ((sv - vmin) / (vmax - vmin));
            dl->AddCircleFilled({sx, cy}, dp(1),
                                col(sx <= hx ? s.onPrimary.withAlpha(0.7f) : s.onSecondaryContainer.withAlpha(0.7f)));
        }
    }
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddCircleFilled({hx, cy}, dp(20), col(s.primary.withAlpha(layer * 1.5f)));
    dl->AddCircleFilled({hx, cy}, r * (act ? 1.1f : 1.0f), col(active));

    if (act && valueFormat && *valueFormat) {
        char buf[64];
        std::snprintf(buf, sizeof buf, valueFormat, static_cast<double>(*v));
        ImVec2 ts = measure(TextStyle::LabelMedium, buf);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float bw = std::max(ts.x + dp(16), dp(28)), bh = dp(28);
        ImVec2 ba{hx - bw * 0.5f, cy - r - dp(8) - bh};
        fg->AddRectFilled(ba, {ba.x + bw, ba.y + bh}, col(s.inverseSurface), bh * 0.5f);
        drawText(fg, TextStyle::LabelMedium, {hx - ts.x * 0.5f, ba.y + (bh - ts.y) * 0.5f}, s.inverseOnSurface, buf);
    }
    return changed;
}

} // namespace imlui::md
