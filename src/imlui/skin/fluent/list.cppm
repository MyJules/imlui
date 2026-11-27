// Fluent list rows and expanders: WinUI's ListViewItem and Expander header, for md::listItem and md::expander.
//
// A list row is a subtle item: transparent at rest, a subtle fill on hover and press, inside the row
// (plateInsetX/plateInsetY from its edges, the control radius). Selected, the fill is one step on and an accent pill
// (the selection indicator) sits at the plate's left edge, 10dp long while pressed. Height: controlHeight + 8dp (40dp),
// with a supporting line controlHeight + 24dp (56dp), the extras scaled by the density. Natural width 320dp, md's
// (the ListItem element measures a row by its supporting text and width alone).
//
// An expander header is a card (the card fill and stroke, the control radius), controlHeight + 16dp high (48dp; with a
// supporting line + 32dp, 64dp), with a chevron in a subtle square on the right that takes the hover and press fills.
// Natural width 320dp, md's (the header's size doesn't depend on its title).
export module imlui.skin.fluent.list;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.typography;
import imlui.widgets.common;
import imlui.widgets.list;

export namespace imlui::fluent {

/// Draws a list row (md::listItem's parameters); returns whether it was clicked.
bool listItem(const Look& look, const char* id, const md::ListItemOptions& o);
/// The size listItem() takes with the same options.
[[nodiscard]] ImVec2 listItemSize(const Look& look, const md::ListItemOptions& o);

/// Draws an expander header (md::expander's parameters); toggles `*open` and returns true when clicked.
bool expander(const Look& look, const char* id, std::string_view title, std::string_view supporting, bool* open,
              float width);
/// The size expander() takes with the same `supporting` and `width`.
[[nodiscard]] ImVec2 expanderSize(const Look& look, std::string_view supporting, float width);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float rowExtra = 8.0f;                         // dp over controlHeight: a one-line row (40dp)
constexpr float twoLineExtra = 24.0f;                    // dp over controlHeight: a row with a supporting line (56dp)
constexpr float expanderExtra = 16.0f;                   // dp over controlHeight: an expander header (48dp)
constexpr float expanderTwoExtra = 32.0f;                // with a supporting line (64dp)
constexpr float naturalWidth = 320.0f;                   // dp: a row's and a header's natural width, md's
constexpr TextStyle captionStyle = TextStyle::BodySmall; // Fluent's caption (12dp): supporting and trailing text

float density(const Look& look) { return look.tokens.density; }

struct RowLayout {
    Content lead;  ///< the leading icon
    Content trail; ///< the trailing icon
    float trailingTextW = 0;
    float w = 0, h = 0;
};

RowLayout rowLayout(const Look& look, const md::ListItemOptions& o) {
    RowLayout l;
    l.lead = content(look, {}, o.leadingIcon);
    l.trail = content(look, {}, o.trailingIcon);
    l.trailingTextW = o.trailingText.empty() ? 0.0f : md::measure(captionStyle, o.trailingText).x;
    // A fixed natural width, md's: ListItem measures itself with its supporting text and width alone.
    l.w = md::itemWidth(o.width, dp(naturalWidth));
    l.h = controlHeight(look) + dp((o.supporting.empty() ? rowExtra : twoLineExtra) * density(look));
    return l;
}

} // namespace

ImVec2 listItemSize(const Look& look, const md::ListItemOptions& o) {
    const RowLayout l = rowLayout(look, o);
    return {l.w, l.h};
}

bool listItem(const Look& look, const char* id, const md::ListItemOptions& o) {
    const RowLayout l = rowLayout(look, o);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(id, {l.w, l.h});
    const ItemState item = itemState();
    const State state = stateOf(item);
    const float sel = md::animate(item.id + 3, o.selected ? 1.0f : 0.0f, 18.0f);
    const ControlColors c = rowColors(look, state, sel);
    const Colors& k = look.colors;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a{p.x + dp(plateInsetX), p.y + dp(plateInsetY)};
    const ImVec2 b{p.x + l.w - dp(plateInsetX), p.y + l.h - dp(plateInsetY)};
    const float radius = controlRadius(look);
    md::fillRoundRect(dl, a, b, radius, c.fill);
    const float cy = p.y + l.h * 0.5f;
    if (sel > 0.01f) {
        const float len = dp(item.pressed ? indicatorPressedLength : indicatorLength) * sel;
        drawIndicator(dl, {a.x + dp(indicatorThickness) * 0.5f, cy}, len, true, item.disabled, look);
    }

    const Color secondary = item.disabled ? k.textDisabled : k.textSecondary;
    float x = a.x + paddingX(look);
    if (l.lead.iconPx > 0) {
        drawContent(dl, l.lead, x, cy, c.text);
        x += l.lead.iconPx + gap(look);
    }
    float right = b.x - paddingX(look);
    if (l.trail.iconPx > 0) {
        drawContent(dl, l.trail, right - l.trail.iconPx, cy, secondary);
        right -= l.trail.iconPx + gap(look);
    }
    if (l.trailingTextW > 0) {
        md::drawText(dl, captionStyle, {right - l.trailingTextW, md::textTop(captionStyle, cy)}, secondary,
                     o.trailingText);
        right -= l.trailingTextW + gap(look);
    }
    dl->PushClipRect({x, p.y}, {std::max(x, right), p.y + l.h}, true);
    if (o.supporting.empty()) {
        md::drawText(dl, bodyStyle, {x, md::textTop(bodyStyle, cy)}, c.text, o.headline);
    } else {
        const float hh = md::measure(bodyStyle, "Ag").y, sh = md::measure(captionStyle, "Ag").y;
        const float y = std::round(cy - (hh + sh) * 0.5f);
        md::drawText(dl, bodyStyle, {x, y}, c.text, o.headline);
        md::drawText(dl, captionStyle, {x, y + hh}, secondary, o.supporting);
    }
    dl->PopClipRect();
    if (item.focused) drawFocusRect(dl, a, b, radius, look);
    return clicked;
}

ImVec2 expanderSize(const Look& look, std::string_view supporting, float width) {
    const float extra = supporting.empty() ? expanderExtra : expanderTwoExtra;
    return {md::itemWidth(width, dp(naturalWidth)), controlHeight(look) + dp(extra * density(look))};
}

bool expander(const Look& look, const char* id, std::string_view title, std::string_view supporting, bool* open,
              float width) {
    const ImVec2 size = expanderSize(look, supporting, width);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(id, size);
    if (clicked) *open = !*open;
    const ItemState item = itemState();
    const Colors& k = look.colors;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{p.x + size.x, p.y + size.y};
    const float radius = controlRadius(look);
    const ControlColors card{k.card, k.cardStroke, k.cardStroke, k.text};
    drawControl(dl, p, q, radius, card, strokeWidth(look));

    const float cy = p.y + size.y * 0.5f;
    const Color fg = item.disabled ? k.textDisabled : k.text;
    const Color secondary = item.disabled ? k.textDisabled : k.textSecondary;
    // The chevron's square, controlHeight wide, paddingX - plateInsetX from the right edge (WinUI's 32dp button).
    const float side = controlHeight(look);
    const float right = q.x - std::max(0.0f, paddingX(look) - dp(plateInsetX));
    const ImVec2 ca{right - side, cy - side * 0.5f}, cb{right, cy + side * 0.5f};
    const ControlColors chevron = controlColors(look, Fill::Subtle, stateOf(item));
    md::fillRoundRect(dl, ca, cb, radius, chevron.fill);
    md::drawIcon(dl, *open ? "expand_less" : "expand_more", {(ca.x + cb.x) * 0.5f, cy}, iconSize(look), secondary);

    const float x = p.x + paddingX(look) + dp(plateInsetX * density(look));
    dl->PushClipRect({x, p.y}, {std::max(x, ca.x - gap(look)), q.y}, true);
    if (supporting.empty()) {
        md::drawText(dl, bodyStyle, {x, md::textTop(bodyStyle, cy)}, fg, title);
    } else {
        const float th = md::measure(bodyStyle, "Ag").y, sh = md::measure(captionStyle, "Ag").y;
        const float y = std::round(cy - (th + sh) * 0.5f);
        md::drawText(dl, bodyStyle, {x, y}, fg, title);
        md::drawText(dl, captionStyle, {x, y + th}, secondary, supporting);
    }
    dl->PopClipRect();
    if (item.focused) drawFocusRect(dl, p, q, radius, look);
    return clicked;
}

} // namespace imlui::fluent
