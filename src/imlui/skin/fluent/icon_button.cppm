// Fluent icon buttons: a square Fluent button holding one icon (WinUI's icon-only Button, AppBarButton and
// ToggleButton), for every md::IconButtonVariant.
//
//   standard            -> subtle (transparent at rest); a selected toggle keeps the subtle fill with an accent icon
//   filled              -> accent; an unselected toggle is standard (an unchecked ToggleButton)
//   tonal, outlined     -> standard; a selected toggle is accent (a checked ToggleButton)
//
// Size: controlHeight square (md's is a 48dp touch target), the control radius, the token's iconSize. `tint` colors
// the icon unless disabled. Same return value as md::iconButton (clicked); the element flips a toggle.
export module imlui.skin.fluent.icon_button;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.fluent_colors;
import imlui.widgets.button;
import imlui.widgets.common;

export namespace imlui::fluent {

/// An icon button (md::iconButton's parameters); returns whether it was clicked.
bool iconButton(const Look& look, const char* id, std::string_view iconName, md::IconButtonVariant variant, bool toggle,
                bool selected, std::optional<Color> tint);
/// The size iconButton() takes: controlHeight square.
[[nodiscard]] ImVec2 iconButtonSize(const Look& look);
/// The fill family an icon button draws with.
[[nodiscard]] Fill iconButtonFill(md::IconButtonVariant variant, bool toggle, bool selected);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

Fill iconButtonFill(md::IconButtonVariant variant, bool toggle, bool selected) {
    switch (variant) {
    case md::IconButtonVariant::Standard: return Fill::Subtle;
    case md::IconButtonVariant::Filled: return toggle && !selected ? Fill::Standard : Fill::Accent;
    case md::IconButtonVariant::Tonal:
    case md::IconButtonVariant::Outlined: return toggle && selected ? Fill::Accent : Fill::Standard;
    }
    return Fill::Subtle;
}

ImVec2 iconButtonSize(const Look& look) {
    const float h = controlHeight(look);
    return {h, h};
}

bool iconButton(const Look& look, const char* id, std::string_view iconName, md::IconButtonVariant variant, bool toggle,
                bool selected, std::optional<Color> tint) {
    const ImVec2 size = iconButtonSize(look);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(id, size);
    const ItemState item = itemState();
    const State state = stateOf(item);
    ControlColors c = controlColors(look, iconButtonFill(variant, toggle, selected), state);
    if (state != State::Disabled) {
        if (variant == md::IconButtonVariant::Standard && toggle && selected) c.text = look.colors.accentText;
        if (tint) c.text = *tint;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{p.x + size.x, p.y + size.y};
    const float radius = controlRadius(look);
    drawControl(dl, p, q, radius, c, strokeWidth(look));
    md::drawIcon(dl, iconName, {std::round((p.x + q.x) * 0.5f), std::round((p.y + q.y) * 0.5f)}, iconSize(look),
                 c.text);
    if (item.focused) drawFocusRect(dl, p, q, radius, look);
    return clicked;
}

} // namespace imlui::fluent
