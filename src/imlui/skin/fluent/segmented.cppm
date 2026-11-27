// Fluent segmented control: the Windows Community Toolkit's Segmented (WinUI's SelectorBar look), for md::segmented.
//
// A standard control's frame (an alt fill, a 1dp stroke, the control radius) holds equal segments, 2dp inside it.
// The segments are subtle (transparent, a subtle fill on hover and press); the selected one is a raised standard
// control (a neutral fill with Fluent's elevation border) with a 16 x 3dp accent pill centered at its bottom. The
// selection fades between segments. No check icon on the selected segment (md's); `icons` show on every segment.
//
// Height: controlHeight. Width: equal segments, each paddingX + icon + gap + the widest label + paddingX (room for an
// icon whether or not one shows, like md's), plus the frame's inset;
// `width` resolves like md::itemWidth. Same IDs, return value and trailing item as md::segmented.
export module imlui.skin.fluent.segmented;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.widgets.common;

export namespace imlui::fluent {

/// A segmented control (md::segmented's parameters): one of `items` selected; returns whether `*current` changed.
/// Draws nothing without items.
bool segmented(const Look& look, const char* id, const std::vector<std::string>& items, int* current, float width,
               const std::vector<std::string>& icons);
/// The size segmented() takes with `items` and `width`; {0, 0} without items.
[[nodiscard]] ImVec2 segmentedSize(const Look& look, const std::vector<std::string>& items, float width);

/// The inset of the segments in the frame, dp.
inline constexpr float segmentedInsetDp = 2.0f;

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float indicatorWidthDp = 16.0f, indicatorHeightDp = 3.0f;
constexpr Color clear{0, 0, 0, 0};

std::string_view iconAt(const std::vector<std::string>& icons, std::size_t i) {
    return i < icons.size() ? std::string_view{icons[i]} : std::string_view{};
}

/// The height, and the width (`width` resolved): equal segments as wide as the widest label with room for an icon
/// (the size function doesn't get the icons, so every segment keeps the room, like md's), plus the inset.
ImVec2 segmentedLayout(const Look& look, const std::vector<std::string>& items, float width) {
    float widest = 0;
    for (const std::string& item : items)
        widest = std::max(widest, md::measure(bodyStyle, item).x);
    const float segment = widest + dp(iconSize(look)) + gap(look) + 2.0f * paddingX(look);
    const float natural = segment * static_cast<float>(items.size()) + 2.0f * dp(segmentedInsetDp);
    return {md::itemWidth(width, natural), controlHeight(look)};
}

} // namespace

ImVec2 segmentedSize(const Look& look, const std::vector<std::string>& items, float width) {
    if (items.empty()) return {0.0f, 0.0f};
    return segmentedLayout(look, items, width);
}

bool segmented(const Look& look, const char* id, const std::vector<std::string>& items, int* current, float width,
               const std::vector<std::string>& icons) {
    if (items.empty()) return false;
    const Colors& col = look.colors;
    const ImVec2 size = segmentedLayout(look, items, width);
    const float w = size.x, h = size.y;
    const float inset = dp(segmentedInsetDp);
    const int n = static_cast<int>(items.size());
    const float segW = std::max(0.0f, w - 2.0f * inset) / static_cast<float>(n);
    const float segRadius = std::max(0.0f, controlRadius(look) - dp(1));
    const float stroke = strokeWidth(look);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The frame first: the segments draw over it.
    drawControl(dl, p, {p.x + w, p.y + h}, controlRadius(look),
                {col.controlAlt, col.controlStroke, col.controlStroke, {}}, stroke);
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const ImVec2 a{p.x + inset + segW * static_cast<float>(i), p.y + inset}, b{a.x + segW, p.y + h - inset};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (invisibleButton("##seg", {segW, b.y - a.y}) && *current != i) {
            *current = i;
            changed = true;
        }
        const ItemState item = itemState();
        ImGui::PopID();
        const State state = stateOf(item);
        const bool sel = *current == i;
        const float selT = md::animate(item.id + 7, sel ? 1.0f : 0.0f, 18.0f);

        const ControlColors subtle = controlColors(look, Fill::Subtle, state);
        if (selT < 0.99f && subtle.fill.a > 0)
            md::fillRoundRect(dl, a, b, segRadius, subtle.fill.withAlpha(subtle.fill.a * (1 - selT)));
        if (selT > 0.01f) {
            ControlColors raised = controlColors(look, Fill::Standard, state);
            const auto fade = [selT](Color c) { return c.withAlpha(c.a * selT); };
            drawControl(dl, a, b, segRadius, {fade(raised.fill), fade(raised.stroke), fade(raised.bottomStroke), clear},
                        stroke);
            const float iw = dp(indicatorWidthDp) * (state == State::Pressed ? 0.75f : 1.0f),
                        ih = dp(indicatorHeightDp);
            const float cx = std::round((a.x + b.x) * 0.5f), y1 = b.y - stroke;
            md::fillRoundRect(dl, {cx - iw * 0.5f, y1 - ih}, {cx + iw * 0.5f, y1}, ih * 0.5f,
                              fade(state == State::Disabled ? col.accentDisabled : col.accent));
        }
        const auto idx = static_cast<std::size_t>(i);
        const Content c = content(look, items[idx], iconAt(icons, idx));
        drawContent(dl, c, a.x + (segW - c.width) * 0.5f, (a.y + b.y) * 0.5f, subtle.text);
        if (item.focused) drawFocusRect(dl, a, b, segRadius, look);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    return changed;
}

} // namespace imlui::fluent
