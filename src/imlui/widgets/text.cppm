// Text, standalone icons, tooltips and dividers.
export module imlui.widgets.text;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.typography;
import imlui.theme.font_scope;
import imlui.ui.frame_request;
import imlui.ui.layout_box;
import imlui.widgets.common;

export namespace imlui::md {

/// Text in a Material type style. `wrap` wraps it at the layout box's right edge; `align` (0..1) positions unwrapped
/// text in the available width.
void text(std::string_view s, TextStyle style, std::optional<Color> color = std::nullopt, bool wrap = false,
          float align = 0.0f);
/// The size text() takes with the same arguments where `maxWidth` is available at the cursor (wrapping text wraps
/// at it; aligned text is offset in it, so it is as wide as its offset and its line). nullopt for unwrapped text too
/// long to measure cheaply (ImGui lays it out another way) and for wrapping text in an auto-sized window's box (it
/// wraps past the box's width): a caller then measures it by rendering.
[[nodiscard]] std::optional<ImVec2> textSize(std::string_view s, TextStyle style, bool wrap = false, float align = 0.0f,
                                             float maxWidth = 0.0f);
/// Unwrapped text in a box exactly `width` pixels wide: positioned in it by `align` (0..1) and, when longer, cut
/// with an ellipsis (a line) or clipped (several lines). Its size doesn't follow the text, so a value that changes
/// (a counter, a clock) doesn't move what comes after it.
void textIn(std::string_view s, TextStyle style, std::optional<Color> color, float width, float align = 0.0f);
/// The size textIn() takes: `width` by the text's height.
[[nodiscard]] ImVec2 textInSize(std::string_view s, TextStyle style, float width);

/// A standalone icon item (size in dp).
void icon(std::string_view name, float size = 24.0f, std::optional<Color> color = std::nullopt);
/// The size of icon(): `size` dp square, as drawIcon() draws it: at most maxIconPixels, and 0 for a size that isn't
/// positive (or NaN). Always finite, so a script's `size = 1/0` never reaches ImGui's layout (which converts the
/// cursor position to int).
[[nodiscard]] ImVec2 iconSize(float size = 24.0f);

/// Plain tooltip (inverseSurface) for the last item.
void tooltip(std::string_view s);

/// 1dp outlineVariant line across `width` (0: the available width).
void divider(float insetStart = 0.0f, float insetEnd = 0.0f, float width = 0.0f);
/// The size of divider() `width` wide.
[[nodiscard]] ImVec2 dividerSize(float width);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

/// Where text() puts a `w` wide line in `avail`: offset by the free width times `align`, if there is any.
float alignOffset(float w, float avail, float align) { return avail > w ? (avail - w) * align : 0.0f; }

} // namespace

void text(std::string_view s, TextStyle style, std::optional<Color> color, bool wrap, float align) {
    FontScope fs{style};
    if (color) ImGui::PushStyleColor(ImGuiCol_Text, color->vec4());
    // Unwrapped text stays on one line even in a box that mirrors its edge as ImGui's wrap position.
    ImGui::PushTextWrapPos(wrap ? layout::textWrapPos() : -1.0f);
    if (align > 0.0f && !wrap) {
        const float w = ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + alignOffset(w, layout::availableWidth(), align));
    }
    ImGui::TextUnformatted(s.data(), s.data() + s.size());
    ImGui::PopTextWrapPos();
    if (color) ImGui::PopStyleColor();
}

std::optional<ImVec2> textSize(std::string_view s, TextStyle style, bool wrap, float align, float maxWidth) {
    if (wrap && layout::shrinkToFit()) return std::nullopt;
    // ImGui::TextUnformatted measures unwrapped text up to this long in one CalcTextSize(); longer text goes another
    // way. Wrapping text is measured in one, wrapped at the wrap position (at least a pixel from the cursor).
    if (!wrap && s.size() > 2000) return std::nullopt;
    FontScope fs{style};
    ImVec2 size = ImGui::CalcTextSize(s.data(), s.data() + s.size(), false, wrap ? std::max(1.0f, maxWidth) : 0.0f);
    if (align > 0.0f && !wrap) size.x += alignOffset(size.x, maxWidth, align);
    return size;
}

ImVec2 textInSize(std::string_view s, TextStyle style, float width) {
    FontScope fs{style};
    return {std::max(0.0f, width), ImGui::CalcTextSize(s.data(), s.data() + s.size()).y};
}

void textIn(std::string_view s, TextStyle style, std::optional<Color> color, float width, float align) {
    FontScope fs{style};
    const char* begin = s.data();
    const char* end = begin + s.size();
    const ImVec2 text = ImGui::CalcTextSize(begin, end);
    const ImVec2 box{std::max(0.0f, width), text.y};
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(box);
    const ImVec2 min{p.x + alignOffset(text.x, box.x, align), p.y};
    const ImVec2 max{p.x + box.x, p.y + box.y};
    if (s.empty() || !rectVisible(ImGui::GetWindowDrawList(), p, max)) return;
    if (color) ImGui::PushStyleColor(ImGuiCol_Text, color->vec4());
    if (s.find('\n') == std::string_view::npos)
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), min, max, max.x, begin, end, &text);
    else
        ImGui::RenderTextClipped(min, max, begin, end, &text);
    if (color) ImGui::PopStyleColor();
}

ImVec2 iconSize(float size) {
    const float px = dp(size);
    const float side = px > 0.0f ? std::min(px, maxIconPixels) : 0.0f; // also NaN
    return {side, side};
}

void icon(std::string_view name, float size, std::optional<Color> color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 box = iconSize(size);
    ImGui::Dummy(box);
    drawIcon(ImGui::GetWindowDrawList(), name, {p.x + box.x * 0.5f, p.y + box.y * 0.5f}, size,
             itemDisabled() ? disabledContent() : color.value_or(scheme().onSurfaceVariant));
}

void tooltip(std::string_view s) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        // Hovered but the tooltip delay hasn't passed: keep frames coming until it shows.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) requestFrameAfter(0.1);
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_PopupBg, scheme().inverseSurface.vec4());
    ImGui::PushStyleColor(ImGuiCol_Text, scheme().inverseOnSurface.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(8), dp(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(shape().extraSmall));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::BeginTooltip();
    {
        FontScope fs{TextStyle::BodySmall};
        ImGui::TextUnformatted(s.data(), s.data() + s.size());
    }
    ImGui::EndTooltip();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

ImVec2 dividerSize(float width) { return {width, dp(1)}; }

void divider(float insetStart, float insetEnd, float width) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = width > 0.0f ? width : layout::availableWidth();
    ImGui::Dummy(dividerSize(w));
    hLine(ImGui::GetWindowDrawList(), p.x + dp(insetStart), p.x + w - dp(insetEnd), p.y, scheme().outlineVariant,
          dp(1));
}

} // namespace imlui::md
