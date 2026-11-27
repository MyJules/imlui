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
// ---- end (A) imports
// ---- (B) inputs: imports
import imlui.skin.fluent.inputs;
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
    // ======== end (A) ========

    // ======== (B) inputs ========
    // textField, spinBox, dropdown, colorField (composites: drawn whole, not through md::)
    md::TextFieldResult textField(const char* id, std::string& value, const md::TextFieldOptions& o) const override;
    [[nodiscard]] ImVec2 textFieldSize(const md::TextFieldOptions& o) const override;
    bool spinBox(const char* id, double* v, double vmin, double vmax, double step, int decimals,
                 std::string_view label, float width, bool outlined) const override;
    [[nodiscard]] ImVec2 spinBoxSize(std::string_view label, float width, bool outlined) const override;
    bool dropdown(const char* id, const std::vector<std::string>& items, int* current, const md::DropdownOptions& o,
                  const std::vector<std::string>& icons) const override;
    [[nodiscard]] ImVec2 dropdownSize(const md::DropdownOptions& o) const override;
    bool colorField(const char* id, Color* c, std::string_view label, bool alpha, float width) const override;
    [[nodiscard]] ImVec2 colorFieldSize(Color c, std::string_view label, float width) const override;
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
// ======== end (A) ========

// ======== (B) inputs ========
md::TextFieldResult FluentSkin::textField(const char* id, std::string& value, const md::TextFieldOptions& o) const {
    return fluent::textField(look(), id, value, o);
}
ImVec2 FluentSkin::textFieldSize(const md::TextFieldOptions& o) const { return fluent::textFieldSize(look(), o); }
bool FluentSkin::spinBox(const char* id, double* v, double vmin, double vmax, double step, int decimals,
                         std::string_view label, float width, bool outlined) const {
    return fluent::spinBox(look(), id, v, vmin, vmax, step, decimals, label, width, outlined);
}
ImVec2 FluentSkin::spinBoxSize(std::string_view label, float width, bool outlined) const {
    return fluent::spinBoxSize(look(), label, width, outlined);
}
bool FluentSkin::dropdown(const char* id, const std::vector<std::string>& items, int* current,
                          const md::DropdownOptions& o, const std::vector<std::string>& icons) const {
    return fluent::dropdown(look(), id, items, current, o, icons);
}
ImVec2 FluentSkin::dropdownSize(const md::DropdownOptions& o) const { return fluent::dropdownSize(look(), o); }
bool FluentSkin::colorField(const char* id, Color* c, std::string_view label, bool alpha, float width) const {
    return fluent::colorField(look(), id, c, label, alpha, width);
}
ImVec2 FluentSkin::colorFieldSize(Color c, std::string_view label, float width) const {
    return fluent::colorFieldSize(look(), c, label, width);
}
// ======== end (B) ========

// ======== (C) lists, menus, navigation, containers, messages; element chrome ========
std::optional<Color> FluentSkin::applicationBackground() const { return fluent::applicationBackground(look()); }
Skin::WindowChrome FluentSkin::windowChrome() const { return fluent::windowChrome(look()); }
float FluentSkin::scrollViewRounding() const { return fluent::scrollViewRounding(look()); }
// ======== end (C) ========

} // namespace imlui
