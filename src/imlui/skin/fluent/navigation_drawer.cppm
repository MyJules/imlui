// Fluent modal navigation drawer: WinUI's NavigationView pane in overlay mode, for md::beginModalDrawer/endModalDrawer.
//
// Over the main viewport, the smoke color dims the app; the pane, in the flyout color with a flyoutStroke edge on its
// right and the overlay radius on its right corners, slides in from the left with a flyout shadow. Its content is a
// scrolling region with drawerPadding (4dp, scaled by the density) around it: the navigation items' own insets put
// their highlights 8dp from the pane's edge. The behavior is md's: the same window and IDs, the same slide, the scrim
// click and Escape (unless a popup over the drawer takes it) setting `*open` to false, the surface stack.
export module imlui.skin.fluent.navigation_drawer;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.widgets.common;
import imlui.widgets.navigation_drawer;
import imlui.widgets.surface;

export namespace imlui::fluent {

/// Begins the overlay pane (md::beginModalDrawer's parameters); returns true while its contents should be rendered,
/// then call endModalDrawer().
bool beginModalDrawer(const Look& look, const char* id, bool* open, float width);
void endModalDrawer();

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float drawerPadding = 4.0f; // dp around the pane's content
constexpr float shadowLevel = 2.0f;   // md::drawShadow's level, fully open

} // namespace

bool beginModalDrawer(const Look& look, const char* id, bool* open, float width) {
    const Colors& c = look.colors;
    // md's window, slide, scrim click and Escape.
    const std::optional<md::ModalDrawerFrame> f = md::beginModalDrawerWindow(id, open, width);
    if (!f) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    md::fillRoundRect(dl, f->viewportMin, f->viewportMax, 0.0f, c.smoke.withAlpha(c.smoke.a * f->t));
    const ImVec2 a = f->sheetMin, b = f->sheetMax;
    const float radius = overlayRadius(look);
    md::drawShadow(dl, a, b, radius, shadowLevel * f->t);
    md::fillRoundRect(dl, a, b, radius, c.flyout, ImDrawFlags_RoundCornersRight);
    const float stroke = strokeWidth(look);
    // The pane's right edge: its outline, the left, top and bottom sides lying outside the viewport.
    if (stroke > 0)
        md::strokeRoundRect(dl, {a.x - radius - stroke, a.y - stroke}, {b.x, b.y + stroke}, radius, c.flyoutStroke,
                            stroke, ImDrawFlags_RoundCornersRight);
    md::beginModalDrawerSheet(*f, dp(drawerPadding * look.tokens.density), c.flyout);
    return true;
}

void endModalDrawer() { md::endModalDrawer(); }

} // namespace imlui::fluent
