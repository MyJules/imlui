// Navigation drawer item.
module;

#include <string_view>

export module imlui.widgets.navigation;

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

} // namespace imlui::md

module :private;

namespace imlui::md {

bool navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                    std::string_view badge, float width) {
    const Scheme& s = scheme();
    const float h = dp(56);
    const float w = itemWidth(width, dp(280));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {w, h});
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImGuiID iid = ImGui::GetItemID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = h * 0.5f;
    float t = animate(iid + 3, selected ? 1.0f : 0.0f, 18.0f);
    if (t > 0.01f) {
        // Indicator grows from the center.
        float inset = (1.0f - t) * w * 0.15f;
        dl->AddRectFilled({p.x + inset, p.y}, {p.x + w - inset, p.y + h}, col(s.secondaryContainer.withAlpha(t)), rnd);
    }
    Color fg = dis ? disabledContent() : (selected ? s.onSecondaryContainer : s.onSurfaceVariant);
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0)
        dl->AddRectFilled(p, {p.x + w, p.y + h},
                          col((selected ? s.onSecondaryContainer : s.onSurface).withAlpha(layer)), rnd);
    float x = p.x + dp(16);
    const float cy = p.y + h * 0.5f;
    if (hasIcon(iconName)) {
        drawIcon(dl, iconName, {x + dp(12), cy}, 24.0f, fg);
        x += dp(24) + dp(12);
    }
    drawTextV(dl, TextStyle::LabelLarge, x, cy,
              selected ? s.onSecondaryContainer : (dis ? disabledContent() : s.onSurfaceVariant), label);
    if (!badge.empty()) {
        float bw = measure(TextStyle::LabelLarge, badge).x;
        drawTextV(dl, TextStyle::LabelLarge, p.x + w - dp(24) - bw, cy, fg, badge);
    }
    return clicked;
}

} // namespace imlui::md
