// Header and body cell helpers for Material data tables (inside ImGui::BeginTable).
module;

#include <optional>
#include <string_view>

export module imlui.widgets.table;

import imgui;
import imlui.color;
import imlui.theme.typography;
import imlui.widgets.common;

export namespace imlui::md {

/// Header cell text (titleSmall, onSurfaceVariant), vertically centered in `rowH`.
void tableHeaderCell(std::string_view text, float rowH);
/// Body cell text (bodyMedium), vertically centered in `rowH`.
void tableCell(std::string_view text, float rowH, std::optional<Color> color = std::nullopt);

} // namespace imlui::md

module :private;

namespace imlui::md {

void tableHeaderCell(std::string_view text, float rowH) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = measure(TextStyle::TitleSmall, text);
    ImGui::Dummy({ts.x, rowH});
    drawText(ImGui::GetWindowDrawList(), TextStyle::TitleSmall, {p.x, p.y + (rowH - ts.y) * 0.5f},
             scheme().onSurfaceVariant, text);
}

void tableCell(std::string_view text, float rowH, std::optional<Color> color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = measure(TextStyle::BodyMedium, text);
    ImGui::Dummy({ts.x, rowH});
    drawText(ImGui::GetWindowDrawList(), TextStyle::BodyMedium, {p.x, p.y + (rowH - ts.y) * 0.5f},
             itemDisabled() ? disabledContent() : color.value_or(scheme().onSurface), text);
}

} // namespace imlui::md
