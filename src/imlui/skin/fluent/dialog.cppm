// Fluent dialog: WinUI's ContentDialog. A modal over the smoke fill, the layer fill with a flyout-stroke border of the
// stroke width, the overlay radius, 24dp padding, 320-548dp wide (or `width`), the title in titleLarge (WinUI's
// subtitle style) 12dp above the content; a hero icon (which ContentDialog doesn't have) is centered above the title
// in the accent color. A dialog's buttons are its children (Button elements), so they draw through fluent::button.
//
// Everything else is md::beginDialog's: `*open` drives the popup, Escape closes the top-most popup only, the content
// renders in a shrink-to-fit layout box at least as wide as the title, and the dialog pushes its fill as the surface.
export module imlui.skin.fluent.dialog;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.ui.layout_box;
import imlui.widgets.common;
import imlui.widgets.surface;
import imlui.widgets.text;

export namespace imlui::fluent {

/// Opens/closes a dialog driven by `*open` (md::beginDialog's parameters). Returns true while its content should be
/// rendered; then call endDialog().
bool beginDialog(const Look& look, const char* title, bool* open, float width, std::string_view heroIcon);
void endDialog(const Look& look);

/// ContentDialog's width limits, dp.
inline constexpr float dialogMinWidth = 320.0f, dialogMaxWidth = 548.0f;
/// ContentDialog's padding, dp.
inline constexpr float dialogPadding = 24.0f;
/// The title's style (WinUI's SubtitleTextBlockStyle, 20px semibold, is closest to titleLarge).
inline constexpr TextStyle dialogTitleStyle = TextStyle::TitleLarge;

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

bool beginDialog(const Look& look, const char* title, bool* open, float width, std::string_view heroIcon) {
    const Colors& c = look.colors;
    if (*open && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    const float stroke = strokeWidth(look);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, c.layer.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, c.flyoutStroke.vec4());
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, c.smoke.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, overlayRadius(look));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(dialogPadding), dp(dialogPadding)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, stroke);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, stroke);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f}, ImGuiCond_Always,
                            {0.5f, 0.5f});
    const float minW = width > 0 ? width : dp(dialogMinWidth), maxW = width > 0 ? width : dp(dialogMaxWidth);
    ImGui::SetNextWindowSizeConstraints({minW, 0}, {maxW, vp->Size.y * 0.9f});
    const bool shown = ImGui::BeginPopupModal(title, nullptr,
                                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
    if (!shown) {
        *open = false;
        return false;
    }
    // Escape closes the top-most popup only (see md::beginDialog).
    if (!*open || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !md::popupOver(ImGuiPopupFlags_AnyPopupId))) {
        *open = false;
        ImGui::CloseCurrentPopup();
        md::notePopupOver(ImGuiPopupFlags_AnyPopupId);
        ImGui::EndPopup();
        return false;
    }
    md::pushSurface(c.layer);
    const float pad = 2.0f * dp(dialogPadding);
    layout::pushBox(layout::shrinkToFitBox(minW - pad, maxW - pad));
    const bool hero = hasIcon(heroIcon);
    const std::string_view t = md::visibleLabel(title);
    if (!t.empty()) layout::claimWidth(md::measure(dialogTitleStyle, t).x);
    if (hero) {
        const float avail = layout::availableWidth();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - dp(24)) * 0.5f);
        md::icon(heroIcon, 24.0f, c.accent);
        ImGui::Dummy({0, dp(4)});
    }
    if (!t.empty()) {
        md::text(t, dialogTitleStyle, c.text, false, hero ? 0.5f : 0.0f);
        ImGui::Dummy({0, dp(12)});
    }
    return true;
}

void endDialog(const Look&) {
    layout::popBox();
    md::popSurface();
    md::notePopupOver(ImGuiPopupFlags_AnyPopupId);
    ImGui::EndPopup();
}

} // namespace imlui::fluent
