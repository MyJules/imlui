// Fluent menus: WinUI's MenuFlyout, for md::menuItem, md::beginMenu/endMenu and md::beginAnchoredMenu/endAnchoredMenu.
//
// The flyout: the flyout color, a flyoutStroke border of the stroke width, the overlay radius, 2dp above and below the
// rows (no side padding: the rows are the menu's width, like md's), a shadow under an anchored menu. A row:
// compactHeight + 8dp (32dp; the 8dp scaled by the density), its highlight inside it (plateInsetX/plateInsetY) with
// the control radius and the subtle fills; a paddingX, the icon slot (iconSize + gap), the body label, and the
// shortcut in the caption style and textSecondary, right-aligned 24dp after the label at the least. A row is at least
// 160dp wide, md's.
//
// The behavior is md's: the same ImGui popups and IDs, ImGui::Selectable rows (closing the menu unless
// `closeOnClick` is false), the same shrink-to-fit menu box (md::pushMenuBox) and the anchored placement (below the
// anchor, kept inside the main viewport; at most 8 rows high, then scrolling).
export module imlui.skin.fluent.menu;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.icons;
import imlui.theme.typography;
import imlui.ui.layout_box;
import imlui.widgets.anchored_popup;
import imlui.widgets.common;
import imlui.widgets.menu;

export namespace imlui::fluent {

/// Draws a menu row (md::menuItem's parameters); returns whether it was chosen.
bool menuItem(const Look& look, const char* id, std::string_view label, std::string_view leadingIcon,
              std::string_view trailing, bool selected, bool closeOnClick, bool reserveIcon);
/// Begins a menu-bar menu or a submenu with the flyout style (md::beginMenu's parameters); endMenu() after it
/// returned true.
bool beginMenu(const Look& look, const char* label, float minWidth);
void endMenu();
/// Begins a flyout anchored to [a, b] (md::beginAnchoredMenu's parameters); endAnchoredMenu() after it returned true.
bool beginAnchoredMenu(const Look& look, const char* popupId, ImVec2 a, ImVec2 b, float minWidth);
void endAnchoredMenu();
/// A menu row's height, pixels.
[[nodiscard]] float menuRowHeight(const Look& look);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float rowExtra = 8.0f;      // dp over compactHeight
constexpr float minRowWidth = 160.0f; // dp
constexpr float shortcutGap = 24.0f;  // dp between the label and the shortcut, at the least
constexpr float anchorGap = 2.0f;     // dp between the anchor and its flyout
constexpr int visibleRows = 8;        // an anchored menu's height before it scrolls

/// The flyout's padding: flyoutPaddingY above and below the rows, none at the sides.
ImVec2 menuPadding() { return {0, dp(flyoutPaddingY)}; }

} // namespace

float menuRowHeight(const Look& look) { return compactHeight(look) + dp(rowExtra * look.tokens.density); }

bool menuItem(const Look& look, const char* id, std::string_view label, std::string_view leadingIcon,
              std::string_view trailing, bool selected, bool closeOnClick, bool reserveIcon) {
    const float h = menuRowHeight(look);
    const bool lead = hasIcon(leadingIcon) || reserveIcon;
    const Content icon = content(look, {}, leadingIcon);
    const float iconPx = dp(iconSize(look));
    const float g = gap(look);
    const float pad = paddingX(look);
    const float insetX = dp(plateInsetX);
    const float trailW = trailing.empty() ? 0.0f : md::measure(captionStyle, trailing).x;
    float natural = 2.0f * insetX + 2.0f * pad + (lead ? iconPx + g : 0.0f) + md::measure(bodyStyle, label).x;
    if (trailW > 0) natural += dp(shortcutGap) + trailW;
    natural = std::max(natural, dp(minRowWidth));
    const float w = layout::claimWidth(natural);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    // The row draws its own highlight and focus rectangle: ImGui's are hidden.
    constexpr ImVec4 none{0, 0, 0, 0};
    ImGui::PushStyleColor(ImGuiCol_Header, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, none);
    const bool clicked = ImGui::Selectable(
        id, selected, closeOnClick ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_NoAutoClosePopups, {w, h});
    ImGui::PopStyleColor(4);
    const ItemState item = itemState();
    const ControlColors c = rowColors(look, stateOf(item), selected ? 1.0f : 0.0f);
    const Colors& k = look.colors;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a{p.x + insetX, p.y + dp(plateInsetY)}, b{p.x + w - insetX, p.y + h - dp(plateInsetY)};
    const float radius = controlRadius(look);
    md::fillRoundRect(dl, a, b, radius, c.fill);
    const float cy = p.y + h * 0.5f;
    float x = a.x + pad;
    if (lead) {
        if (icon.iconPx > 0) drawContent(dl, icon, x, cy, c.text);
        x += iconPx + g;
    }
    md::drawText(dl, bodyStyle, {x, md::textTop(bodyStyle, cy)}, c.text, label);
    if (trailW > 0)
        md::drawText(dl, captionStyle, {b.x - pad - trailW, md::textTop(captionStyle, cy)},
                     item.disabled ? k.textDisabled : k.textSecondary, trailing);
    if (item.focused) drawFocusRect(dl, a, b, radius, look);
    return clicked;
}

bool beginMenu(const Look& look, const char* label, float minWidth) {
    pushFlyoutStyle(look, menuPadding()); // the menu bar's own item spacing stays for BeginMenu's item
    const bool open = ImGui::BeginMenu(label);
    popFlyoutStyle();
    if (open) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        md::pushMenuBox(minWidth);
    }
    return open;
}

void endMenu() {
    md::popMenuBox();
    ImGui::PopStyleVar();
    ImGui::EndMenu();
}

bool beginAnchoredMenu(const Look& look, const char* popupId, ImVec2 a, ImVec2 b, float minWidth) {
    // beginFlyout's steps, with the rows' zero item spacing already set for BeginPopup (md's anchored menu sets it
    // there too: ImGui sizes a window's menu columns from it).
    const md::AnchoredPopup placed = placeFlyout(look, popupId, a, b, minWidth, dp(anchorGap));
    ImGui::SetNextWindowSizeConstraints(
        {minWidth, 0}, {md::hugeExtent, menuRowHeight(look) * visibleRows + 2.0f * dp(flyoutPaddingY)});
    pushFlyoutStyle(look, menuPadding());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    const bool open = ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    popFlyoutStyle();
    md::trackAnchoredPopup(placed, open);
    if (open) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        md::pushMenuBox(minWidth);
    }
    return open;
}

void endAnchoredMenu() {
    md::popMenuBox();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

} // namespace imlui::fluent
