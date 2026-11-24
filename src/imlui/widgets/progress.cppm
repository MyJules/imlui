// Linear and circular progress indicators (determinate and indeterminate).
export module imlui.widgets.progress;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.widgets.common;
import imlui.ui.frame_request;

export namespace imlui::md {

/// Linear progress (M3 2024: gap + stop indicator). `fraction` < 0 = indeterminate.
void progress(float fraction, float width = -1.0f);

/// Circular progress. `fraction` < 0 = indeterminate.
void circularProgress(float fraction, float diameter = 0.0f, float thickness = 4.0f);

} // namespace imlui::md

module :private;

namespace imlui::md {

void progress(float fraction, float width) {
    const Scheme& s = scheme();
    const float w = itemWidth(width, dp(240)), h = dp(4), gap = dp(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({w, h + dp(8)});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = p.y + dp(4), x1 = p.x + w;
    auto seg = [&](float a, float b, Color c) {
        if (b - a > 0.5f) fillRoundRect(dl, {a, y}, {b, y + h}, h * 0.5f, c);
    };
    if (fraction >= 0.0f) {
        float ax = p.x + w * std::clamp(fraction, 0.0f, 1.0f);
        seg(p.x, ax, s.primary);
        seg(fraction > 0.0f ? ax + gap : p.x, x1, s.secondaryContainer);
        if (fraction < 1.0f) fillCircle(dl, {x1 - h * 0.5f, y + h * 0.5f}, h * 0.5f, s.primary);
    } else {
        requestFrame(); // indeterminate: animates continuously
        float t = static_cast<float>(std::fmod(ImGui::GetTime() * 0.55, 1.0));
        auto ease = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); };
        float a1 = ease(t * 1.4f - 0.1f), b1 = ease(t * 1.4f + 0.25f);
        float a2 = ease(t * 1.6f - 0.9f), b2 = ease(t * 1.6f - 0.55f);
        float xa1 = p.x + w * a1, xb1 = p.x + w * b1, xa2 = p.x + w * a2, xb2 = p.x + w * b2;
        // track pieces around the two moving segments
        seg(p.x, std::min(xa2, xa1) - gap, s.secondaryContainer);
        seg(xb2 + gap, xa1 - gap, s.secondaryContainer);
        seg(xb1 + gap, x1, s.secondaryContainer);
        seg(xa1, xb1, s.primary);
        seg(xa2, xb2, s.primary);
    }
}

void circularProgress(float fraction, float diameter, float thickness) {
    const Scheme& s = scheme();
    const float d = diameter > 0 ? diameter : dp(48);
    const float th = dp(thickness);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({d, d});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c{p.x + d * 0.5f, p.y + d * 0.5f};
    const float r = d * 0.5f - th * 0.5f - dp(2);
    const float gapA = (th + dp(4)) / r;
    const float top = -pi * 0.5f;
    auto arc = [&](float a0, float a1, Color cl) {
        if (a1 - a0 < 0.01f) return;
        strokeArc(dl, c, r, a0, a1, cl, th);
    };
    if (fraction >= 0.0f) {
        float f = std::clamp(fraction, 0.0f, 1.0f);
        float end = top + f * 2 * pi;
        arc(top, end, s.primary);
        if (f < 1.0f) arc(end + gapA, top + 2 * pi - (f > 0 ? gapA : 0.0f), s.secondaryContainer);
    } else {
        requestFrame();
        // The periodic math in double: a float time loses the sub-frame precision after a few hours.
        constexpr double turn = 2.0 * std::numbers::pi;
        const double t = ImGui::GetTime();
        const auto a0 = static_cast<float>(std::fmod(t * 5.0, turn));
        const auto wave = static_cast<float>(std::sin(std::fmod(t * 2.6, turn)));
        const float span = 0.35f + 0.5f * (1.0f + wave) * pi * 0.65f;
        arc(a0, a0 + span, s.primary);
    }
}

} // namespace imlui::md
