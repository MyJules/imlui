// Fluent progress: WinUI 3's ProgressBar and ProgressRing, for md::progress and md::circularProgress (same
// parameters: `fraction` < 0 is indeterminate).
//
//   progress         -> a 1dp track (the strong stroke) under a 3dp accent bar with round ends, 3dp high (WinUI's
//                       minimum height), 240dp natural width; indeterminate: two accent bars sweeping across
//   circularProgress -> an accent arc `thickness` dp wide from the top, clockwise, 32dp across unless `diameter`
//                       sets it, with no track (WinUI's ring has a transparent background); indeterminate: a
//                       spinning arc that grows and shrinks
export module imlui.skin.fluent.progress;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.ui.frame_request;
import imlui.widgets.common;

export namespace imlui::fluent {

/// A progress bar (md::progress's parameters).
void progress(const Look& look, float fraction, float width);
/// The size progress() takes with `width` (0: the natural 240dp).
[[nodiscard]] ImVec2 progressSize(const Look& look, float width);
/// A progress ring (md::circularProgress's parameters; `thickness` in dp).
void circularProgress(const Look& look, float fraction, float diameter, float thickness);
/// The size circularProgress() takes with `diameter` (0 or less: 32dp).
[[nodiscard]] ImVec2 circularProgressSize(const Look& look, float diameter);

/// The bar's height, dp (WinUI's ProgressBar minimum height).
inline constexpr float progressBarDp = 3.0f;
/// The ring's diameter when none is given, dp (WinUI's ProgressRing default).
inline constexpr float progressRingDp = 32.0f;

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float naturalWidthDp = 240.0f;
constexpr float trackDp = 1.0f;

} // namespace

ImVec2 progressSize([[maybe_unused]] const Look& look, float width) {
    return {md::itemWidth(width, dp(naturalWidthDp)), dp(progressBarDp)};
}

ImVec2 circularProgressSize([[maybe_unused]] const Look& look, float diameter) {
    const float d = diameter > 0 ? diameter : dp(progressRingDp);
    return {d, d};
}

void progress(const Look& look, float fraction, float width) {
    const Colors& c = look.colors;
    const ImVec2 size = progressSize(look, width);
    const float w = size.x, h = size.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x1 = p.x + w, cy = p.y + h * 0.5f;
    const float track = std::max(1.0f, std::round(dp(trackDp)));
    md::fillRoundRect(dl, {p.x, std::round(cy - track * 0.5f)}, {x1, std::round(cy - track * 0.5f) + track},
                      track * 0.5f, c.controlStrongStroke);
    const auto bar = [&](float a, float b) {
        a = std::max(a, p.x);
        b = std::min(b, x1);
        if (b - a > 0.5f) md::fillRoundRect(dl, {a, p.y}, {b, p.y + h}, h * 0.5f, c.accent);
    };
    if (fraction >= 0.0f) {
        bar(p.x, p.x + w * std::clamp(fraction, 0.0f, 1.0f));
        return;
    }
    requestFrame(); // indeterminate: animates continuously
    // Two bars, the second shorter and faster, sweeping in from the left (WinUI's indeterminate storyboard), in md's
    // timing: a period of 1 / 0.55 s.
    const float t = static_cast<float>(std::fmod(ImGui::GetTime() * 0.55, 1.0));
    const auto ease = [](float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3 - 2 * x);
    };
    bar(p.x + w * ease(t * 1.4f - 0.1f), p.x + w * ease(t * 1.4f + 0.25f));
    bar(p.x + w * ease(t * 1.6f - 0.9f), p.x + w * ease(t * 1.6f - 0.55f));
}

void circularProgress(const Look& look, float fraction, float diameter, float thickness) {
    const ImVec2 size = circularProgressSize(look, diameter);
    const float d = size.x;
    const float th = dp(thickness);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 center{p.x + d * 0.5f, p.y + d * 0.5f};
    const float r = d * 0.5f - th * 0.5f;
    const float top = -md::pi * 0.5f;
    const Color accent = look.colors.accent;
    const auto arc = [&](float a0, float a1) {
        if (a1 - a0 >= 0.01f && r > 0) md::strokeArc(dl, center, r, a0, a1, accent, th);
    };
    if (fraction >= 0.0f) {
        arc(top, top + std::clamp(fraction, 0.0f, 1.0f) * 2 * md::pi);
        return;
    }
    requestFrame();
    // The periodic math in double: a float time loses the sub-frame precision after a few hours.
    constexpr double turn = 2.0 * std::numbers::pi;
    const double time = ImGui::GetTime();
    const auto a0 = static_cast<float>(std::fmod(time * 5.0, turn));
    const auto wave = static_cast<float>(std::sin(std::fmod(time * 2.6, turn)));
    arc(a0, a0 + 0.35f + 0.5f * (1.0f + wave) * md::pi * 0.65f);
}

} // namespace imlui::fluent
