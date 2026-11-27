// Fluent table cells: WinUI's DataGrid text. A header cell in body-strong text (titleSmall, 14dp medium) in the
// secondary text color, a body cell in body text (bodyMedium) in the primary text color (or `color`: the selected
// row's), the disabled text color inside a disabled table. Both are vertically centered in `rowH` and take their
// text's width, like md::tableHeaderCell and md::tableCell. The table's rules, selection fill, padding and header
// height are its chrome (fluent::tableChrome).
export module imlui.skin.fluent.table;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.typography;
import imlui.widgets.common;

export namespace imlui::fluent {

/// A header cell (md::tableHeaderCell's parameters).
void tableHeaderCell(const Look& look, std::string_view text, float rowH);
/// A body cell (md::tableCell's parameters).
void tableCell(const Look& look, std::string_view text, float rowH, std::optional<Color> color);

/// The header cells' text style.
inline constexpr TextStyle tableHeaderStyle = TextStyle::TitleSmall;

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

/// `text` in `color`, or in `disabled` when its item is disabled.
void cell(TextStyle style, std::string_view text, float rowH, Color color, Color disabled) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 ts = md::measure(style, text);
    ImGui::Dummy({ts.x, rowH});
    md::drawText(ImGui::GetWindowDrawList(), style, {p.x, p.y + (rowH - ts.y) * 0.5f},
                 md::itemDisabled() ? disabled : color, text);
}

} // namespace

void tableHeaderCell(const Look& look, std::string_view text, float rowH) {
    cell(tableHeaderStyle, text, rowH, look.colors.textSecondary, look.colors.textDisabled);
}

void tableCell(const Look& look, std::string_view text, float rowH, std::optional<Color> color) {
    cell(bodyStyle, text, rowH, color.value_or(look.colors.text), look.colors.textDisabled);
}

} // namespace imlui::fluent
