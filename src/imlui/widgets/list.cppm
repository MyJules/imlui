// List items and collapsible section headers (expanders).
export module imlui.widgets.list;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;

export namespace imlui::md {

struct ListItemOptions {
    std::string_view headline, supporting, leadingIcon, trailingText, trailingIcon;
    bool selected = false;
    float width = -1.0f;
};

/// One- or two-line list item. Returns true when clicked.
bool listItem(const char* id, const ListItemOptions& o);
/// The size of listItem() with the same options (`o.width` resolved like itemWidth(): 0 is the natural 320dp).
[[nodiscard]] ImVec2 listItemSize(const ListItemOptions& o);

/// Collapsible section header; toggles `*open`. Returns true when clicked.
bool expander(const char* id, std::string_view title, std::string_view supporting, bool* open, float width = -1.0f);
/// The size of expander() with the same `supporting` text and `width`.
[[nodiscard]] ImVec2 expanderSize(std::string_view supporting, float width = -1.0f);

} // namespace imlui::md

module :private;

namespace imlui::md {

ImVec2 listItemSize(const ListItemOptions& o) {
    return {itemWidth(o.width, dp(320)), dp(o.supporting.empty() ? 56 : 72)};
}

ImVec2 expanderSize(std::string_view supporting, float width) {
    return {itemWidth(width, dp(320)), dp(supporting.empty() ? 48 : 64)};
}

bool listItem(const char* id, const ListItemOptions& o) {
    const Scheme& s = scheme();
    const bool two = !o.supporting.empty();
    const ImVec2 size = listItemSize(o);
    const float w = size.x, h = size.y;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    const bool dis = itemDisabled();
    const ImGuiID iid = ImGui::GetItemID();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(shape().medium);
    float st = animate(iid + 3, o.selected ? 1.0f : 0.0f, 18.0f);
    if (st > 0.01f) fillRoundRect(dl, p, {p.x + w, p.y + h}, rnd, s.secondaryContainer.withAlpha(st));
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) fillRoundRect(dl, p, {p.x + w, p.y + h}, rnd, s.onSurface.withAlpha(layer));

    const float cy = p.y + h * 0.5f;
    float x = p.x + dp(16);
    Color fg = dis ? disabledContent() : (o.selected ? s.onSecondaryContainer : s.onSurface);
    Color sub = dis ? disabledContent() : s.onSurfaceVariant;
    if (hasIcon(o.leadingIcon)) {
        drawIcon(dl, o.leadingIcon, {x + dp(12), cy}, 24.0f, o.selected ? fg : sub);
        x += dp(24) + dp(16);
    }
    float right = p.x + w - dp(16);
    if (hasIcon(o.trailingIcon)) {
        drawIcon(dl, o.trailingIcon, {right - dp(12), cy}, 24.0f, sub);
        right -= dp(24) + dp(16);
    }
    if (!o.trailingText.empty()) {
        float tw = measure(TextStyle::LabelSmall, o.trailingText).x;
        drawTextV(dl, TextStyle::LabelSmall, right - tw, cy, sub, o.trailingText);
        right -= tw + dp(16);
    }
    dl->PushClipRect({x, p.y}, {right, p.y + h}, true);
    const float hh = measure(TextStyle::BodyLarge, "Ag").y, sh = measure(TextStyle::BodyMedium, "Ag").y;
    const float total = hh + (two ? sh : 0.0f);
    const float y = cy - total * 0.5f;
    drawText(dl, TextStyle::BodyLarge, {x, y}, fg, o.headline);
    if (two) drawText(dl, TextStyle::BodyMedium, {x, y + hh}, sub, o.supporting);
    dl->PopClipRect();
    return clicked;
}

bool expander(const char* id, std::string_view title, std::string_view supporting, bool* open, float width) {
    const Scheme& s = scheme();
    const bool two = !supporting.empty();
    const ImVec2 size = expanderSize(supporting, width);
    const float w = size.x, h = size.y;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    if (clicked) *open = !*open;
    const bool dis = itemDisabled();
    const ImGuiID iid = ImGui::GetItemID();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(shape().medium);
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) fillRoundRect(dl, p, {p.x + w, p.y + h}, rnd, s.onSurface.withAlpha(layer));
    const float cy = p.y + h * 0.5f;
    const Color fg = dis ? disabledContent() : s.onSurface;
    if (two) {
        float th = measure(TextStyle::TitleMedium, title).y, sh = measure(TextStyle::BodyMedium, supporting).y;
        float y = cy - (th + sh) * 0.5f;
        drawText(dl, TextStyle::TitleMedium, {p.x + dp(16), y}, fg, title);
        drawText(dl, TextStyle::BodyMedium, {p.x + dp(16), y + th}, dis ? disabledContent() : s.onSurfaceVariant,
                 supporting);
    } else {
        drawTextV(dl, TextStyle::TitleMedium, p.x + dp(16), cy, fg, title);
    }
    drawIcon(dl, *open ? "expand_less" : "expand_more", {p.x + w - dp(16) - dp(12), cy}, 24.0f,
             dis ? disabledContent() : s.onSurfaceVariant);
    return clicked;
}

} // namespace imlui::md
