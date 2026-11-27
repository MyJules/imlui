// Snackbar: brief message bar at the bottom of the main viewport.
export module imlui.widgets.snackbar;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets.common;
import imlui.widgets.surface;
import imlui.widgets.button;

export namespace imlui::md {

/// Draws a snackbar at the bottom of the viewport. `t` in [0, 1] is its
/// show animation. Returns true when the action was clicked. A message wider than the bar leaves for it (the bar's
/// width less the padding and the actions) wraps there: the bar grows from 48dp by 20dp a line (68dp for two
/// lines), and the actions stay on the right, centered vertically.
bool snackbar(std::string_view message, std::string_view action, float t, bool closeIcon, bool* closed);

/// The snackbar's width for `content` pixels (the message, the actions and the padding) on a viewport
/// `viewportWidth` wide: within 344-672dp, and at most the viewport less 16dp on each side, which wins.
[[nodiscard]] float snackbarWidth(float content, float viewportWidth);

/// The lines `message` takes in a snackbar with `width` pixels for it (word-wrapped, and at each newline).
[[nodiscard]] std::vector<std::string_view> snackbarLines(std::string_view message, float width);

} // namespace imlui::md

module :private;

namespace imlui::md {

std::vector<std::string_view> snackbarLines(std::string_view message, float width) {
    std::vector<std::string_view> lines;
    ImFont* f = font(TextStyle::BodyMedium);
    const float size = fontPixels(TextStyle::BodyMedium);
    const char* p = message.data();
    const char* const end = p + message.size();
    while (p < end) {
        const char* const lineEnd = std::find(p, end, '\n');
        const char* e = f ? f->CalcWordWrapPosition(size, p, lineEnd, std::max(1.0f, width)) : lineEnd;
        if (e <= p || e > lineEnd) e = lineEnd; // nothing fits (no font): the rest of the line
        const char* last = e;
        while (last > p && (last[-1] == ' ' || last[-1] == '\t'))
            --last;
        lines.emplace_back(p, static_cast<std::size_t>(last - p));
        p = e;
        while (p < lineEnd && (*p == ' ' || *p == '\t'))
            ++p;                          // a wrapped line doesn't start with a blank
        if (p == lineEnd && p < end) ++p; // the newline
    }
    if (lines.empty()) lines.emplace_back();
    return lines;
}

float snackbarWidth(float content, float viewportWidth) {
    const float most = std::min(dp(672), viewportWidth - dp(32));
    return std::max(1.0f, std::min(most, std::max(dp(344), content)));
}

bool snackbar(std::string_view message, std::string_view action, float t, bool closeIcon, bool* closed) {
    if (t <= 0.001f) return false;
    const Scheme& s = scheme();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ease = 1.0f - (1.0f - t) * (1.0f - t);
    ImGui::SetNextWindowPos(
        {vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - dp(16) + dp(24) * (1 - ease)},
        ImGuiCond_Always, {0.5f, 1.0f});
    // The width comes from the content, not from the window: the message, then the gaps and the actions (`right`;
    // the gaps are explicit, not the theme's ItemSpacing), plus the padding, within 344-672dp (less on a narrow
    // viewport, which wins). A spacer pushes the actions to the right edge.
    const float padX = dp(16);
    const float actionGap = dp(8), closeGap = 0.0f; // the close button's 48 dp target already has its inset
    const float msgW = measure(TextStyle::BodyMedium, message).x;
    float right = 0;
    if (!action.empty()) right += actionGap + measure(TextStyle::LabelLarge, action).x + dp(24);
    if (closeIcon) right += closeGap + dp(48);
    const float width = snackbarWidth(msgW + right + 2.0f * padX, vp->WorkSize.x);
    ImGui::SetNextWindowSize({width, 0.0f}); // 0: the height fits the content
    ImGui::PushStyleColor(ImGuiCol_WindowBg, s.inverseSurface.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(shape().extraSmall));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padX, dp(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {dp(8), 0});
    const auto name = scopedWindowId("snackbar");
    ImGui::Begin(name.data(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor();
    pushSurface(s.inverseSurface);
    bool clicked = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The message wraps in what the bar leaves for it (only when the bar is at its widest): 48dp for one line, 20dp
    // more for each further one (M3: 68dp for two), with 14dp above and below the lines.
    const float msgRoom = std::max(dp(48), width - 2.0f * padX - right);
    const std::vector<std::string_view> lines = snackbarLines(message, msgW > msgRoom ? msgRoom : hugeExtent);
    const float msgBoxW = std::min(msgW, msgRoom);
    const float lineH = dp(20);
    const float h = std::max(dp(48), static_cast<float>(lines.size()) * lineH + 2.0f * dp(14));
    const float lineY = ImGui::GetCursorPosY();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({msgBoxW, h});
    const float firstCenter = lines.size() == 1 ? h * 0.5f : dp(14) + lineH * 0.5f;
    for (std::size_t i = 0; i < lines.size(); ++i)
        drawTextV(dl, TextStyle::BodyMedium, p.x, p.y + firstCenter + lineH * static_cast<float>(i),
                  s.inverseOnSurface.withAlpha(ease), lines[i]);
    const float spacer = width - 2.0f * padX - msgBoxW - right;
    if (spacer > 0) {
        ImGui::SameLine(0, 0);
        ImGui::Dummy({spacer, 1});
    }
    if (!action.empty()) {
        ImGui::SameLine(0, actionGap);
        ImGui::SetCursorPosY(lineY + (h - dp(40)) * 0.5f);
        // Text button in inversePrimary. An invisible button draws no label, so its ID needn't carry the action.
        ImVec2 bp = ImGui::GetCursorScreenPos();
        float bw = measure(TextStyle::LabelLarge, action).x + dp(24);
        clicked = navButton("###snack_action", {bw, dp(40)});
        float layer = stateLayer(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive());
        if (focusVisible()) focusRing(dl, bp, {bp.x + bw, bp.y + dp(40)}, dp(20));
        if (layer > 0) fillRoundRect(dl, bp, {bp.x + bw, bp.y + dp(40)}, dp(20), s.inversePrimary.withAlpha(layer));
        drawTextV(dl, TextStyle::LabelLarge, bp.x + dp(12), bp.y + dp(20), s.inversePrimary.withAlpha(ease), action);
    }
    if (closeIcon) {
        ImGui::SameLine(0, closeGap);
        ImGui::SetCursorPosY(lineY + (h - dp(48)) * 0.5f);
        if (iconButton("##snack_close", "close", IconButtonVariant::Standard, false, false, s.inverseOnSurface) &&
            closed)
            *closed = true;
    }
    popSurface();
    ImGui::End();
    return clicked;
}

} // namespace imlui::md
