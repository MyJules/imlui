// Navigation destinations: drawer items, navigation bar items and navigation rail items.
export module imlui.widgets.navigation;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;

export namespace imlui::md {

/// Navigation drawer item: 56dp pill with icon, label and optional badge text. Returns true when clicked.
bool navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                    std::string_view badge = {}, float width = -1.0f);
/// The size of navigationItem() with `width` (resolved like itemWidth(): 0 is the natural 280dp).
[[nodiscard]] ImVec2 navigationItemSize(float width = -1.0f);

/// Navigation bar destination, `width` x 80dp: a 24dp icon in a 64x32dp pill indicator (secondaryContainer when
/// selected) above a labelMedium label. `badge` puts a small badge on the icon. Returns true when clicked.
bool navigationBarItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                       std::string_view badge, float width);
/// The size of navigationBarItem() `width` wide.
[[nodiscard]] ImVec2 navigationBarItemSize(float width);

/// Navigation rail destination, `width` x 56dp: a 24dp icon in a 56x32dp pill indicator with the label below.
/// Returns true when clicked.
bool navigationRailItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                        std::string_view badge, float width);
/// The size of navigationRailItem() `width` wide.
[[nodiscard]] ImVec2 navigationRailItemSize(float width);

} // namespace imlui::md

module :private;

namespace imlui::md {

ImVec2 navigationItemSize(float width) { return {itemWidth(width, dp(280)), dp(56)}; }

bool navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                    std::string_view badge, float width) {
    const Scheme& s = scheme();
    const ImVec2 size = navigationItemSize(width);
    const float w = size.x, h = size.y;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImGuiID iid = ImGui::GetItemID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = h * 0.5f;
    float t = animate(iid + 3, selected ? 1.0f : 0.0f, 18.0f);
    if (t > 0.01f) {
        // Indicator grows from the center.
        float inset = (1.0f - t) * w * 0.15f;
        // Disabled: the disabled container, faded in with its own (12%) alpha like the enabled one.
        const Color ind = dis ? disabledContainer() : s.secondaryContainer;
        fillRoundRect(dl, {p.x + inset, p.y}, {p.x + w - inset, p.y + h}, rnd, ind.withAlpha(ind.a * t));
    }
    Color fg = dis ? disabledContent() : (selected ? s.onSecondaryContainer : s.onSurfaceVariant);
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0)
        fillRoundRect(dl, p, {p.x + w, p.y + h}, rnd,
                      (selected ? s.onSecondaryContainer : s.onSurface).withAlpha(layer));
    float x = p.x + dp(16);
    const float cy = p.y + h * 0.5f;
    if (hasIcon(iconName)) {
        drawIcon(dl, iconName, {x + dp(12), cy}, 24.0f, fg);
        x += dp(24) + dp(12);
    }
    drawTextV(dl, TextStyle::LabelLarge, x, cy, fg, label);
    if (!badge.empty()) {
        float bw = measure(TextStyle::LabelLarge, badge).x;
        drawTextV(dl, TextStyle::LabelLarge, p.x + w - dp(24) - bw, cy, fg, badge);
    }
    return clicked;
}

namespace {

/// Geometry of a bar or rail destination, in dp relative to the item's top-left corner.
struct DestinationLayout {
    float height;         ///< whole item (the click target)
    float indicatorWidth; ///< the pill behind the icon; 32dp tall
    float indicatorTop;
    float labelCenter; ///< vertical center of the label
};

/// Small M3 badge on the icon's top-right: a 6dp dot for "•", else a 16dp pill with labelSmall text.
void drawBadge(ImDrawList* dl, ImVec2 iconCenter, std::string_view badge) {
    const Scheme& s = scheme();
    if (badge == "\xE2\x80\xA2" || badge == ".") {
        fillCircle(dl, {iconCenter.x + dp(9), iconCenter.y - dp(9)}, dp(3), s.error);
        return;
    }
    const float tw = measure(TextStyle::LabelSmall, badge).x;
    const float bw = std::max(dp(16), tw + dp(8)), x0 = iconCenter.x + dp(4), y0 = iconCenter.y - dp(14);
    fillRoundRect(dl, {x0, y0}, {x0 + bw, y0 + dp(16)}, dp(8), s.error);
    drawTextV(dl, TextStyle::LabelSmall, x0 + (bw - tw) * 0.5f, y0 + dp(8), s.onError, badge);
}

constexpr DestinationLayout barDestination{.height = 80, .indicatorWidth = 64, .indicatorTop = 12, .labelCenter = 56};
constexpr DestinationLayout railDestination{.height = 56, .indicatorWidth = 56, .indicatorTop = 0, .labelCenter = 44};

ImVec2 destinationSize(float width, const DestinationLayout& l) { return {std::max(1.0f, width), dp(l.height)}; }

bool destination(const char* id, std::string_view iconName, std::string_view label, bool selected,
                 std::string_view badge, float width, const DestinationLayout& l) {
    const Scheme& s = scheme();
    const ImVec2 size = destinationSize(width, l);
    const float w = size.x, h = size.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImGuiID iid = ImGui::GetItemID();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float cx = p.x + w * 0.5f, iw = dp(l.indicatorWidth), ih = dp(32);
    const ImVec2 ia{cx - iw * 0.5f, p.y + dp(l.indicatorTop)}, ib{cx + iw * 0.5f, p.y + dp(l.indicatorTop) + ih};
    // The indicator grows out from its center when the destination becomes selected.
    const float t = animate(iid + 3, selected ? 1.0f : 0.0f, 18.0f);
    if (t > 0.01f) {
        const float half = iw * 0.5f * (0.5f + 0.5f * t);
        const Color ind = dis ? disabledContainer() : s.secondaryContainer;
        fillRoundRect(dl, {cx - half, ia.y}, {cx + half, ib.y}, ih * 0.5f, ind.withAlpha(ind.a * t));
    }
    // M3 puts the hover / press layer on the indicator, not on the whole target.
    const float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0)
        fillRoundRect(dl, ia, ib, ih * 0.5f, (selected ? s.onSecondaryContainer : s.onSurface).withAlpha(layer));

    const ImVec2 ic{cx, (ia.y + ib.y) * 0.5f};
    const Color iconColor = dis ? disabledContent() : (selected ? s.onSecondaryContainer : s.onSurfaceVariant);
    drawIcon(dl, iconName, ic, 24.0f, iconColor);
    if (!badge.empty()) drawBadge(dl, ic, badge);

    if (!label.empty()) {
        const Color labelColor = dis ? disabledContent() : (selected ? s.onSurface : s.onSurfaceVariant);
        const float lw = measure(TextStyle::LabelMedium, label).x;
        // Long labels are clipped to the destination rather than running into the next one.
        dl->PushClipRect({p.x, p.y}, {p.x + w, p.y + h}, true);
        drawTextV(dl, TextStyle::LabelMedium, std::max(p.x + dp(2), cx - lw * 0.5f), p.y + dp(l.labelCenter),
                  labelColor, label);
        dl->PopClipRect();
    }
    return clicked;
}

} // namespace

bool navigationBarItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                       std::string_view badge, float width) {
    return destination(id, iconName, label, selected, badge, width, barDestination);
}

ImVec2 navigationBarItemSize(float width) { return destinationSize(width, barDestination); }

bool navigationRailItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                        std::string_view badge, float width) {
    return destination(id, iconName, label, selected, badge, width, railDestination);
}

ImVec2 navigationRailItemSize(float width) { return destinationSize(width, railDestination); }

} // namespace imlui::md
