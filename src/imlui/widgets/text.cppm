// Text, standalone icons, tooltips and dividers.
export module imlui.widgets.text;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.typography;
import imlui.theme.font_scope;
import imlui.ui.frame_request;
import imlui.widgets.common;

export namespace imlui::md {

/// Text in a Material type style. `align` (0..1) positions unwrapped text in the available width.
void text(std::string_view s, TextStyle style, std::optional<Color> color = std::nullopt, bool wrap = false,
          float align = 0.0f);

/// A standalone icon item (size in dp).
void icon(std::string_view name, float size = 24.0f, std::optional<Color> color = std::nullopt);

/// Plain tooltip (inverseSurface) for the last item.
void tooltip(std::string_view s);

/// Full-width 1dp outlineVariant line.
void divider(float insetStart = 0.0f, float insetEnd = 0.0f);

} // namespace imlui::md

module :private;

namespace imlui::md {

void text(std::string_view s, TextStyle style, std::optional<Color> color, bool wrap, float align) {
    FontScope fs{style};
    if (color) ImGui::PushStyleColor(ImGuiCol_Text, color->vec4());
    if (wrap) ImGui::PushTextWrapPos(0.0f);
    if (align > 0.0f && !wrap) {
        float w = ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * align);
    }
    ImGui::TextUnformatted(s.data(), s.data() + s.size());
    if (wrap) ImGui::PopTextWrapPos();
    if (color) ImGui::PopStyleColor();
}

void icon(std::string_view name, float size, std::optional<Color> color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({dp(size), dp(size)});
    drawIcon(ImGui::GetWindowDrawList(), name, {p.x + dp(size) * 0.5f, p.y + dp(size) * 0.5f}, size,
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

void divider(float insetStart, float insetEnd) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy({w, dp(1)});
    hLine(ImGui::GetWindowDrawList(), p.x + dp(insetStart), p.x + w - dp(insetEnd), p.y, scheme().outlineVariant,
          dp(1));
}

} // namespace imlui::md
