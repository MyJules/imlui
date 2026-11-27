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
constexpr float flyoutPadY = 2.0f;    // dp above the first row and below the last
constexpr float minRowWidth = 160.0f; // dp
constexpr float shortcutGap = 24.0f;  // dp between the label and the shortcut, at the least
constexpr float anchorGap = 2.0f;     // dp between the anchor and its flyout
constexpr float shadowLevel = 2.0f;   // md::drawShadow's level
constexpr int visibleRows = 8;        // an anchored menu's height before it scrolls
constexpr TextStyle captionStyle = TextStyle::BodySmall;

// The anchored flyout's size, recorded in the anchor window's storage at the popup key's offsets md's anchored popup
// uses for the same values, so a switch of design keeps the placement.
constexpr ImGuiID widthSlot = 3;
constexpr ImGuiID heightSlot = 4;
constexpr ImGuiID shownSlot = 5;

/// Four style vars (WindowPadding, PopupRounding, PopupBorderSize, ItemSpacing: the last, so beginMenu can pop it
/// alone) and two colors (PopupBg, Border).
void pushFlyoutStyle(const Look& look) {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, look.colors.flyout.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, look.colors.flyoutStroke.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, dp(flyoutPadY)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, overlayRadius(look));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, strokeWidth(look));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
}

void popFlyoutStyle() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

/// Where a popup of `size` goes next to the anchor [a, b]: md's placement (below, left edges aligned; else right
/// edges aligned and/or above; clamped to the main viewport).
ImVec2 placeInViewport(ImVec2 a, ImVec2 b, ImVec2 size, float gap) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 lo = vp->Pos, hi{vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y};
    float x = a.x;
    if (x + size.x > hi.x) x = b.x - size.x;
    x = std::max(lo.x, std::min(x, hi.x - size.x));
    float y = b.y + gap;
    if (y + size.y > hi.y) {
        const float above = a.y - gap - size.y;
        if (above >= lo.y || a.y - lo.y > hi.y - b.y) y = above;
    }
    y = std::max(lo.y, std::min(y, hi.y - size.y));
    return {x, y};
}

struct Flyout {
    ImGuiStorage* storage = nullptr;
    ImGuiID key = 0;
};

/// Positions the next window next to [a, b] and draws its shadow (in the anchor's window, behind the popup, at last
/// frame's size), with the overlay radius.
Flyout placeFlyout(const Look& look, const char* popupId, ImVec2 a, ImVec2 b, float minWidth) {
    Flyout f{ImGui::GetStateStorage(), ImGui::GetID(popupId)};
    const ImVec2 size{std::max(minWidth, f.storage->GetFloat(f.key + widthSlot, 0.0f)),
                      f.storage->GetFloat(f.key + heightSlot, 0.0f)};
    const ImVec2 pos = placeInViewport(a, b, size, dp(anchorGap));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    if (ImGui::IsPopupOpen(popupId) && f.storage->GetBool(f.key + shownSlot, false) && size.y > 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRectFullScreen();
        md::drawShadow(dl, pos, {pos.x + size.x, pos.y + size.y}, overlayRadius(look), shadowLevel);
        dl->PopClipRect();
    }
    return f;
}

/// Records the flyout's size for the next frame (call right after BeginPopup, with its result).
void trackFlyout(const Flyout& f, bool open) {
    if (!open) {
        f.storage->SetBool(f.key + shownSlot, false);
        return;
    }
    // The appearing frame is laid out hidden, with a provisional size: no shadow for it yet.
    f.storage->SetBool(f.key + shownSlot, !ImGui::IsWindowAppearing());
    const ImVec2 sz = ImGui::GetWindowSize();
    f.storage->SetFloat(f.key + widthSlot, sz.x);
    f.storage->SetFloat(f.key + heightSlot, sz.y);
}

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
    pushFlyoutStyle(look);
    ImGui::PopStyleVar(); // keep the menu bar's own item spacing
    const bool open = ImGui::BeginMenu(label);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
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
    const Flyout f = placeFlyout(look, popupId, a, b, minWidth);
    ImGui::SetNextWindowSizeConstraints({minWidth, 0},
                                        {md::hugeExtent, menuRowHeight(look) * visibleRows + 2.0f * dp(flyoutPadY)});
    pushFlyoutStyle(look);
    const bool open = ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
    popFlyoutStyle();
    trackFlyout(f, open);
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
