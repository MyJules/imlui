// Fluent cards: WinUI's card (a translucent card fill, a 1dp card stroke, the overlay radius), for every
// md::CardVariant.
//
//   elevated (default) -> the card fill and the card stroke (Fluent marks elevation with the stroke, not a shadow)
//   filled             -> the layer fill, no stroke
//   outlined           -> no fill of its own (the surface it is on), a divider-colored stroke
//
// A `color` replaces the fill; `rounding` (dp) replaces the overlay radius. The card is md::beginCard's child window
// with these colors, so its sizes (md::cardSize: 320dp natural width, the content plus the padding), its card stack
// (md::cardDepth, what the renderer restores after an error) and its surface push are Material's. The stroke is drawn
// over the child's background when it begins, like ImGui draws a child's border.
export module imlui.skin.fluent.card;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.widgets.card;
import imlui.widgets.common;
import imlui.widgets.surface;

export namespace imlui::fluent {

/// Begins a card (md::beginCard's parameters); pair with endCard(), also when it returns false.
bool beginCard(const Look& look, const char* id, md::CardVariant variant, float width, float padding, float height,
               std::optional<Color> color, float rounding);
void endCard(const Look& look);
/// The size beginCard() .. endCard() takes (md::cardSize: the variant and the tokens don't change it).
[[nodiscard]] ImVec2 cardSize(const Look& look, float width, float padding, float height, float contentHeight);

/// What a card of `variant` is painted with: its fill (opaque: a translucent fill over the current surface, which
/// it pushes as its surface) and its stroke (transparent: none).
struct CardColors {
    Color fill;
    Color stroke;
};
[[nodiscard]] CardColors cardColors(const Look& look, md::CardVariant variant);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

/// `c` painted over the current surface, opaque.
Color overSurface(Color c) { return overlay(md::surfaceColor(), c.withAlpha(1.0f), c.a); }

} // namespace

CardColors cardColors(const Look& look, md::CardVariant variant) {
    const Colors& c = look.colors;
    switch (variant) {
    case md::CardVariant::Elevated: return {overSurface(c.card), c.cardStroke};
    case md::CardVariant::Filled: return {overSurface(c.layer), Color{0, 0, 0, 0}};
    case md::CardVariant::Outlined: return {md::surfaceColor(), c.divider};
    }
    return {overSurface(c.card), c.cardStroke};
}

bool beginCard(const Look& look, const char* id, md::CardVariant variant, float width, float padding, float height,
               std::optional<Color> color, float rounding) {
    const CardColors cc = cardColors(look, variant);
    const float rnd = rounding >= 0 ? rounding : look.tokens.overlayRadius;
    // Filled: md's card without a shadow or a border of its own; the fill and the radius are passed.
    const bool visible =
        md::beginCard(id, md::CardVariant::Filled, width, padding, height, color.value_or(cc.fill), rnd);
    const float stroke = strokeWidth(look);
    if (visible && stroke > 0 && cc.stroke.a > 0) {
        const ImGuiWindow* w = ImGui::GetCurrentContext()->CurrentWindow;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // The child clips its content inside the padding; the border lies on its edge.
        dl->PushClipRect(w->OuterRectClipped.Min, w->OuterRectClipped.Max, false);
        md::strokeRoundRect(dl, w->Pos, {w->Pos.x + w->Size.x, w->Pos.y + w->Size.y}, dp(rnd), cc.stroke, stroke);
        dl->PopClipRect();
    }
    return visible;
}

void endCard(const Look&) { md::endCard(); }

ImVec2 cardSize(const Look&, float width, float padding, float height, float contentHeight) {
    return md::cardSize(width, padding, height, contentHeight);
}

} // namespace imlui::fluent
