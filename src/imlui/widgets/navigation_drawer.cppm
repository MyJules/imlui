// Material modal navigation drawer: a sheet that slides in from the left over a scrim, above the app content.
export module imlui.widgets.navigation_drawer;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.widgets.common;
import imlui.widgets.surface;

export namespace imlui::md {

/// Modal navigation drawer driven by `*open`. It covers the main viewport with a 32% scrim and slides a
/// surfaceContainerLow sheet, `width` dp wide (at most 80% of the viewport), in from the left. Clicking the scrim
/// or pressing Escape sets `*open` to false; the sheet then slides out. Returns true while the sheet's contents
/// should be rendered (a scrolling region with 12dp padding); then call endModalDrawer().
bool beginModalDrawer(const char* id, bool* open, float width = 360.0f);
void endModalDrawer();

} // namespace imlui::md

module :private;

namespace imlui::md {

bool beginModalDrawer(const char* id, bool* open, float width) {
    const Scheme& s = scheme();
    const ImGuiID key = ImGui::GetID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID shownKey = key + 1;
    // animate() requests frames while the sheet slides, so on-demand rendering keeps up.
    const float t = animate(key, *open ? 1.0f : 0.0f, 16.0f);
    if (t <= 0.0f) {
        st->SetBool(shownKey, false);
        return false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sheetW = std::min(dp(width), vp->Size.x * 0.8f);
    // Ease out: fast at first, settling into place.
    const float e = 1.0f - (1.0f - t) * (1.0f - t);
    const float x0 = vp->Pos.x - sheetW * (1.0f - e);

    // A top-level window over the whole viewport. The ApplicationWindow never comes to the front, so this one
    // stays above the app content; it takes focus when it appears, which also moves it above other windows.
    if (!st->GetBool(shownKey, false)) ImGui::SetNextWindowFocus();
    st->SetBool(shownKey, true);
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const std::string name = scopedWindowName("", "modalDrawer");
    ImGui::Begin(name.c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end{vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y};
    fillRoundRect(dl, vp->Pos, end, 0.0f, s.scrim.withAlpha(0.32f * t));

    // The scrim right of the sheet closes the drawer when clicked.
    const float sheetEnd = x0 + sheetW;
    ImGui::SetCursorScreenPos({sheetEnd, vp->Pos.y});
    if (ImGui::InvisibleButton("##scrim", {std::max(1.0f, end.x - sheetEnd), std::max(1.0f, vp->Size.y)}))
        *open = false;
    // Escape closes it too, unless a menu or popup opened from the drawer takes the key first (or took it: ImGui's
    // nav closes a menu in NewFrame).
    if (*open && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !popupOver(ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        *open = false;

    const ImVec2 a{x0, vp->Pos.y}, b{sheetEnd, end.y};
    const float radius = dp(shape().large);
    drawShadow(dl, a, b, radius, t);
    fillRoundRect(dl, a, b, radius, s.surfaceContainerLow, ImDrawFlags_RoundCornersRight);

    ImGui::SetCursorScreenPos(a);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(12), dp(12)});
    ImGui::BeginChild("##sheet", {sheetW, vp->Size.y}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    pushSurface(s.surfaceContainerLow);
    return true;
}

void endModalDrawer() {
    popSurface();
    ImGui::EndChild();
    notePopupOver(ImGuiPopupFlags_AnyPopup);
    ImGui::End();
}

} // namespace imlui::md
