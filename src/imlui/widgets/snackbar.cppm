// Snackbar: brief message bar at the bottom of the main viewport.
module;

#include <algorithm>
#include <string>
#include <string_view>

export module imlui.widgets.snackbar;

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
/// show animation. Returns true when the action was clicked.
bool snackbar(std::string_view message, std::string_view action, float t, bool closeIcon, bool* closed);

} // namespace imlui::md

module :private;

namespace imlui::md {

bool snackbar(std::string_view message, std::string_view action, float t, bool closeIcon, bool* closed) {
    if (t <= 0.001f) return false;
    const Scheme& s = scheme();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ease = 1.0f - (1.0f - t) * (1.0f - t);
    ImGui::SetNextWindowPos(
        {vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - dp(16) + dp(24) * (1 - ease)},
        ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowSizeConstraints({dp(344), 0}, {std::min(dp(672), vp->WorkSize.x - dp(32)), hugeExtent});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, s.inverseSurface.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(shape().extraSmall));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(16), dp(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {dp(8), 0});
    ImGui::Begin("##imlui_snackbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor();
    pushSurface(s.inverseSurface);
    bool clicked = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = dp(48);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float msgW = measure(TextStyle::BodyMedium, message).x;
    ImGui::Dummy({msgW, h});
    drawTextV(dl, TextStyle::BodyMedium, p.x, p.y + h * 0.5f, s.inverseOnSurface.withAlpha(ease), message);
    // Push the actions to the right edge of the (min-width) bar.
    float right = 0;
    if (!action.empty()) right += measure(TextStyle::LabelLarge, action).x + dp(24) + dp(8);
    if (closeIcon) right += dp(48);
    float avail = std::max(dp(344) - dp(32), ImGui::GetWindowWidth() - dp(32));
    float spacer = avail - msgW - right;
    if (spacer > 0) {
        ImGui::SameLine(0, 0);
        ImGui::Dummy({spacer, 1});
    }
    if (!action.empty()) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (h - dp(40)) * 0.5f);
        std::string a{action};
        a += "###snack_action";
        // Text button in inversePrimary.
        ImVec2 bp = ImGui::GetCursorScreenPos();
        float bw = measure(TextStyle::LabelLarge, action).x + dp(24);
        clicked = ImGui::InvisibleButton(a.c_str(), {bw, dp(40)});
        float layer = stateLayer(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive());
        if (layer > 0) dl->AddRectFilled(bp, {bp.x + bw, bp.y + dp(40)}, col(s.inversePrimary.withAlpha(layer)), dp(20));
        drawTextV(dl, TextStyle::LabelLarge, bp.x + dp(12), bp.y + dp(20), s.inversePrimary.withAlpha(ease), action);
    }
    if (closeIcon) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY());
        if (iconButton("##snack_close", "close", IconButtonVariant::Standard, false, false, s.inverseOnSurface) &&
            closed)
            *closed = true;
    }
    popSurface();
    ImGui::End();
    return clicked;
}

} // namespace imlui::md
