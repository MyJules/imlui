// Fluent element chrome: the looks elements draw themselves around the controls (Skin's chrome hooks), Fluent's.
// So far the backgrounds at an app's root: the ApplicationWindow, a Window, a ScrollView's corners. The bars, the
// table, the separator and the link keep Material's hooks (group C of the Fluent controls adds them here).
export module imlui.skin.fluent.chrome;

import std;

import imgui;
import imlui.color;
import imlui.skin.base;
import imlui.skin.fluent.common;
import imlui.theme.fluent_colors;
import imlui.theme.theme;

export namespace imlui::fluent {

/// Fluent's solid background, unless the theme overrides ImGui's WindowBg (`imgui = { WindowBg = ... }`).
[[nodiscard]] std::optional<Color> applicationBackground(const Look& look);
/// A layer over the background, a divider-colored border of the stroke width, title bars of the background.
[[nodiscard]] Skin::WindowChrome windowChrome(const Look& look);
/// The overlay radius.
[[nodiscard]] float scrollViewRounding(const Look& look);

// -- (C1) navigation containers
/// The NavigationBar's container: a layer over the background.
[[nodiscard]] Color navigationBarColor(const Look& look);
/// The NavigationRail's container: the background (NavigationView's pane is transparent over Mica).
[[nodiscard]] Color navigationRailColor(const Look& look);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

std::optional<Color> applicationBackground(const Look& look) {
    for (const auto& entry : theme().imguiColors)
        if (entry.first == ImGuiCol_WindowBg) return std::nullopt;
    return look.colors.background;
}

Skin::WindowChrome windowChrome(const Look& look) {
    const Colors& c = look.colors;
    return {.background = c.layer,
            .titleBar = c.background,
            .titleBarActive = c.background,
            .border = c.divider,
            .borderSize = std::max(1.0f, strokeWidth(look))};
}

float scrollViewRounding(const Look& look) { return overlayRadius(look); }

// -- (C1) navigation containers
Color navigationBarColor(const Look& look) { return look.colors.layer; }
Color navigationRailColor(const Look& look) { return look.colors.background; }

} // namespace imlui::fluent
