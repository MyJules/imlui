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

/// Where a modal drawer is in its slide, from beginModalDrawerWindow().
struct ModalDrawerFrame {
    float t = 0.0f;                   ///< how far it is open, 0..1 (animated)
    ImVec2 viewportMin, viewportMax;  ///< the main viewport, which the window covers
    ImVec2 sheetMin, sheetMax;        ///< the sheet, slid in from the left by `t` (eased)
};
/// beginModalDrawer()'s behaviour, shared with the other skins' drawers: slides the sheet (`width` dp, at most 80% of
/// the viewport) by `*open`, and begins the window over the viewport, focused when it appears. A click on the scrim
/// right of the sheet, or Escape (unless a popup over the drawer takes it), sets `*open` to false. nullopt when it
/// is closed (nothing begun). The caller draws the scrim and the sheet, then calls beginModalDrawerSheet().
[[nodiscard]] std::optional<ModalDrawerFrame> beginModalDrawerWindow(const char* id, bool* open, float width);
/// Begins the sheet's scrolling region, `padding` inside it, over `surface` (pushSurface()); endModalDrawer() ends it.
void beginModalDrawerSheet(const ModalDrawerFrame& f, float padding, Color surface);

} // namespace imlui::md

module :private;

namespace imlui::md {

std::optional<ModalDrawerFrame> beginModalDrawerWindow(const char* id, bool* open, float width) {
    const ImGuiID key = ImGui::GetID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID shownKey = key + 1;
    // animate() requests frames while the sheet slides, so on-demand rendering keeps up.
    const float t = animate(key, *open ? 1.0f : 0.0f, 16.0f);
    if (t <= 0.0f) {
        st->SetBool(shownKey, false);
        return std::nullopt;
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
    const auto name = scopedWindowId("modalDrawer");
    ImGui::Begin(name.data(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    ModalDrawerFrame f;
    f.t = t;
    f.viewportMin = vp->Pos;
    f.viewportMax = {vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y};
    f.sheetMin = {x0, vp->Pos.y};
    f.sheetMax = {x0 + sheetW, f.viewportMax.y};

    // The scrim right of the sheet closes the drawer when clicked.
    ImGui::SetCursorScreenPos({f.sheetMax.x, vp->Pos.y});
    const ImVec2 scrim{std::max(1.0f, f.viewportMax.x - f.sheetMax.x), std::max(1.0f, vp->Size.y)};
    if (ImGui::InvisibleButton("##scrim", scrim)) *open = false;
    // Escape closes it too, unless a menu or popup opened from the drawer takes the key first (or took it: ImGui's
    // nav closes a menu in NewFrame).
    if (*open && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !popupOver(ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        *open = false;
    return f;
}

void beginModalDrawerSheet(const ModalDrawerFrame& f, float padding, Color surface) {
    ImGui::SetCursorScreenPos(f.sheetMin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padding, padding});
    ImGui::BeginChild("##sheet", {f.sheetMax.x - f.sheetMin.x, f.viewportMax.y - f.viewportMin.y},
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    pushSurface(surface);
}

bool beginModalDrawer(const char* id, bool* open, float width) {
    const Scheme& s = scheme();
    const std::optional<ModalDrawerFrame> f = beginModalDrawerWindow(id, open, width);
    if (!f) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    fillRoundRect(dl, f->viewportMin, f->viewportMax, 0.0f, s.scrim.withAlpha(0.32f * f->t));
    const float radius = dp(shape().large);
    drawShadow(dl, f->sheetMin, f->sheetMax, radius, f->t);
    fillRoundRect(dl, f->sheetMin, f->sheetMax, radius, s.surfaceContainerLow, ImDrawFlags_RoundCornersRight);
    beginModalDrawerSheet(*f, dp(12), s.surfaceContainerLow);
    return true;
}

void endModalDrawer() {
    popSurface();
    ImGui::EndChild();
    notePopupOver(ImGuiPopupFlags_AnyPopup);
    ImGui::End();
}

} // namespace imlui::md
