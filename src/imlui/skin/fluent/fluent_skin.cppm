// FluentSkin: the Fluent 2 (Windows 11) look, registered as "fluent" (`theme { design = "fluent" }`).
//
// Driven by its tokens (fluentTokens() with the theme's `tokens` overrides) and its colors (derived from the scheme's
// roles, imlui.theme.fluent_colors). Each control it draws lives in a module of free functions under
// src/imlui/skin/fluent/ taking a fluent::Look; the overrides here are one-line forwards. A control it doesn't
// override yet stays Material.
//
// The overrides come in three groups, each in its own delimited section (imports, declarations, forwards), so the
// groups are written in parallel without touching each other's lines: (A) buttons, toggles and ranges, (B) inputs,
// (C) lists, menus, navigation, containers, messages, and the element chrome. Add a group's lines just above its
// "end" marker. See docs/architecture.md, "Adding a Fluent control".
export module imlui.skin.fluent.fluent_skin;

import std;

import imgui;
import imlui.color;
import imlui.skin.base;
import imlui.skin.fluent.common;
import imlui.theme.fluent_colors;
import imlui.theme.skin_tokens;
import imlui.widgets.button;
import imlui.widgets.card;
import imlui.widgets.chart;
import imlui.widgets.dropdown;
import imlui.widgets.list;
import imlui.widgets.selection;
import imlui.widgets.tabs;
import imlui.widgets.text_field;
// ---- (A) buttons, toggles and ranges: imports
import imlui.skin.fluent.button;
import imlui.skin.fluent.chip;
import imlui.skin.fluent.icon_button;
import imlui.skin.fluent.progress;
import imlui.skin.fluent.segmented;
import imlui.skin.fluent.slider;
import imlui.skin.fluent.toggles;
// ---- end (A) imports
// ---- (B) inputs: imports
// ---- end (B) imports
// ---- (C) lists, menus, navigation, containers, messages, chrome: imports
import imlui.skin.fluent.chrome;
// ---- end (C) imports

export namespace imlui {

/// The Fluent skin. Subclass it to change its tokens or colors for every Fluent control (override tokens() or
/// colors()), or to draw some controls differently.
class FluentSkin : public Skin {
public:
    [[nodiscard]] std::string_view name() const override;

    /// fluentTokens() with the theme's `tokens` overrides (fluentTokensFor(theme())), made again when the theme
    /// changes.
    [[nodiscard]] const SkinTokens& tokens() const override;
    /// The colors the Fluent controls draw with: fluent::colorsFor(theme().scheme, theme().dark), made again when the
    /// theme changes.
    [[nodiscard]] virtual const fluent::Colors& colors() const;
    /// {tokens(), colors()}: what the forwards pass to the Fluent control functions.
    [[nodiscard]] fluent::Look look() const;

    // ======== (A) buttons, toggles and ranges ========
    // button, iconButton, checkbox, radio, switchToggle, chip, segmented, slider, progress, circularProgress
    bool button(const char* label, md::ButtonVariant variant, ImVec2 size, std::string_view iconName) const override;
    [[nodiscard]] ImVec2 buttonSize(const char* label, md::ButtonVariant variant, ImVec2 size,
                                    std::string_view iconName) const override;
    bool iconButton(const char* id, std::string_view iconName, md::IconButtonVariant variant, bool toggle,
                    bool selected, std::optional<Color> tint) const override;
    [[nodiscard]] ImVec2 iconButtonSize() const override;
    bool checkbox(const char* label, bool* v) const override;
    [[nodiscard]] ImVec2 checkboxSize(const char* label) const override;
    bool radio(const char* label, bool selected) const override;
    [[nodiscard]] ImVec2 radioSize(const char* label) const override;
    bool switchToggle(const char* label, bool* v, bool withIcons) const override;
    [[nodiscard]] ImVec2 switchSize(const char* label) const override;
    bool chip(const char* label, bool selected, md::ChipVariant variant, std::string_view leadingIcon) const override;
    [[nodiscard]] ImVec2 chipSize(const char* label, bool selected, md::ChipVariant variant,
                                  std::string_view leadingIcon) const override;
    bool segmented(const char* id, const std::vector<std::string>& items, int* current, float width,
                   const std::vector<std::string>& icons) const override;
    [[nodiscard]] ImVec2 segmentedSize(const std::vector<std::string>& items, float width) const override;
    bool slider(const char* id, double* v, double vmin, double vmax, double step, float width,
                const char* valueFormat) const override;
    [[nodiscard]] ImVec2 sliderSize(float width) const override;
    void progress(float fraction, float width) const override;
    [[nodiscard]] ImVec2 progressSize(float width) const override;
    void circularProgress(float fraction, float diameter, float thickness) const override;
    [[nodiscard]] ImVec2 circularProgressSize(float diameter) const override;
    // ======== end (A) ========

    // ======== (B) inputs ========
    // textField, spinBox, dropdown, colorField (composites: drawn whole, not through md::)
    // ======== end (B) ========

    // ======== (C) lists, menus, navigation, containers, messages; element chrome ========
    // listItem, expander, menuItem, beginMenu/endMenu, beginAnchoredMenu/endAnchoredMenu, tabs, navigationItem,
    // navigationBarItem, navigationRailItem, beginModalDrawer/endModalDrawer, beginCard/endCard/cardSize,
    // beginDialog/endDialog, snackbar, tooltip, divider, tableHeaderCell, tableCell, chart; the chrome hooks
    [[nodiscard]] std::optional<Color> applicationBackground() const override;
    [[nodiscard]] WindowChrome windowChrome() const override;
    [[nodiscard]] float scrollViewRounding() const override;
    // ======== end (C) ========
};

} // namespace imlui

module :private;

namespace imlui {

std::string_view FluentSkin::name() const { return fluent::designName; }
const SkinTokens& FluentSkin::tokens() const { return fluent::themeTokens(); }
const fluent::Colors& FluentSkin::colors() const { return fluent::themeColors(); }
fluent::Look FluentSkin::look() const { return {tokens(), colors()}; }

// ======== (A) buttons, toggles and ranges ========
bool FluentSkin::button(const char* label, md::ButtonVariant variant, ImVec2 size, std::string_view iconName) const {
    return fluent::button(look(), label, variant, size, iconName);
}
ImVec2 FluentSkin::buttonSize(const char* label, md::ButtonVariant variant, ImVec2 size,
                              std::string_view iconName) const {
    return fluent::buttonSize(look(), label, variant, size, iconName);
}
bool FluentSkin::iconButton(const char* id, std::string_view iconName, md::IconButtonVariant variant, bool toggle,
                            bool selected, std::optional<Color> tint) const {
    return fluent::iconButton(look(), id, iconName, variant, toggle, selected, tint);
}
ImVec2 FluentSkin::iconButtonSize() const { return fluent::iconButtonSize(look()); }
bool FluentSkin::checkbox(const char* label, bool* v) const { return fluent::checkbox(look(), label, v); }
ImVec2 FluentSkin::checkboxSize(const char* label) const { return fluent::checkboxSize(look(), label); }
bool FluentSkin::radio(const char* label, bool selected) const { return fluent::radio(look(), label, selected); }
ImVec2 FluentSkin::radioSize(const char* label) const { return fluent::radioSize(look(), label); }
bool FluentSkin::switchToggle(const char* label, bool* v, bool withIcons) const {
    return fluent::switchToggle(look(), label, v, withIcons);
}
ImVec2 FluentSkin::switchSize(const char* label) const { return fluent::switchSize(look(), label); }
bool FluentSkin::chip(const char* label, bool selected, md::ChipVariant variant, std::string_view leadingIcon) const {
    return fluent::chip(look(), label, selected, variant, leadingIcon);
}
ImVec2 FluentSkin::chipSize(const char* label, bool selected, md::ChipVariant variant,
                            std::string_view leadingIcon) const {
    return fluent::chipSize(look(), label, selected, variant, leadingIcon);
}
bool FluentSkin::segmented(const char* id, const std::vector<std::string>& items, int* current, float width,
                           const std::vector<std::string>& icons) const {
    return fluent::segmented(look(), id, items, current, width, icons);
}
ImVec2 FluentSkin::segmentedSize(const std::vector<std::string>& items, float width) const {
    return fluent::segmentedSize(look(), items, width);
}
bool FluentSkin::slider(const char* id, double* v, double vmin, double vmax, double step, float width,
                        const char* valueFormat) const {
    return fluent::slider(look(), id, v, vmin, vmax, step, width, valueFormat);
}
ImVec2 FluentSkin::sliderSize(float width) const { return fluent::sliderSize(look(), width); }
void FluentSkin::progress(float fraction, float width) const { fluent::progress(look(), fraction, width); }
ImVec2 FluentSkin::progressSize(float width) const { return fluent::progressSize(look(), width); }
void FluentSkin::circularProgress(float fraction, float diameter, float thickness) const {
    fluent::circularProgress(look(), fraction, diameter, thickness);
}
ImVec2 FluentSkin::circularProgressSize(float diameter) const { return fluent::circularProgressSize(look(), diameter); }
// ======== end (A) ========

// ======== (B) inputs ========
// ======== end (B) ========

// ======== (C) lists, menus, navigation, containers, messages; element chrome ========
std::optional<Color> FluentSkin::applicationBackground() const { return fluent::applicationBackground(look()); }
Skin::WindowChrome FluentSkin::windowChrome() const { return fluent::windowChrome(look()); }
float FluentSkin::scrollViewRounding() const { return fluent::scrollViewRounding(look()); }
// ======== end (C) ========

} // namespace imlui
