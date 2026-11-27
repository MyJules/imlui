// Fluent modal navigation drawer: WinUI's NavigationView pane in overlay mode, for md::beginModalDrawer/endModalDrawer.
//
// Over the main viewport, the smoke color dims the app; the pane, in the flyout color with a flyoutStroke edge on its
// right and the overlay radius on its right corners, slides in from the left with a flyout shadow. Its content is a
// scrolling region with drawerPadding (4dp, scaled by the density) around it: the navigation items' own insets put
// their highlights 8dp from the pane's edge. The behavior is md's: the same window and IDs, the same slide, the scrim
// click and Escape (unless a popup over the drawer takes it) setting `*open` to false, the surface stack.
export module imlui.skin.fluent.navigation_drawer;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.widgets.common;
import imlui.widgets.surface;

export namespace imlui::fluent {

/// Begins the overlay pane (md::beginModalDrawer's parameters); returns true while its contents should be rendered,
/// then call endModalDrawer().
bool beginModalDrawer(const Look& look, const char* id, bool* open, float width);
void endModalDrawer();

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float drawerPadding = 4.0f; // dp around the pane's content
constexpr float shadowLevel = 2.0f;   // md::drawShadow's level, fully open

} // namespace

bool beginModalDrawer(const Look& look, const char* id, bool* open, float width) {
    const Colors& c = look.colors;
    const ImGuiID key = ImGui::GetID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID shownKey = key + 1;
    // animate() requests frames while the pane slides, so on-demand rendering keeps up.
    const float t = md::animate(key, *open ? 1.0f : 0.0f, 16.0f);
    if (t <= 0.0f) {
        st->SetBool(shownKey, false);
        return false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sheetW = std::min(dp(width), vp->Size.x * 0.8f);
    const float e = 1.0f - (1.0f - t) * (1.0f - t); // ease out
    const float x0 = vp->Pos.x - sheetW * (1.0f - e);

    // md's window: a top-level one over the whole viewport, focused when it appears (so above other windows).
    if (!st->GetBool(shownKey, false)) ImGui::SetNextWindowFocus();
    st->SetBool(shownKey, true);
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const std::string name = md::scopedWindowName("", "modalDrawer");
    ImGui::Begin(name.c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end{vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y};
    md::fillRoundRect(dl, vp->Pos, end, 0.0f, c.smoke.withAlpha(c.smoke.a * t));

    // The smoke right of the pane closes the drawer when clicked.
    const float sheetEnd = x0 + sheetW;
    ImGui::SetCursorScreenPos({sheetEnd, vp->Pos.y});
    if (ImGui::InvisibleButton("##scrim", {std::max(1.0f, end.x - sheetEnd), std::max(1.0f, vp->Size.y)}))
        *open = false;
    // Escape closes it too, unless a menu or popup opened from the drawer takes the key first (or took it).
    if (*open && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !md::popupOver(ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        *open = false;

    const ImVec2 a{x0, vp->Pos.y}, b{sheetEnd, end.y};
    const float radius = overlayRadius(look);
    md::drawShadow(dl, a, b, radius, shadowLevel * t);
    md::fillRoundRect(dl, a, b, radius, c.flyout, ImDrawFlags_RoundCornersRight);
    const float stroke = strokeWidth(look);
    // The pane's right edge: its outline, the left, top and bottom sides lying outside the viewport.
    if (stroke > 0)
        md::strokeRoundRect(dl, {a.x - radius - stroke, a.y - stroke}, {b.x, b.y + stroke}, radius, c.flyoutStroke,
                            stroke, ImDrawFlags_RoundCornersRight);

    ImGui::SetCursorScreenPos(a);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
    const float pad = dp(drawerPadding * look.tokens.density);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {pad, pad});
    ImGui::BeginChild("##sheet", {sheetW, vp->Size.y}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    md::pushSurface(c.flyout);
    return true;
}

void endModalDrawer() {
    md::popSurface();
    ImGui::EndChild();
    md::notePopupOver(ImGuiPopupFlags_AnyPopup);
    ImGui::End();
}

} // namespace imlui::fluent
