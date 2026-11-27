// Fluent buttons: the accent, standard and subtle buttons of Fluent 2, for every md::ButtonVariant.
//
//   filled, tonal      -> accent (the token buttonFill: filled, or an accent outline)
//   outlined, elevated -> standard (a neutral fill, a stroke with a darker bottom edge)
//   text               -> subtle (transparent at rest)
//   fab                -> accent, larger (controlHeight + 16dp, the overlay radius, a 20dp icon); square when it has
//                         no label
//
// Height: controlHeight (unless `size.y` is set). Width: paddingX + icon + gap + label + paddingX, at least the
// height; `size.x` resolves like md::itemWidth. The reference pattern for the Fluent controls (docs/architecture.md,
// "Adding a Fluent control"): a layout function both the draw and the size function use, state from the item, colors
// from controlColors(), the background from drawControl(), the focus rectangle for keyboard focus.
export module imlui.skin.fluent.button;

import std;

import imgui;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.widgets.button;
import imlui.widgets.common;

export namespace imlui::fluent {

/// Draws a button (md::button's parameters); returns whether it was clicked.
bool button(const Look& look, const char* label, md::ButtonVariant variant, ImVec2 size, std::string_view iconName);
/// The size button() takes with the same arguments, without drawing it.
[[nodiscard]] ImVec2 buttonSize(const Look& look, const char* label, md::ButtonVariant variant, ImVec2 size,
                                std::string_view iconName);
/// The fill family a variant maps to.
[[nodiscard]] Fill variantFill(md::ButtonVariant variant);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

struct ButtonLayout {
    Content content;
    float w = 0, h = 0;
    float radius = 0;
};

ButtonLayout buttonLayout(const Look& look, const char* label, md::ButtonVariant variant, ImVec2 size,
                          std::string_view iconName) {
    const bool fab = variant == md::ButtonVariant::Fab;
    ButtonLayout l;
    l.content = content(look, md::visibleLabel(label), iconName, bodyStyle, fab ? iconSize(look) + 4.0f : 0.0f);
    l.h = size.y > 0 ? size.y : controlHeight(look) + (fab ? dp(16) : 0.0f);
    float natural = l.content.width + 2.0f * paddingX(look);
    if (fab && l.content.text.empty()) natural = l.h;
    l.w = md::itemWidth(size.x, std::max(natural, l.h));
    l.radius = fab ? overlayRadius(look) : controlRadius(look);
    return l;
}

} // namespace

Fill variantFill(md::ButtonVariant variant) {
    switch (variant) {
    case md::ButtonVariant::Filled:
    case md::ButtonVariant::Tonal:
    case md::ButtonVariant::Fab: return Fill::Accent;
    case md::ButtonVariant::Outlined:
    case md::ButtonVariant::Elevated: return Fill::Standard;
    case md::ButtonVariant::Text: return Fill::Subtle;
    }
    return Fill::Standard;
}

ImVec2 buttonSize(const Look& look, const char* label, md::ButtonVariant variant, ImVec2 size,
                  std::string_view iconName) {
    const ButtonLayout l = buttonLayout(look, label, variant, size, iconName);
    return {l.w, l.h};
}

bool button(const Look& look, const char* label, md::ButtonVariant variant, ImVec2 size, std::string_view iconName) {
    const ButtonLayout l = buttonLayout(look, label, variant, size, iconName);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(label, {l.w, l.h});
    const ItemState item = itemState();
    const ControlColors c = controlColors(look, variantFill(variant), stateOf(item));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{p.x + l.w, p.y + l.h};
    drawControl(dl, p, q, l.radius, c, strokeWidth(look));
    drawContent(dl, l.content, p.x + (l.w - l.content.width) * 0.5f, p.y + l.h * 0.5f, c.text);
    if (item.focused) drawFocusRect(dl, p, q, l.radius, look);
    return clicked;
}

} // namespace imlui::fluent
