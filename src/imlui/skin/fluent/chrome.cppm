// Fluent element chrome: the looks elements draw themselves around the controls (Skin's chrome hooks), Fluent's.
// The backgrounds at an app's root (the ApplicationWindow, a Window, a ScrollView's corners), the NavigationBar and
// NavigationRail containers, the TopAppBar, the Table, the Separator's label and the Link.
export module imlui.skin.fluent.chrome;

import std;

import imgui;
import imlui.color;
import imlui.skin.base;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.theme;
import imlui.widgets.common;
import imlui.widgets.surface;
import imlui.widgets.top_app_bar;

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

// -- (C2) bars, tables, separators, links

/// WinUI's title/command bar: a 48dp top row (Small, Center); Medium and Large put the title below it in a
/// bar of 104dp and 128dp. The current surface's color, the title in the primary text color.
[[nodiscard]] Skin::TopAppBarChrome topAppBarChrome(const Look& look, md::TopAppBarVariant variant);
/// Divider-colored rules, the selected row in the subtle hover fill with primary text, `paddingX` cell padding, a
/// header as high as a control.
[[nodiscard]] Skin::TableChrome tableChrome(const Look& look);
/// The secondary text color.
[[nodiscard]] Color separatorTextColor(const Look& look);
/// WinUI's HyperlinkButton text: the accent text color.
[[nodiscard]] std::optional<Color> linkColor(const Look& look);
/// A broken Image's placeholder: the standard control fill with its stroke, a broken-image icon (at most iconSize) in
/// the secondary text color.
void brokenImage(const Look& look, ImVec2 a, ImVec2 b, float radius);

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

// -- (C2) bars, tables, separators, links

Skin::TopAppBarChrome topAppBarChrome(const Look& look, md::TopAppBarVariant variant) {
    const float row = dp(48);
    return {.rowHeight = row,
            .height = variant == md::TopAppBarVariant::Medium  ? dp(104)
                      : variant == md::TopAppBarVariant::Large ? dp(128)
                                                               : row,
            .background = md::surfaceColor(),
            .title = look.colors.text};
}

Skin::TableChrome tableChrome(const Look& look) {
    const Colors& c = look.colors;
    return {.border = c.divider,
            .header = c.subtleHover,
            .selectedText = c.text,
            .cellPaddingX = paddingX(look),
            .headerHeight = controlHeight(look)};
}

Color separatorTextColor(const Look& look) { return look.colors.textSecondary; }
std::optional<Color> linkColor(const Look& look) { return look.colors.accentText; }

void brokenImage(const Look& look, ImVec2 a, ImVec2 b, float radius) {
    const Colors& c = look.colors;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawControl(dl, a, b, radius, {c.control, c.controlStroke, c.controlStroke, c.textSecondary}, strokeWidth(look));
    const float icon = std::min(iconSize(look) + 4.0f, std::min(b.x - a.x, b.y - a.y) / dp(1.0f) * 0.6f);
    md::drawIcon(dl, "broken_image", {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, icon, c.textSecondary);
}

} // namespace imlui::fluent
