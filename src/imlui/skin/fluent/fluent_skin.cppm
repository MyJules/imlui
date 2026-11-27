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
// ---- end (B) imports
// ---- (C) lists, menus, navigation, containers, messages, chrome: imports
import imlui.skin.fluent.chrome;
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
    [[nodiscard]] TopAppBarChrome topAppBarChrome(std::string_view variant) const override;
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
// ======== end (A) ========

// ======== (B) inputs ========
// ======== end (B) ========

// ======== (C) lists, menus, navigation, containers, messages; element chrome ========
std::optional<Color> FluentSkin::applicationBackground() const { return fluent::applicationBackground(look()); }
Skin::WindowChrome FluentSkin::windowChrome() const { return fluent::windowChrome(look()); }
float FluentSkin::scrollViewRounding() const { return fluent::scrollViewRounding(look()); }
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
Skin::TopAppBarChrome FluentSkin::topAppBarChrome(std::string_view variant) const {
    return fluent::topAppBarChrome(look(), variant);
}
Skin::TableChrome FluentSkin::tableChrome() const { return fluent::tableChrome(look()); }
Color FluentSkin::separatorTextColor() const { return fluent::separatorTextColor(look()); }
std::optional<Color> FluentSkin::linkColor() const { return fluent::linkColor(look()); }
// ======== end (C) ========

} // namespace imlui
