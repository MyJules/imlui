// Material menus: popup styling, menu rows, dividers and anchored (dropdown-style) menus.
export module imlui.widgets.menu;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;
import imlui.widgets.anchored_popup;
import imlui.ui.layout_box;

export namespace imlui::md {

/// Push before BeginPopup/BeginMenu so the popup gets Material menu styling.
void pushMenuStyle();
void popMenuStyle();

/// One menu row (48dp). Must be inside a popup/menu. Returns true when chosen. Every row is as wide as the widest
/// (layout::claimWidth in the menu's shrink-to-fit box), so the menu shrinks back when its rows get shorter.
bool menuItem(const char* id, std::string_view label, std::string_view leadingIcon = {}, std::string_view trailing = {},
              bool selected = false, bool closeOnClick = true, bool reserveIcon = false);

/// Full-width divider between menu rows.
void menuDivider();

/// Begins a dropdown-style popup anchored to [a, b] (below it, kept inside the main viewport), at least `minWidth`
/// wide. Returns true while open; then call endAnchoredMenu(). Its content is in a shrink-to-fit layout box
/// (pushMenuBox()).
bool beginAnchoredMenu(const char* popupId, ImVec2 a, ImVec2 b, float minWidth);
void endAnchoredMenu();

/// Pushes the layout box of a menu's content, right after its popup began (endAnchoredMenu() pops it; a
/// BeginMenu() caller calls popMenuBox()): the menu takes its content's width, at least `minWidth`, and a fill (a
/// row, a divider) is the widest row, never the menu's current width.
void pushMenuBox(float minWidth);
void popMenuBox();

} // namespace imlui::md

module :private;

namespace imlui::md {

void pushMenuStyle() {
    const Scheme& s = scheme();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s.surfaceContainer.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, s.outlineVariant.withAlpha(0.5f).vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, dp(8)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(shape().extraSmall));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
}

void popMenuStyle() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

bool menuItem(const char* id, std::string_view label, std::string_view leadingIcon, std::string_view trailing,
              bool selected, bool closeOnClick, bool reserveIcon) {
    const Scheme& s = scheme();
    const float h = dp(48);
    const bool lead = hasIcon(leadingIcon) || reserveIcon;
    float natural = dp(12) + (lead ? dp(24) + dp(12) : 0.0f) + measure(TextStyle::BodyLarge, label).x + dp(12);
    if (!trailing.empty()) natural += dp(32) + measure(TextStyle::LabelLarge, trailing).x;
    natural = std::max(natural, dp(160));
    const float w = layout::claimWidth(natural);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Header, s.secondaryContainer.vec4());
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, s.onSurface.withAlpha(0.08f).vec4());
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, s.onSurface.withAlpha(0.12f).vec4());
    bool clicked = ImGui::Selectable(
        id, selected, closeOnClick ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_NoAutoClosePopups, {w, h});
    ImGui::PopStyleColor(3);
    const bool dis = itemDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    float x = p.x + dp(12);
    Color fg = dis ? disabledContent() : (selected ? s.onSecondaryContainer : s.onSurface);
    if (lead) {
        drawIcon(dl, leadingIcon, {x + dp(12), cy}, 24.0f,
                 dis ? disabledContent() : (selected ? fg : s.onSurfaceVariant));
        x += dp(24) + dp(12);
    }
    drawTextV(dl, TextStyle::BodyLarge, x, cy, fg, label);
    if (!trailing.empty()) {
        float tw = measure(TextStyle::LabelLarge, trailing).x;
        drawTextV(dl, TextStyle::LabelLarge, p.x + w - dp(12) - tw, cy, dis ? disabledContent() : s.onSurfaceVariant,
                  trailing);
    }
    return clicked;
}

void menuDivider() {
    const Scheme& s = scheme();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = layout::availableWidth();
    ImGui::Dummy({w, dp(17)});
    hLine(ImGui::GetWindowDrawList(), p.x, p.x + w, p.y + dp(8), s.outlineVariant, dp(1));
}

bool beginAnchoredMenu(const char* popupId, ImVec2 a, ImVec2 b, float minWidth) {
    const AnchoredPopup popup = placeAnchoredPopup(popupId, a, b, minWidth, dp(2));
    ImGui::SetNextWindowSizeConstraints({minWidth, 0}, {hugeExtent, dp(48) * 8 + dp(16)});
    pushMenuStyle();
    bool open = ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
    popMenuStyle();
    trackAnchoredPopup(popup, open);
    if (open) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        pushMenuBox(minWidth);
    }
    return open;
}

void endAnchoredMenu() {
    popMenuBox();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

void pushMenuBox(float minWidth) {
    // The menu style has no horizontal window padding: the content is the menu's width.
    layout::pushBox(layout::shrinkToFitBox(minWidth, hugeExtent));
}

void popMenuBox() { layout::popBox(); }

} // namespace imlui::md
