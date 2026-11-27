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
// -- (C1) lists, menus, navigation
import imlui.skin.fluent.list;
import imlui.skin.fluent.menu;
import imlui.skin.fluent.navigation;
import imlui.skin.fluent.navigation_drawer;
import imlui.skin.fluent.tabs;
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
    bool navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                        std::string_view badge, float width) const override;
    [[nodiscard]] ImVec2 navigationItemSize(float width) const override;
    bool navigationBarItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                           std::string_view badge, float width) const override;
    [[nodiscard]] ImVec2 navigationBarItemSize(float width) const override;
    bool navigationRailItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                            std::string_view badge, float width) const override;
    [[nodiscard]] ImVec2 navigationRailItemSize(float width) const override;
    bool beginModalDrawer(const char* id, bool* open, float width) const override;
    void endModalDrawer() const override;
    [[nodiscard]] Color navigationBarColor() const override;
    [[nodiscard]] Color navigationRailColor() const override;
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
// ======== end (C) ========

} // namespace imlui
