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
import imlui.widgets.top_app_bar;
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
import imlui.skin.fluent.inputs;
// ---- end (B) imports
// ---- (C) lists, menus, navigation, containers, messages, chrome: imports
import imlui.skin.fluent.chrome;
// -- (C1) lists, menus, navigation
import imlui.skin.fluent.list;
import imlui.skin.fluent.menu;
import imlui.skin.fluent.navigation;
import imlui.skin.fluent.navigation_drawer;
import imlui.skin.fluent.tabs;
// -- (C2) containers, messages, tables, charts, chrome
import imlui.skin.fluent.card;
import imlui.skin.fluent.chart;
import imlui.skin.fluent.dialog;
import imlui.skin.fluent.divider;
import imlui.skin.fluent.snackbar;
import imlui.skin.fluent.table;
import imlui.skin.fluent.tooltip;
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
    // -- (C1) lists, menus, navigation
    bool listItem(const char* id, const md::ListItemOptions& o) const override;
    [[nodiscard]] ImVec2 listItemSize(const md::ListItemOptions& o) const override;
    bool expander(const char* id, std::string_view title, std::string_view supporting, bool* open,
                  float width) const override;
    [[nodiscard]] ImVec2 expanderSize(std::string_view supporting, float width) const override;
    bool menuItem(const char* id, std::string_view label, std::string_view leadingIcon, std::string_view trailing,
                  bool selected, bool closeOnClick, bool reserveIcon) const override;
    bool beginMenu(const char* label, float minWidth) const override;
    void endMenu() const override;
    bool beginAnchoredMenu(const char* popupId, ImVec2 a, ImVec2 b, float minWidth) const override;
    void endAnchoredMenu() const override;
    bool tabs(const char* id, const std::vector<md::TabInfo>& items, int* current, bool secondary,
              float width) const override;
    [[nodiscard]] ImVec2 tabsSize(const std::vector<md::TabInfo>& items, bool secondary, float width) const override;
    bool navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                        std::string_view badge, float width) const override;
    [[nodiscard]] ImVec2 navigationItemSize(float width) const override;
    bool navigationBarItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                           std::string_view badge, float width) const override;
    [[nodiscard]] ImVec2 navigationBarItemSize(float width) const override;
    [[nodiscard]] ImVec2 navigationBarSize(float width) const override;
    bool navigationRailItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                            std::string_view badge, float width) const override;
    [[nodiscard]] ImVec2 navigationRailItemSize(float width) const override;
    bool beginModalDrawer(const char* id, bool* open, float width) const override;
    void endModalDrawer() const override;
    [[nodiscard]] Color navigationBarColor() const override;
    [[nodiscard]] Color navigationRailColor() const override;
    // -- (C2) containers, messages, tables, charts, chrome
    bool beginCard(const char* id, md::CardVariant variant, float width, float padding, float height,
                   std::optional<Color> color, float rounding) const override;
    void endCard() const override;
    [[nodiscard]] ImVec2 cardSize(float width, float padding, float height, float contentHeight) const override;
    bool beginDialog(const char* title, bool* open, float width, std::string_view heroIcon) const override;
    void endDialog() const override;
    bool snackbar(std::string_view message, std::string_view action, float t, bool closeIcon,
                  bool* closed) const override;
    void tooltip(std::string_view s) const override;
    void divider(float insetStart, float insetEnd, float width) const override;
    [[nodiscard]] ImVec2 dividerSize(float width) const override;
    void tableHeaderCell(std::string_view text, float rowH) const override;
    void tableCell(std::string_view text, float rowH, std::optional<Color> color) const override;
    void chart(const char* id, const std::vector<float>& values, const md::ChartOptions& o) const override;
    [[nodiscard]] ImVec2 chartSize(const md::ChartOptions& o) const override;
    void brokenImage(ImVec2 a, ImVec2 b, float radius) const override;
    [[nodiscard]] TopAppBarChrome topAppBarChrome(md::TopAppBarVariant variant) const override;
    [[nodiscard]] TableChrome tableChrome() const override;
    [[nodiscard]] Color separatorTextColor() const override;
    [[nodiscard]] std::optional<Color> linkColor() const override;
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
// -- (C1) lists, menus, navigation
bool FluentSkin::listItem(const char* id, const md::ListItemOptions& o) const {
    return fluent::listItem(look(), id, o);
}
ImVec2 FluentSkin::listItemSize(const md::ListItemOptions& o) const { return fluent::listItemSize(look(), o); }
bool FluentSkin::expander(const char* id, std::string_view title, std::string_view supporting, bool* open,
                          float width) const {
    return fluent::expander(look(), id, title, supporting, open, width);
}
ImVec2 FluentSkin::expanderSize(std::string_view supporting, float width) const {
    return fluent::expanderSize(look(), supporting, width);
}
bool FluentSkin::menuItem(const char* id, std::string_view label, std::string_view leadingIcon,
                          std::string_view trailing, bool selected, bool closeOnClick, bool reserveIcon) const {
    return fluent::menuItem(look(), id, label, leadingIcon, trailing, selected, closeOnClick, reserveIcon);
}
bool FluentSkin::beginMenu(const char* label, float minWidth) const {
    return fluent::beginMenu(look(), label, minWidth);
}
void FluentSkin::endMenu() const { fluent::endMenu(); }
bool FluentSkin::beginAnchoredMenu(const char* popupId, ImVec2 a, ImVec2 b, float minWidth) const {
    return fluent::beginAnchoredMenu(look(), popupId, a, b, minWidth);
}
void FluentSkin::endAnchoredMenu() const { fluent::endAnchoredMenu(); }
bool FluentSkin::tabs(const char* id, const std::vector<md::TabInfo>& items, int* current, bool secondary,
                      float width) const {
    return fluent::tabs(look(), id, items, current, secondary, width);
}
ImVec2 FluentSkin::tabsSize(const std::vector<md::TabInfo>& items, bool secondary, float width) const {
    return fluent::tabsSize(look(), items, secondary, width);
}
bool FluentSkin::navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                                std::string_view badge, float width) const {
    return fluent::navigationItem(look(), id, iconName, label, selected, badge, width);
}
ImVec2 FluentSkin::navigationItemSize(float width) const { return fluent::navigationItemSize(look(), width); }
bool FluentSkin::navigationBarItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                                   std::string_view badge, float width) const {
    return fluent::navigationBarItem(look(), id, iconName, label, selected, badge, width);
}
ImVec2 FluentSkin::navigationBarItemSize(float width) const { return fluent::navigationBarItemSize(look(), width); }
ImVec2 FluentSkin::navigationBarSize(float width) const { return fluent::navigationBarSize(look(), width); }
bool FluentSkin::navigationRailItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                                    std::string_view badge, float width) const {
    return fluent::navigationRailItem(look(), id, iconName, label, selected, badge, width);
}
ImVec2 FluentSkin::navigationRailItemSize(float width) const { return fluent::navigationRailItemSize(look(), width); }
bool FluentSkin::beginModalDrawer(const char* id, bool* open, float width) const {
    return fluent::beginModalDrawer(look(), id, open, width);
}
void FluentSkin::endModalDrawer() const { fluent::endModalDrawer(); }
Color FluentSkin::navigationBarColor() const { return fluent::navigationBarColor(look()); }
Color FluentSkin::navigationRailColor() const { return fluent::navigationRailColor(look()); }
// -- (C2) containers, messages, tables, charts, chrome
bool FluentSkin::beginCard(const char* id, md::CardVariant variant, float width, float padding, float height,
                           std::optional<Color> color, float rounding) const {
    return fluent::beginCard(look(), id, variant, width, padding, height, color, rounding);
}
void FluentSkin::endCard() const { fluent::endCard(look()); }
ImVec2 FluentSkin::cardSize(float width, float padding, float height, float contentHeight) const {
    return fluent::cardSize(look(), width, padding, height, contentHeight);
}
bool FluentSkin::beginDialog(const char* title, bool* open, float width, std::string_view heroIcon) const {
    return fluent::beginDialog(look(), title, open, width, heroIcon);
}
void FluentSkin::endDialog() const { fluent::endDialog(look()); }
bool FluentSkin::snackbar(std::string_view message, std::string_view action, float t, bool closeIcon,
                          bool* closed) const {
    return fluent::snackbar(look(), message, action, t, closeIcon, closed);
}
void FluentSkin::tooltip(std::string_view s) const { fluent::tooltip(look(), s); }
void FluentSkin::divider(float insetStart, float insetEnd, float width) const {
    fluent::divider(look(), insetStart, insetEnd, width);
}
ImVec2 FluentSkin::dividerSize(float width) const { return fluent::dividerSize(look(), width); }
void FluentSkin::tableHeaderCell(std::string_view text, float rowH) const {
    fluent::tableHeaderCell(look(), text, rowH);
}
void FluentSkin::tableCell(std::string_view text, float rowH, std::optional<Color> color) const {
    fluent::tableCell(look(), text, rowH, color);
}
void FluentSkin::chart(const char* id, const std::vector<float>& values, const md::ChartOptions& o) const {
    fluent::chart(look(), id, values, o);
}
ImVec2 FluentSkin::chartSize(const md::ChartOptions& o) const { return fluent::chartSize(look(), o); }
void FluentSkin::brokenImage(ImVec2 a, ImVec2 b, float radius) const { fluent::brokenImage(look(), a, b, radius); }
Skin::TopAppBarChrome FluentSkin::topAppBarChrome(md::TopAppBarVariant variant) const {
    return fluent::topAppBarChrome(look(), variant);
}
Skin::TableChrome FluentSkin::tableChrome() const { return fluent::tableChrome(look()); }
Color FluentSkin::separatorTextColor() const { return fluent::separatorTextColor(look()); }
std::optional<Color> FluentSkin::linkColor() const { return fluent::linkColor(look()); }
// ======== end (C) ========

} // namespace imlui
