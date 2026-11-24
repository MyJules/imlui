// Selection controls: checkbox, radio button, switch, chips and segmented buttons.
export module imlui.widgets.selection;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;

export namespace imlui::md {

/// Checkbox with optional label; toggles `*v`. Returns true when clicked.
bool checkbox(const char* label, bool* v);
/// Radio button with optional label. Returns true when clicked.
bool radio(const char* label, bool selected);
/// Material switch; toggles `*v`. Returns true when clicked.
bool switchToggle(const char* label, bool* v, bool withIcons = true);

enum class ChipVariant { Filter, Assist, Suggestion, Input };

/// "filter" (default), "assist", "suggestion", "input".
[[nodiscard]] ChipVariant chipVariant(std::string_view n);

/// Chip. Returns true when clicked.
bool chip(const char* label, bool selected, ChipVariant v = ChipVariant::Filter, std::string_view leadingIcon = {});

/// Segmented button: one of `items` selected. Returns true when changed.
bool segmented(const char* id, const std::vector<std::string>& items, int* current, float width = 0.0f,
               const std::vector<std::string>& icons = {});

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {
float controlRowHeight() { return dp(40); }
/// Center of the `h` x `h` box at `p`, on whole pixels: even-sized circles and boxes around it stay symmetric.
ImVec2 pixelCenter(ImVec2 p, float h) { return {std::round(p.x + h * 0.5f), std::round(p.y + h * 0.5f)}; }
} // namespace

bool checkbox(const char* label, bool* v) {
    const Scheme& s = scheme();
    const float box = dp(18), h = controlRowHeight();
    const std::string_view text = visibleLabel(label);
    const float labelW = text.empty() ? 0.0f : dp(4) + measure(TextStyle::BodyLarge, text).x + dp(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {h + labelW, h});
    if (clicked) *v = !*v;
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = pixelCenter(p, h);
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) fillCircle(dl, c, h * 0.5f, (*v ? s.primary : s.onSurface).withAlpha(layer));
    ImVec2 a{c.x - box * 0.5f, c.y - box * 0.5f}, b{c.x + box * 0.5f, c.y + box * 0.5f};
    float t = animate(id + 1, *v ? 1.0f : 0.0f, 20.0f);
    Color fill = dis ? disabledContent() : s.primary;
    Color outline = dis ? disabledContent() : s.onSurfaceVariant;
    if (t > 0.01f) {
        fillRoundRect(dl, a, b, dp(2), fill.withAlpha(fill.a * t));
        drawIcon(dl, "check", c, 18.0f * (0.6f + 0.4f * t), (dis ? s.surface : s.onPrimary).withAlpha(t));
    }
    if (t < 0.99f) strokeRoundRect(dl, a, b, dp(2), outline.withAlpha(outline.a * (1 - t)), dp(2));
    if (!text.empty()) drawTextV(dl, TextStyle::BodyLarge, p.x + h + dp(4), c.y, dis ? disabledContent() : s.onSurface, text);
    return clicked;
}

bool radio(const char* label, bool selected) {
    const Scheme& s = scheme();
    const float h = controlRowHeight();
    const std::string_view text = visibleLabel(label);
    const float labelW = text.empty() ? 0.0f : dp(4) + measure(TextStyle::BodyLarge, text).x + dp(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {h + labelW, h});
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = pixelCenter(p, h);
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) fillCircle(dl, c, h * 0.5f, (selected ? s.primary : s.onSurface).withAlpha(layer));
    Color ring = dis ? disabledContent() : (selected ? s.primary : s.onSurfaceVariant);
    // 20dp ring with a 2dp stroke inside it.
    strokeCircle(dl, c, dp(10), ring, dp(2));
    float t = animate(id + 1, selected ? 1.0f : 0.0f, 18.0f);
    if (t > 0.01f) fillCircle(dl, c, dp(5) * t, ring);
    if (!text.empty()) drawTextV(dl, TextStyle::BodyLarge, p.x + h + dp(4), c.y, dis ? disabledContent() : s.onSurface, text);
    return clicked;
}

bool switchToggle(const char* label, bool* v, bool withIcons) {
    const Scheme& s = scheme();
    const float tw = dp(52), th = dp(32), h = controlRowHeight();
    const std::string_view text = visibleLabel(label);
    const float labelW = text.empty() ? 0.0f : dp(12) + measure(TextStyle::BodyLarge, text).x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {tw + labelW, h});
    if (clicked) *v = !*v;
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Track on whole pixels, so the thumb (centered on its end caps) lines up with the snapped track.
    const ImVec2 a{std::round(p.x), std::round(p.y + (h - th) * 0.5f)}, b{a.x + tw, a.y + th};
    float t = animate(id + 1, *v ? 1.0f : 0.0f, 16.0f);

    Color trackOff = s.surfaceContainerHighest, trackOn = s.primary;
    Color thumbOff = hov ? s.onSurfaceVariant : s.outline, thumbOn = hov ? s.primaryContainer : s.onPrimary;
    Color border = s.outline;
    if (dis) {
        trackOff = s.surfaceContainerHighest.withAlpha(0.12f);
        trackOn = s.onSurface.withAlpha(0.12f);
        thumbOff = s.onSurface.withAlpha(0.38f);
        thumbOn = s.surface;
        border = s.onSurface.withAlpha(0.12f);
    }
    fillRoundRect(dl, a, b, th * 0.5f, mix(trackOff, trackOn, t));
    if (t < 0.99f) strokeRoundRect(dl, a, b, th * 0.5f, border.withAlpha(border.a * (1 - t)), dp(2));

    // Thumb (M3): 16dp unselected, 24dp selected, 28dp pressed, centered on the track's end-cap centers, which
    // leaves 8dp to the track edge unselected and 4dp selected.
    const bool showIcon = withIcons && t > 0.5f;
    float r = act ? dp(14) : (showIcon ? dp(12) : dp(8) + dp(4) * t);
    ImVec2 c{a.x + th * 0.5f + (tw - th) * t, a.y + th * 0.5f};
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) fillCircle(dl, c, dp(20), (*v ? s.primary : s.onSurface).withAlpha(layer));
    fillCircle(dl, c, r, mix(thumbOff, thumbOn, t));
    if (showIcon) drawIcon(dl, "check", c, 16.0f, dis ? s.onSurface.withAlpha(0.38f) : s.onPrimaryContainer);
    if (!text.empty())
        drawTextV(dl, TextStyle::BodyLarge, p.x + tw + dp(12), a.y + th * 0.5f, dis ? disabledContent() : s.onSurface,
                  text);
    return clicked;
}

ChipVariant chipVariant(std::string_view n) {
    if (n == "assist") return ChipVariant::Assist;
    if (n == "suggestion") return ChipVariant::Suggestion;
    if (n == "input") return ChipVariant::Input;
    return ChipVariant::Filter;
}

bool chip(const char* label, bool selected, ChipVariant v, std::string_view leadingIcon) {
    const Scheme& s = scheme();
    const std::string_view text = visibleLabel(label);
    ImVec2 ts = measure(TextStyle::LabelLarge, text);
    const bool check = v == ChipVariant::Filter && selected;
    const bool lead = check || hasIcon(leadingIcon);
    const float h = dp(32), iconW = lead ? dp(18) + dp(8) : 0.0f;
    const float padL = lead ? dp(8) : dp(16), padR = dp(16);
    const float w = padL + iconW + ts.x + padR;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{p.x + w, p.y + h};
    const float rnd = dp(shape().small);
    const bool filled = selected && v != ChipVariant::Assist;
    Color fg = filled ? s.onSecondaryContainer : (v == ChipVariant::Assist ? s.onSurface : s.onSurfaceVariant);
    Color iconCol = filled ? s.onSecondaryContainer : s.primary;
    if (dis) { fg = disabledContent(); iconCol = fg; }
    if (filled) fillRoundRect(dl, p, q, rnd, dis ? disabledContainer() : s.secondaryContainer);
    else strokeRoundRect(dl, p, q, rnd, dis ? disabledContainer() : s.outline, dp(1));
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) fillRoundRect(dl, p, q, rnd, fg.withAlpha(layer));
    float x = p.x + padL;
    if (lead) {
        drawIcon(dl, check ? std::string_view{"check"} : leadingIcon, {x + dp(9), p.y + h * 0.5f}, 18.0f, iconCol);
        x += iconW;
    }
    drawText(dl, TextStyle::LabelLarge, {x, textTop(TextStyle::LabelLarge, p.y + h * 0.5f)}, fg, text);
    return clicked;
}

bool segmented(const char* id, const std::vector<std::string>& items, int* current, float width,
               const std::vector<std::string>& icons) {
    if (items.empty()) return false;
    const Scheme& s = scheme();
    const float h = dp(40);
    float natural = 0;
    for (auto& it : items) natural = std::max(natural, measure(TextStyle::LabelLarge, it).x + dp(24) * 2 + dp(26));
    const float w = itemWidth(width, natural * static_cast<float>(items.size()));
    const float segW = w / static_cast<float>(items.size());
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    const int n = static_cast<int>(items.size());
    bool anyDisabled = false;
    for (int i = 0; i < n; ++i) {
        ImVec2 a{p.x + segW * static_cast<float>(i), p.y}, b{a.x + segW, p.y + h};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", {segW, h}) && *current != i) { *current = i; changed = true; }
        const bool dis = itemDisabled();
        anyDisabled = anyDisabled || dis;
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        ImGuiID sid = ImGui::GetItemID();
        ImGui::PopID();
        ImDrawFlags corners = n == 1 ? ImDrawFlags_RoundCornersAll
                            : i == 0 ? ImDrawFlags_RoundCornersLeft
                            : i == n - 1 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
        bool sel = *current == i;
        Color fg = dis ? disabledContent() : (sel ? s.onSecondaryContainer : s.onSurface);
        float selT = animate(sid + 7, sel ? 1.0f : 0.0f, 18.0f);
        if (selT > 0.01f)
            fillRoundRect(dl, a, b, h * 0.5f, (dis ? disabledContainer() : s.secondaryContainer).withAlpha(selT),
                          corners);
        float layer = dis ? 0.0f : stateLayer(sid, hov, act);
        if (layer > 0) fillRoundRect(dl, a, b, h * 0.5f, fg.withAlpha(layer), corners);
        const auto idx = static_cast<std::size_t>(i);
        ImVec2 ts = measure(TextStyle::LabelLarge, items[idx]);
        const std::string_view ic = sel ? std::string_view{"check"}
                                        : (idx < icons.size() ? std::string_view{icons[idx]} : std::string_view{});
        const bool showIcon = hasIcon(ic);
        float iconW = showIcon ? dp(18) + dp(8) : 0.0f;
        float x = a.x + (segW - ts.x - iconW) * 0.5f;
        if (showIcon) drawIcon(dl, ic, {x + dp(9), p.y + h * 0.5f}, 18.0f, fg);
        drawText(dl, TextStyle::LabelLarge, {x + iconW, textTop(TextStyle::LabelLarge, p.y + h * 0.5f)}, fg,
                 items[idx]);
        if (i > 0) vLine(dl, a.x, a.y, b.y, anyDisabled ? disabledContainer() : s.outline, dp(1));
    }
    strokeRoundRect(dl, p, {p.x + w, p.y + h}, h * 0.5f, anyDisabled ? disabledContainer() : s.outline, dp(1));
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    return changed;
}

} // namespace imlui::md
