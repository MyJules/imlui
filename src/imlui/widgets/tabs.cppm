// Material tabs header (primary and secondary) with a sliding indicator.
export module imlui.widgets.tabs;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;

export namespace imlui::md {

struct TabInfo {
    std::string label, icon;
};

/// Material tabs header. `current` 0-based. `secondary` = secondary tabs style. Returns true when changed.
bool tabs(const char* id, const std::vector<TabInfo>& items, int* current, bool secondary = false, float width = -1.0f);

} // namespace imlui::md

module :private;

namespace imlui::md {

bool tabs(const char* id, const std::vector<TabInfo>& items, int* current, bool secondary, float width) {
    if (items.empty()) return false;
    const Scheme& s = scheme();
    bool anyIcon = false;
    for (auto& t : items) anyIcon = anyIcon || hasIcon(t.icon);
    const bool stacked = anyIcon && !secondary;
    const float h = dp(stacked ? 64 : 48);
    const float w = itemWidth(width, dp(90) * static_cast<float>(items.size()));
    const float tw = w / static_cast<float>(items.size());
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    // The indicator's ends relative to the bar's left edge, so it doesn't lag behind when the bar moves (scrolling,
    // resizing); none while no tab is selected.
    std::optional<std::pair<float, float>> indicator;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const ImVec2 a{p.x + tw * static_cast<float>(i), p.y};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", {tw, h}) && *current != i) { *current = i; changed = true; }
        const bool dis = itemDisabled();
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const ImGuiID tid = ImGui::GetItemID();
        ImGui::PopID();
        const bool sel = i == *current;
        Color fg = dis ? disabledContent() : (sel ? (secondary ? s.onSurface : s.primary) : s.onSurfaceVariant);
        float layer = dis ? 0.0f : stateLayer(tid, hov, act);
        if (layer > 0)
            fillRoundRect(dl, a, {a.x + tw, a.y + h}, 0.0f, (sel ? s.primary : s.onSurface).withAlpha(layer));
        const auto& t = items[static_cast<std::size_t>(i)];
        ImVec2 ts = measure(TextStyle::TitleSmall, t.label);
        const bool ic = hasIcon(t.icon);
        float contentW = ts.x;
        if (stacked) {
            if (ic) drawIcon(dl, t.icon, {a.x + tw * 0.5f, a.y + dp(10) + dp(12)}, 24.0f, fg);
            drawText(dl, TextStyle::TitleSmall, {a.x + (tw - ts.x) * 0.5f, a.y + dp(38)}, fg, t.label);
        } else {
            const float iw = ic ? dp(24) + dp(8) : 0.0f;
            contentW = ts.x + iw;
            float x = a.x + (tw - contentW) * 0.5f;
            if (ic) drawIcon(dl, t.icon, {x + dp(12), a.y + h * 0.5f}, 24.0f, fg);
            drawText(dl, TextStyle::TitleSmall, {x + iw, textTop(TextStyle::TitleSmall, a.y + h * 0.5f)}, fg, t.label);
        }
        if (sel) {
            contentW = std::max(contentW, dp(24));
            const float left = tw * static_cast<float>(i);
            if (secondary)
                indicator = std::pair{left, left + tw};
            else
                indicator = std::pair{left + (tw - contentW) * 0.5f, left + (tw + contentW) * 0.5f};
        }
    }
    hLine(dl, p.x, p.x + w, p.y + h - dp(1), s.surfaceVariant, dp(1));
    // Sliding indicator.
    const ImGuiID k = ImGui::GetID("##ind");
    if (indicator) {
        const float x0 = p.x + animate(k, indicator->first, 16.0f), x1 = p.x + animate(k + 1, indicator->second, 16.0f);
        float ih = secondary ? dp(2) : dp(3);
        if (x1 > x0)
            fillRoundRect(dl, {x0, p.y + h - ih}, {x1, p.y + h}, secondary ? 0.0f : dp(3), s.primary,
                          ImDrawFlags_RoundCornersTop);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    return changed;
}

} // namespace imlui::md
