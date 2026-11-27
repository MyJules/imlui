// Fluent tabs, for md::tabs. The two Material variants map onto WinUI's two tab looks:
//
//   primary   -> SelectorBar: subtle items (the hover and press fills inside the item, the control radius), the icon
//                beside the label (never above it), secondary text on the unselected items; the selected one has the
//                selection indicator, a 16dp accent pill under its content, which slides to the new tab.
//   secondary -> TabView: the selected tab is a `layer` tab with the overlay radius on its top corners, standing on
//                a divider line along the bar's bottom that stops under it; the others are subtle (top corners).
//
// Height: controlHeight + 8dp (40dp; the 8dp scaled by the density) for both. Width: the tabs share it equally, like
// md's; natural, each tab as wide as the widest tab's content plus paddingX on each side. Selection, IDs and the
// return value are md's.
export module imlui.skin.fluent.tabs;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.widgets.common;
import imlui.widgets.tabs;

export namespace imlui::fluent {

/// Draws a tab bar (md::tabs' parameters); returns true when the user picked another tab.
bool tabs(const Look& look, const char* id, const std::vector<md::TabInfo>& items, int* current, bool secondary,
          float width);
/// The size tabs() takes with the same arguments.
[[nodiscard]] ImVec2 tabsSize(const Look& look, const std::vector<md::TabInfo>& items, bool secondary, float width);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float barExtra = 8.0f; // dp over controlHeight

struct TabsLayout {
    std::vector<Content> tabs;
    float w = 0, h = 0, tabW = 0;
};

TabsLayout tabsLayout(const Look& look, const std::vector<md::TabInfo>& items, float width) {
    TabsLayout l;
    float widest = 0;
    for (const md::TabInfo& t : items) {
        l.tabs.push_back(content(look, t.label, t.icon));
        widest = std::max(widest, l.tabs.back().width);
    }
    const float n = static_cast<float>(std::max<std::size_t>(1, items.size()));
    l.w = md::itemWidth(width, (widest + 2.0f * paddingX(look)) * n);
    l.h = controlHeight(look) + dp(barExtra * look.tokens.density);
    l.tabW = l.w / n;
    return l;
}

} // namespace

ImVec2 tabsSize(const Look& look, const std::vector<md::TabInfo>& items, bool, float width) {
    const TabsLayout l = tabsLayout(look, items, width);
    return {l.w, l.h};
}

bool tabs(const Look& look, const char* id, const std::vector<md::TabInfo>& items, int* current, bool secondary,
          float width) {
    if (items.empty()) return false;
    const TabsLayout l = tabsLayout(look, items, width);
    const Colors& k = look.colors;
    const float radius = secondary ? overlayRadius(look) : controlRadius(look);
    const float stroke = std::max(1.0f, strokeWidth(look));
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    // The selected tab's span relative to the bar's left edge (the indicator's ends, the TabView tab's gap in the
    // divider); none while no tab is selected.
    std::optional<std::pair<float, float>> indicator;
    std::optional<std::pair<float, float>> selectedTab;
    bool selectedDisabled = false;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const ImVec2 a{p.x + l.tabW * static_cast<float>(i), p.y};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (invisibleButton("##tab", {l.tabW, l.h}) && *current != i) {
            *current = i;
            changed = true;
        }
        const ItemState item = itemState();
        ImGui::PopID();
        const bool sel = i == *current;
        const State state = stateOf(item);
        const Content& c = l.tabs[static_cast<std::size_t>(i)];
        const ImVec2 b{a.x + l.tabW, a.y + l.h};

        ImVec2 pa, pb; // the item's highlight
        if (secondary) {
            pa = {a.x + dp(plateInsetX) * 0.5f, a.y + dp(plateInsetX)};
            pb = {b.x - dp(plateInsetX) * 0.5f, b.y};
            if (sel)
                md::fillRoundRect(dl, pa, pb, radius, k.layer, ImDrawFlags_RoundCornersTop);
            else
                md::fillRoundRect(dl, pa, pb, radius, controlColors(look, Fill::Subtle, state).fill,
                                  ImDrawFlags_RoundCornersTop);
        } else {
            pa = {a.x + dp(plateInsetY), a.y + dp(plateInsetX)};
            pb = {b.x - dp(plateInsetY), b.y - dp(plateInsetX)};
            md::fillRoundRect(dl, pa, pb, radius, controlColors(look, Fill::Subtle, state).fill);
        }
        // Unselected tabs are secondary text, the selected and the hovered one primary, a pressed one secondary.
        const bool primary = (sel || state == State::Hover) && state != State::Pressed;
        const Color fg = item.disabled ? k.textDisabled : (primary ? k.text : k.textSecondary);
        drawContent(dl, c, a.x + (l.tabW - c.width) * 0.5f, a.y + l.h * 0.5f, fg);
        if (item.focused) drawFocusRect(dl, pa, pb, radius, look);

        if (sel) {
            const float left = l.tabW * static_cast<float>(i);
            selectedTab = std::pair{pa.x - p.x, pb.x - p.x};
            const float mid = left + l.tabW * 0.5f, half = dp(indicatorLength) * 0.5f;
            indicator = std::pair{mid - half, mid + half};
            selectedDisabled = item.disabled;
        }
    }
    if (secondary) {
        // The divider along the bottom, open under the selected tab.
        const float y = p.y + l.h - stroke;
        if (selectedTab) {
            md::hLine(dl, p.x, p.x + selectedTab->first, y, k.divider, stroke);
            md::hLine(dl, p.x + selectedTab->second, p.x + l.w, y, k.divider, stroke);
        } else {
            md::hLine(dl, p.x, p.x + l.w, y, k.divider, stroke);
        }
    } else if (indicator) {
        // The sliding indicator, its ends relative to the bar's left edge (no lag when the bar moves), like md's.
        const ImGuiID key = ImGui::GetID("##ind");
        const float x0 = p.x + md::animate(key, indicator->first, 16.0f, md::pixelEpsilon);
        const float x1 = p.x + md::animate(key + 1, indicator->second, 16.0f, md::pixelEpsilon);
        const float th = std::max(1.0f, std::round(dp(indicatorThickness)));
        if (x1 > x0)
            drawIndicator(dl, {(x0 + x1) * 0.5f, p.y + l.h - dp(plateInsetX) - th * 0.5f}, x1 - x0, false,
                          selectedDisabled, look);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({l.w, l.h});
    return changed;
}

} // namespace imlui::fluent
