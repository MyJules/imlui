// Material basic dialog (modal popup centered in the main viewport).
export module imlui.widgets.dialog;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;
import imlui.widgets.surface;
import imlui.widgets.text;
import imlui.ui.layout_box;

export namespace imlui::md {

/// Opens/closes a Material dialog driven by `*open`. Returns true while the
/// contents should be rendered; then call endDialog().
///
/// The dialog takes its content's width, 280-560dp (or `width`). The content is in a shrink-to-fit layout box
/// (layout::shrinkToFitBox): a fill is as wide as the minimum width or the title, whichever is wider, never the
/// dialog's current width, so the dialog shrinks back when its content does; text wraps at the maximum width.
bool beginDialog(const char* title, bool* open, float width = 0.0f, std::string_view heroIcon = {});
void endDialog();

} // namespace imlui::md

module :private;

namespace imlui::md {

bool beginDialog(const char* title, bool* open, float width, std::string_view heroIcon) {
    const Scheme& s = scheme();
    if (*open && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s.surfaceContainerHigh.vec4());
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, s.scrim.withAlpha(0.32f).vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(shape().extraLarge));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(24), dp(24)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f}, ImGuiCond_Always,
                            {0.5f, 0.5f});
    const float minW = width > 0 ? width : dp(280), maxW = width > 0 ? width : dp(560);
    ImGui::SetNextWindowSizeConstraints({minW, 0}, {maxW, vp->Size.y * 0.9f});
    bool shown = ImGui::BeginPopupModal(title, nullptr,
                                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    if (!shown) {
        *open = false;
        return false;
    }
    // Escape closes the top-most popup only: not this dialog while a nested dialog or a menu opened from it is open,
    // nor when the press closed such a menu (ImGui's nav did, in NewFrame).
    if (!*open || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !popupOver(ImGuiPopupFlags_AnyPopupId))) {
        *open = false;
        ImGui::CloseCurrentPopup();
        notePopupOver(ImGuiPopupFlags_AnyPopupId); // nothing is over it now: no stale record when it opens again
        ImGui::EndPopup();
        return false;
    }
    pushSurface(s.surfaceContainerHigh);
    const float pad = 2.0f * dp(24);
    layout::pushBox(layout::shrinkToFitBox(minW - pad, maxW - pad));
    const bool hero = hasIcon(heroIcon);
    std::string_view t = visibleLabel(title);
    // A fill is at least as wide as the title, which the hero icon and the title center over.
    if (!t.empty()) layout::claimWidth(measure(TextStyle::HeadlineSmall, t).x);
    if (hero) {
        const float avail = layout::availableWidth();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - dp(24)) * 0.5f);
        icon(heroIcon, 24.0f, s.secondary);
        ImGui::Dummy({0, dp(4)});
    }
    if (!t.empty()) {
        text(t, TextStyle::HeadlineSmall, s.onSurface, false, hero ? 0.5f : 0.0f);
        ImGui::Dummy({0, dp(8)});
    }
    return true;
}

void endDialog() {
    layout::popBox();
    popSurface();
    notePopupOver(ImGuiPopupFlags_AnyPopupId);
    ImGui::EndPopup();
}

} // namespace imlui::md
