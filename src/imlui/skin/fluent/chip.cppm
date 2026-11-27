// Fluent chips. WinUI has no chip; the closest Fluent control is a ToggleButton (Fluent 2's interaction tag looks the
// same: a control-height token with the control radius), so a chip is drawn as one, for every md::ChipVariant:
//
//   filter, suggestion, input -> unselected: a standard toggle button (neutral fill, 1dp stroke with a darker bottom
//                                edge); selected: a checked toggle button (accent, following `tokens.buttonFill`)
//   assist                    -> always a standard button (an assist chip has no selected look, as in Material)
//
// A selected filter chip shows no check (a checked ToggleButton shows its fill); its leading icon, when it has one,
// stays. Height: controlHeight. Width: paddingX + icon + gap + label + paddingX. md::chip's behavior: it reports
// clicks, and the element decides what a click toggles.
export module imlui.skin.fluent.chip;

import std;

import imgui;
import imlui.skin.fluent.common;
import imlui.widgets.common;
import imlui.widgets.selection;

export namespace imlui::fluent {

/// A chip (md::chip's parameters); returns whether it was clicked.
bool chip(const Look& look, const char* label, bool selected, md::ChipVariant variant, std::string_view leadingIcon);
/// The size chip() takes with the same arguments.
[[nodiscard]] ImVec2 chipSize(const Look& look, const char* label, bool selected, md::ChipVariant variant,
                              std::string_view leadingIcon);
/// The fill family a chip draws with: accent when selected (but an assist chip), else standard.
[[nodiscard]] Fill chipFill(md::ChipVariant variant, bool selected);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

struct ChipLayout {
    Content content;
    float w = 0, h = 0;
};

ChipLayout chipLayout(const Look& look, const char* label, std::string_view leadingIcon) {
    ChipLayout l;
    l.content = content(look, md::visibleLabel(label), leadingIcon);
    l.h = controlHeight(look);
    l.w = l.content.width + 2.0f * paddingX(look);
    return l;
}

} // namespace

Fill chipFill(md::ChipVariant variant, bool selected) {
    return selected && variant != md::ChipVariant::Assist ? Fill::Accent : Fill::Standard;
}

ImVec2 chipSize(const Look& look, const char* label, [[maybe_unused]] bool selected,
                [[maybe_unused]] md::ChipVariant variant, std::string_view leadingIcon) {
    const ChipLayout l = chipLayout(look, label, leadingIcon);
    return {l.w, l.h};
}

bool chip(const Look& look, const char* label, bool selected, md::ChipVariant variant, std::string_view leadingIcon) {
    const ChipLayout l = chipLayout(look, label, leadingIcon);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(label, {l.w, l.h});
    const ItemState item = itemState();
    const ControlColors c = controlColors(look, chipFill(variant, selected), stateOf(item));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{p.x + l.w, p.y + l.h};
    const float radius = controlRadius(look);
    drawControl(dl, p, q, radius, c, strokeWidth(look));
    drawContent(dl, l.content, p.x + paddingX(look), p.y + l.h * 0.5f, c.text);
    if (item.focused) drawFocusRect(dl, p, q, radius, look);
    return clicked;
}

} // namespace imlui::fluent
