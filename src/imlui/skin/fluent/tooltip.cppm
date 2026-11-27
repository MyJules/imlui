// Fluent tooltip: WinUI's ToolTip. The flyout fill with a flyout-stroke border of the stroke width, the control
// radius, 8dp x 6dp padding, caption text (bodySmall, 12dp) in the primary text color, wrapping at WinUI's 320dp
// maximum width. When it shows is md::tooltip's: on hover after ImGui's tooltip delay, also over a disabled item.
export module imlui.skin.fluent.tooltip;

import std;

import imgui;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.font_scope;
import imlui.theme.typography;
import imlui.ui.frame_request;

export namespace imlui::fluent {

/// A tooltip for the last item (md::tooltip's parameters).
void tooltip(const Look& look, std::string_view s);

/// ToolTip's maximum width, dp (its text wraps inside it).
inline constexpr float tooltipMaxWidth = 320.0f;
/// The tooltip's text style (WinUI's caption).
inline constexpr TextStyle tooltipStyle = TextStyle::BodySmall;

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

void tooltip(const Look& look, std::string_view s) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        // Hovered but the tooltip delay hasn't passed: keep frames coming until it shows.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) requestFrameAfter(0.1);
        return;
    }
    const Colors& c = look.colors;
    const float padX = dp(8);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, c.flyout.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, c.flyoutStroke.vec4());
    ImGui::PushStyleColor(ImGuiCol_Text, c.text.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padX, dp(6)});
    // A tooltip window isn't a popup to ImGui: it takes the window rounding and border.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, controlRadius(look));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, strokeWidth(look));
    ImGui::BeginTooltip();
    {
        const FontScope fs{tooltipStyle};
        ImGui::PushTextWrapPos(dp(tooltipMaxWidth) - padX); // window-local: the padding is on both sides
        ImGui::TextUnformatted(s.data(), s.data() + s.size());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndTooltip();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

} // namespace imlui::fluent
