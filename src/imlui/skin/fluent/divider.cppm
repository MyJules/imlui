// Fluent divider: a 1dp line in the divider color (WinUI's DividerStrokeColorDefault) across `width` (0: the
// available width), with md::divider's insets and size.
export module imlui.skin.fluent.divider;

import std;

import imgui;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.ui.layout_box;
import imlui.widgets.common;
import imlui.widgets.text;

export namespace imlui::fluent {

/// Draws a divider (md::divider's parameters: insets in dp, `width` 0 for the available width).
void divider(const Look& look, float insetStart, float insetEnd, float width);
/// The size of divider() `width` wide: 1dp high, like md::dividerSize.
[[nodiscard]] ImVec2 dividerSize(const Look& look, float width);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

ImVec2 dividerSize(const Look&, float width) { return md::dividerSize(width); }

void divider(const Look& look, float insetStart, float insetEnd, float width) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = width > 0.0f ? width : layout::availableWidth();
    const ImVec2 size = dividerSize(look, w);
    ImGui::Dummy(size);
    md::hLine(ImGui::GetWindowDrawList(), p.x + dp(insetStart), p.x + w - dp(insetEnd), p.y, look.colors.divider,
              size.y);
}

} // namespace imlui::fluent
