// Fluent navigation destinations: WinUI's NavigationViewItem, for md::navigationItem (a drawer item, the expanded
// pane's), md::navigationRailItem (the compact pane's, with its label) and md::navigationBarItem (a bottom bar's).
//
// All are subtle items: the hover and press fills inside the item (plateInsetX/plateInsetY, the control radius);
// selected, the fill is one step on and the selection indicator, an accent pill, marks it (10dp while pressed).
//
//   drawer item -> controlHeight + 8dp high (40dp), natural width 280dp (md's); the icon, the body label, the
//                  indicator at the left edge; a badge is an InfoBadge at the right.
//   rail item   -> controlHeight + 24dp high (56dp), `width` wide; the icon over a caption label, the indicator at the
//                  left edge.
//   bar item    -> 80dp high (the NavigationBar's height), `width` wide; the icon over a caption label, the indicator
//                  under the label; selected, icon and label in the accent text color.
//
// Heights scale with the density but the bar item's. Icons are iconSize + 4dp (20dp, WinUI's navigation glyphs). A
// bar or rail badge is an InfoBadge on the icon's top-right: a dot for "." or "•", else a pill with the text, in
// accent (Fluent's attention badge) with onAccent text.
export module imlui.skin.fluent.navigation;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.typography;
import imlui.widgets.common;

export namespace imlui::fluent {

/// Draws a drawer item (md::navigationItem's parameters); returns whether it was clicked.
bool navigationItem(const Look& look, const char* id, std::string_view iconName, std::string_view label, bool selected,
                    std::string_view badge, float width);
[[nodiscard]] ImVec2 navigationItemSize(const Look& look, float width);
/// Draws a navigation bar destination (md::navigationBarItem's parameters); returns whether it was clicked.
bool navigationBarItem(const Look& look, const char* id, std::string_view iconName, std::string_view label,
                       bool selected, std::string_view badge, float width);
[[nodiscard]] ImVec2 navigationBarItemSize(const Look& look, float width);
/// The NavigationBar `width` (pixels, resolved) wide: as high as its destinations (80dp).
[[nodiscard]] ImVec2 navigationBarSize(const Look& look, float width);
/// Draws a navigation rail destination (md::navigationRailItem's parameters); returns whether it was clicked.
bool navigationRailItem(const Look& look, const char* id, std::string_view iconName, std::string_view label,
                        bool selected, std::string_view badge, float width);
[[nodiscard]] ImVec2 navigationRailItemSize(const Look& look, float width);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float drawerExtra = 8.0f;     // dp over controlHeight
constexpr float drawerNatural = 280.0f; // dp
constexpr float railExtra = 24.0f;      // dp over controlHeight
constexpr float barHeight = 80.0f;      // dp: the NavigationBar's
constexpr float navIconExtra = 4.0f;    // dp over iconSize
constexpr float labelGap = 4.0f;        // dp between a destination's icon and its label
constexpr float badgeHeight = 16.0f;    // dp
constexpr float badgeDot = 3.0f;        // dp: a dot badge's radius
constexpr TextStyle badgeStyle = TextStyle::LabelSmall;

float navIconDp(const Look& look) { return iconSize(look) + navIconExtra; }

bool isDot(std::string_view badge) { return badge == "\xE2\x80\xA2" || badge == "."; }

/// A badge's width, pixels.
float badgeWidth(std::string_view badge) {
    if (isDot(badge)) return 2.0f * dp(badgeDot);
    return std::max(dp(badgeHeight), md::measure(badgeStyle, badge).x + dp(8));
}

/// An InfoBadge: a dot centered on `c`, or a pill `badgeHeight` high whose left edge (`leftAligned`) or right edge
/// is at c.x, vertically centered on c.y.
void drawBadge(ImDrawList* dl, ImVec2 c, std::string_view badge, bool leftAligned, bool disabled, const Look& look) {
    const Color fill = disabled ? look.colors.accentDisabled : look.colors.accent;
    if (isDot(badge)) {
        md::fillCircle(dl, c, dp(badgeDot), fill);
        return;
    }
    const float tw = md::measure(badgeStyle, badge).x, bh = dp(badgeHeight), bw = badgeWidth(badge);
    const float x0 = leftAligned ? c.x : c.x - bw;
    md::fillRoundRect(dl, {x0, c.y - bh * 0.5f}, {x0 + bw, c.y + bh * 0.5f}, bh * 0.5f, fill);
    md::drawText(dl, badgeStyle, {x0 + (bw - tw) * 0.5f, md::textTop(badgeStyle, c.y)},
                 disabled ? look.colors.onAccentDisabled : look.colors.onAccent, badge);
}

/// The item's button, state and highlight; returns the highlight's rect.
struct Plate {
    bool clicked = false;
    ItemState item;
    float selected = 0; ///< animated 0..1
    ControlColors colors;
    ImVec2 a, b;
};

Plate plate(const Look& look, const char* id, bool selected, ImVec2 size) {
    Plate pl;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    pl.clicked = invisibleButton(id, size);
    pl.item = itemState();
    pl.selected = md::animate(pl.item.id + 3, selected ? 1.0f : 0.0f, 18.0f);
    pl.colors = rowColors(look, stateOf(pl.item), pl.selected);
    pl.a = {p.x + dp(plateInsetX), p.y + dp(plateInsetY)};
    pl.b = {p.x + size.x - dp(plateInsetX), p.y + size.y - dp(plateInsetY)};
    md::fillRoundRect(ImGui::GetWindowDrawList(), pl.a, pl.b, controlRadius(look), pl.colors.fill);
    return pl;
}

float indicatorPx(const Plate& pl) {
    return dp(pl.item.pressed ? indicatorPressedLength : indicatorLength) * pl.selected;
}

/// A rail or bar destination: the icon over the caption label, centered in the item; the indicator at the left edge
/// (`indicatorLeft`) or under the label.
bool destination(const Look& look, const char* id, std::string_view iconName, std::string_view label, bool selected,
                 std::string_view badge, ImVec2 size, bool indicatorLeft, bool accentContent) {
    const Plate pl = plate(look, id, selected, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Colors& k = look.colors;
    const bool dis = pl.item.disabled;
    Color fg = pl.colors.text;
    if (accentContent && selected && !dis) fg = k.accentText;

    const float iconPx = dp(navIconDp(look));
    const float labelH = label.empty() ? 0.0f : md::measure(captionStyle, "Ag").y;
    const float blockH = iconPx + (label.empty() ? 0.0f : dp(labelGap) + labelH);
    const ImVec2 p = ImGui::GetItemRectMin();
    const float cx = p.x + size.x * 0.5f;
    const float top = std::round(p.y + (size.y - blockH) * 0.5f);
    const ImVec2 ic{cx, top + iconPx * 0.5f};
    md::drawIcon(dl, iconName, ic, navIconDp(look), fg);
    if (!badge.empty()) {
        const ImVec2 at = isDot(badge) ? ImVec2{ic.x + iconPx * 0.5f, ic.y - iconPx * 0.5f}
                                       : ImVec2{ic.x + iconPx * 0.25f, ic.y - iconPx * 0.5f};
        drawBadge(dl, at, badge, true, dis, look);
    }
    if (!label.empty()) {
        const float lw = md::measure(captionStyle, label).x;
        // Long labels are clipped to the destination rather than running into the next one.
        dl->PushClipRect(p, {p.x + size.x, p.y + size.y}, true);
        md::drawText(dl, captionStyle, {std::max(p.x + dp(2), cx - lw * 0.5f), top + iconPx + dp(labelGap)}, fg, label);
        dl->PopClipRect();
    }
    if (pl.selected > 0.01f) {
        if (indicatorLeft)
            drawIndicator(dl, {pl.a.x + dp(indicatorThickness) * 0.5f, (pl.a.y + pl.b.y) * 0.5f}, indicatorPx(pl), true,
                          dis, look);
        else
            drawIndicator(dl, {cx, pl.b.y - dp(indicatorThickness)}, indicatorPx(pl), false, dis, look);
    }
    if (pl.item.focused) drawFocusRect(dl, pl.a, pl.b, controlRadius(look), look);
    return pl.clicked;
}

} // namespace

ImVec2 navigationItemSize(const Look& look, float width) {
    return {md::itemWidth(width, dp(drawerNatural)), controlHeight(look) + dp(drawerExtra * look.tokens.density)};
}

bool navigationItem(const Look& look, const char* id, std::string_view iconName, std::string_view label, bool selected,
                    std::string_view badge, float width) {
    const ImVec2 size = navigationItemSize(look, width);
    const Plate pl = plate(look, id, selected, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = (pl.a.y + pl.b.y) * 0.5f;
    if (pl.selected > 0.01f)
        drawIndicator(dl, {pl.a.x + dp(indicatorThickness) * 0.5f, cy}, indicatorPx(pl), true, pl.item.disabled, look);
    const Content c = content(look, label, iconName, bodyStyle, navIconDp(look));
    const float x = pl.a.x + paddingX(look);
    float right = pl.b.x - paddingX(look);
    if (!badge.empty()) {
        drawBadge(dl, {isDot(badge) ? right - dp(badgeDot) : right, cy}, badge, false, pl.item.disabled, look);
        right -= badgeWidth(badge) + gap(look);
    }
    dl->PushClipRect({x, pl.a.y}, {std::max(x, right), pl.b.y}, true);
    drawContent(dl, c, x, cy, pl.colors.text);
    dl->PopClipRect();
    if (pl.item.focused) drawFocusRect(dl, pl.a, pl.b, controlRadius(look), look);
    return pl.clicked;
}

ImVec2 navigationBarItemSize(const Look&, float width) { return {std::max(1.0f, width), dp(barHeight)}; }
ImVec2 navigationBarSize(const Look&, float width) { return {std::max(0.0f, width), dp(barHeight)}; }

bool navigationBarItem(const Look& look, const char* id, std::string_view iconName, std::string_view label,
                       bool selected, std::string_view badge, float width) {
    return destination(look, id, iconName, label, selected, badge, navigationBarItemSize(look, width), false, true);
}

ImVec2 navigationRailItemSize(const Look& look, float width) {
    return {std::max(1.0f, width), controlHeight(look) + dp(railExtra * look.tokens.density)};
}

bool navigationRailItem(const Look& look, const char* id, std::string_view iconName, std::string_view label,
                        bool selected, std::string_view badge, float width) {
    return destination(look, id, iconName, label, selected, badge, navigationRailItemSize(look, width), true, false);
}

} // namespace imlui::fluent
