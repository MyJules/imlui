// Shared Fluent primitives: what every Fluent control draws with. The metrics from the tokens, the item state
// (hover, pressed, disabled, keyboard focus), Fluent's three fill families (accent, standard, subtle) per state, a
// control's background (fill, stroke, darker bottom edge), an input's accent underline, the keyboard focus rectangle,
// and the icon + label content of a control.
//
// The neutral drawing helpers are md::'s (imlui.widgets.common: fillRoundRect, strokeRoundRect, hLine, drawText,
// textTop, drawIcon, measure, itemWidth, itemDisabled, visibleLabel, animate): they decide no look, and they cull to
// the clip rect. Use them; don't add Fluent copies.
export module imlui.skin.fluent.common;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.icons;
import imlui.theme.skin_tokens;
import imlui.theme.theme;
import imlui.theme.typography;
import imlui.widgets.common;

export namespace imlui::fluent {

/// What a Fluent control draws with: the skin's tokens and colors. FluentSkin's forwards pass `look()`, so a subclass
/// that overrides tokens() or colors() restyles every Fluent control.
struct Look {
    const SkinTokens& tokens;
    const Colors& colors;
};

/// fluentTokensFor(theme()), made again when the theme changed (themeGeneration()): FluentSkin::tokens()'s default.
[[nodiscard]] const SkinTokens& themeTokens();
/// colorsFor(theme().scheme, theme().dark), made again when the theme changed: FluentSkin::colors()'s default.
[[nodiscard]] const Colors& themeColors();

// Metrics, in pixels (the tokens are dp). Heights, paddings and gaps include the density.

[[nodiscard]] float controlHeight(const Look& look);
[[nodiscard]] float compactHeight(const Look& look);
[[nodiscard]] float paddingX(const Look& look);
[[nodiscard]] float gap(const Look& look);
[[nodiscard]] float controlRadius(const Look& look);
[[nodiscard]] float overlayRadius(const Look& look);
/// The border width: whole pixels, at least 1 when the token isn't 0.
[[nodiscard]] float strokeWidth(const Look& look);
/// The icon size in dp, what md::drawIcon takes.
[[nodiscard]] float iconSize(const Look& look);

/// Fluent's body text (14dp): control labels, menu items, list rows.
inline constexpr TextStyle bodyStyle = TextStyle::BodyMedium;

/// A control's visual state, in the order Fluent resolves it: disabled wins, then pressed, then hover.
enum class State { Rest, Hover, Pressed, Disabled };

/// The last submitted item's state.
struct ItemState {
    ImGuiID id = 0;
    bool hovered = false;
    bool pressed = false;  ///< held (ImGui::IsItemActive)
    bool disabled = false; ///< inside BeginDisabled (md::itemDisabled)
    bool focused = false;  ///< keyboard focus with the focus rectangle showing (focusVisible())
};

/// The last item's state (call right after its InvisibleButton).
[[nodiscard]] ItemState itemState();
/// Disabled, else pressed, else hover, else rest.
[[nodiscard]] State stateOf(const ItemState& item);
/// Whether the last item has the keyboard nav focus and ImGui shows the nav cursor (after a key press, not after a
/// click): Fluent shows the focus rectangle for keyboard focus only.
[[nodiscard]] bool focusVisible();
/// ImGui::InvisibleButton that takes part in keyboard navigation (Tab, arrows, Space/Enter activate it), without
/// ImGui's own nav rectangle: draw drawFocusRect() when itemState().focused.
bool invisibleButton(const char* id, ImVec2 size);

/// Fluent's fill families: accent (a filled button), standard (a neutral button, a combo box), subtle (transparent
/// at rest: a text button, a list row, a menu item).
enum class Fill { Accent, Standard, Subtle };

/// What a control's background and content are painted with in one state.
struct ControlColors {
    Color fill;
    Color stroke;       ///< the border's top and sides (transparent: no border)
    Color bottomStroke; ///< the border's bottom edge
    Color text;         ///< label and icon
};

/// The colors of `fill` in `state`. An accent fill follows `tokens.buttonFill`: ButtonFill::Outline makes it
/// transparent inside, with an accent stroke and accent text.
[[nodiscard]] ControlColors controlColors(const Look& look, Fill fill, State state);

/// Draws a control's background in [a, b]: `c.fill` inside the border, the border `stroke` pixels wide in `c.stroke`
/// with its bottom edge in `c.bottomStroke` (Fluent's elevation border). `radius` in pixels.
void drawControl(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, const ControlColors& c, float stroke);
/// Draws an input's underline: `width` pixels at the bottom of [a, b], inside its rounded corners.
void drawUnderline(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, Color c, float width);
/// Draws the keyboard focus rectangle around the control [a, b] whose corner radius is `radius` (pixels): an outer
/// stroke of `focusStrokeWidth` in `focusOuter` lying `focusOffset` outside the control, and a 1dp inner stroke in
/// `focusInner` inside it. Nothing when focusStrokeWidth is 0.
void drawFocusRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, const Look& look);

/// A control's content: an optional leading icon, a gap, a label.
struct Content {
    std::string_view text;
    std::string_view icon; ///< empty when the name isn't an icon
    float iconPx = 0;      ///< the icon's size in pixels (0: none)
    float iconDp = 0;      ///< the same in dp, for md::drawIcon
    float gap = 0;         ///< between icon and label (0 unless both)
    float width = 0;       ///< icon + gap + label, pixels
};

/// Measures `text` (an ImGui label's visible part) and `icon` in `style`; `iconDp` 0 takes the token's iconSize.
[[nodiscard]] Content content(const Look& look, std::string_view text, std::string_view icon,
                              TextStyle style = bodyStyle, float iconDp = 0);
/// Draws `c` from x, centered vertically on `cy`, in `color`.
void drawContent(ImDrawList* dl, const Content& c, float x, float cy, Color color, TextStyle style = bodyStyle);

// ---- (A) buttons, toggles and ranges
/// Whether `look.colors` are a dark scheme's: lighter text than background.
[[nodiscard]] bool darkColors(const Look& look);
/// Fluent's ControlSolidFillColorDefault, the opaque fill of a slider's thumb: the background 18% toward the (white)
/// text in a dark scheme (about #454545 on #202020), `controlInputActive` (white) in a light one.
[[nodiscard]] Color solidControlFill(const Look& look);
/// The fill of an accent toggle (a checked checkbox, radio or switch, a slider's value) in `state`, whatever
/// `tokens.buttonFill`: accent, accentHover, accentPressed, accentDisabled.
[[nodiscard]] Color accentToggleFill(const Look& look, State state);
/// The glyph on an accent toggle (a check, a radio's dot, a switch's knob): onAccent, onAccentDisabled when disabled.
[[nodiscard]] Color accentToggleGlyph(const Look& look, State state);
/// The fill of an unchecked checkbox, radio or switch: controlAlt, controlAltHover, controlAltPressed, transparent
/// when disabled.
[[nodiscard]] Color altFill(const Look& look, State state);
/// The outline of an unchecked checkbox, radio or switch: controlStrongStroke, controlStrongStrokeDisabled when
/// disabled.
[[nodiscard]] Color strongStroke(const Look& look, State state);
// ---- end (A)

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr std::uint64_t never = std::numeric_limits<std::uint64_t>::max();
constexpr Color clear{0, 0, 0, 0};

} // namespace

const SkinTokens& themeTokens() {
    static SkinTokens tokens;
    static std::uint64_t generation = never;
    if (generation != themeGeneration()) {
        generation = themeGeneration();
        tokens = fluentTokensFor(theme());
    }
    return tokens;
}

const Colors& themeColors() {
    static Colors colors;
    static std::uint64_t generation = never;
    if (generation != themeGeneration()) {
        generation = themeGeneration();
        colors = colorsFor(theme().scheme, theme().dark);
    }
    return colors;
}

float controlHeight(const Look& look) { return dp(look.tokens.controlHeight * look.tokens.density); }
float compactHeight(const Look& look) { return dp(look.tokens.compactHeight * look.tokens.density); }
float paddingX(const Look& look) { return dp(look.tokens.paddingX * look.tokens.density); }
float gap(const Look& look) { return dp(look.tokens.gap * look.tokens.density); }
float controlRadius(const Look& look) { return dp(look.tokens.controlRadius); }
float overlayRadius(const Look& look) { return dp(look.tokens.overlayRadius); }
float strokeWidth(const Look& look) {
    return look.tokens.strokeWidth > 0 ? std::max(1.0f, std::round(dp(look.tokens.strokeWidth))) : 0.0f;
}
float iconSize(const Look& look) { return look.tokens.iconSize; }

ItemState itemState() {
    return {.id = ImGui::GetItemID(),
            .hovered = ImGui::IsItemHovered(),
            .pressed = ImGui::IsItemActive(),
            .disabled = md::itemDisabled(),
            .focused = focusVisible()};
}

State stateOf(const ItemState& item) {
    if (item.disabled) return State::Disabled;
    if (item.pressed) return State::Pressed;
    if (item.hovered) return State::Hover;
    return State::Rest;
}

bool focusVisible() {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    return g.NavCursorVisible && g.NavId != 0 && g.NavId == ImGui::GetItemID() && !md::itemDisabled();
}

bool invisibleButton(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4{0, 0, 0, 0});
    const bool clicked = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    return clicked;
}

ControlColors controlColors(const Look& look, Fill fill, State state) {
    const Colors& c = look.colors;
    switch (fill) {
    case Fill::Accent:
        if (look.tokens.buttonFill == ButtonFill::Outline) {
            switch (state) {
            case State::Rest: return {clear, c.accent, c.accent, c.accentText};
            case State::Hover: return {c.accent.withAlpha(0.08f), c.accent, c.accent, c.accentText};
            case State::Pressed: return {c.accent.withAlpha(0.14f), c.accentPressed, c.accentPressed, c.accentText};
            case State::Disabled:
                return {clear, c.controlStrongStrokeDisabled, c.controlStrongStrokeDisabled, c.textDisabled};
            }
        }
        switch (state) {
        case State::Rest: return {c.accent, c.accentStroke, c.accentBottomStroke, c.onAccent};
        case State::Hover: return {c.accentHover, c.accentStroke, c.accentBottomStroke, c.onAccent};
        case State::Pressed: return {c.accentPressed, clear, clear, c.onAccent.withAlpha(c.onAccent.a * 0.7f)};
        case State::Disabled: return {c.accentDisabled, clear, clear, c.onAccentDisabled};
        }
        break;
    case Fill::Standard:
        switch (state) {
        case State::Rest: return {c.control, c.controlStroke, c.controlBottomStroke, c.text};
        case State::Hover: return {c.controlHover, c.controlStroke, c.controlBottomStroke, c.text};
        case State::Pressed: return {c.controlPressed, c.controlStroke, c.controlStroke, c.textSecondary};
        case State::Disabled: return {c.controlDisabled, c.controlStroke, c.controlStroke, c.textDisabled};
        }
        break;
    case Fill::Subtle:
        switch (state) {
        case State::Rest: return {c.subtle, clear, clear, c.text};
        case State::Hover: return {c.subtleHover, clear, clear, c.text};
        case State::Pressed: return {c.subtlePressed, clear, clear, c.textSecondary};
        case State::Disabled: return {clear, clear, clear, c.textDisabled};
        }
        break;
    }
    return {clear, clear, clear, c.text};
}

void drawControl(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, const ControlColors& c, float stroke) {
    const bool bordered = stroke > 0 && (c.stroke.a > 0 || c.bottomStroke.a > 0);
    // The fill lies inside the border (XAML's InnerBorderEdge), so translucent fills and strokes don't add up.
    const float inset = bordered ? stroke : 0.0f;
    if (c.fill.a > 0)
        md::fillRoundRect(dl, {a.x + inset, a.y + inset}, {b.x - inset, b.y - inset}, std::max(0.0f, radius - inset),
                          c.fill);
    if (!bordered) return;
    md::strokeRoundRect(dl, a, b, radius, c.stroke, stroke);
    if (c.bottomStroke != c.stroke) {
        // The bottom edge between the corners, over the stroke's: the corners keep the border's color.
        const float r = std::min(radius, (b.x - a.x) * 0.5f);
        md::hLine(dl, std::round(a.x + r), std::round(b.x - r), std::round(b.y) - stroke, c.bottomStroke, stroke);
    }
}

void drawUnderline(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, Color c, float width) {
    if (width <= 0 || c.a <= 0) return;
    // Clipped to the control's bottom band, then a rounded rect of the control's shape: the line follows the
    // corners' curve like Fluent's (a bottom border inside a rounded border).
    const float w = std::max(1.0f, std::round(width));
    const ImVec2 clipA{a.x, std::round(b.y) - w};
    dl->PushClipRect(clipA, b, true);
    md::fillRoundRect(dl, a, b, radius, c, ImDrawFlags_RoundCornersBottom);
    dl->PopClipRect();
}

void drawFocusRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, const Look& look) {
    if (look.tokens.focusStrokeWidth <= 0) return;
    const float outer = std::max(1.0f, std::round(dp(look.tokens.focusStrokeWidth)));
    const float off = std::round(dp(look.tokens.focusOffset));
    const float inner = std::max(1.0f, std::round(dp(1)));
    md::strokeRoundRect(dl, {a.x - off, a.y - off}, {b.x + off, b.y + off}, radius + off, look.colors.focusOuter,
                        outer);
    const float in = off - outer; // the inner stroke's outer edge
    if (in >= inner)
        md::strokeRoundRect(dl, {a.x - in, a.y - in}, {b.x + in, b.y + in}, std::max(0.0f, radius + in),
                            look.colors.focusInner, inner);
}

Content content(const Look& look, std::string_view text, std::string_view icon, TextStyle style, float iconDp) {
    Content c;
    c.text = text;
    if (hasIcon(icon)) {
        c.icon = icon;
        c.iconDp = iconDp > 0 ? iconDp : iconSize(look);
        c.iconPx = dp(c.iconDp);
    }
    const float textW = text.empty() ? 0.0f : md::measure(style, text).x;
    c.gap = c.iconPx > 0 && !text.empty() ? gap(look) : 0.0f;
    c.width = c.iconPx + c.gap + textW;
    return c;
}

void drawContent(ImDrawList* dl, const Content& c, float x, float cy, Color color, TextStyle style) {
    if (c.iconPx > 0) {
        md::drawIcon(dl, c.icon, {x + c.iconPx * 0.5f, cy}, c.iconDp, color);
        x += c.iconPx + c.gap;
    }
    if (!c.text.empty()) md::drawText(dl, style, {x, md::textTop(style, cy)}, color, c.text);
}

// ---- (A) buttons, toggles and ranges
bool darkColors(const Look& look) {
    const auto luma = [](Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
    return luma(look.colors.text) > luma(look.colors.background);
}

Color solidControlFill(const Look& look) {
    const Colors& c = look.colors;
    return darkColors(look) ? mix(c.background, c.text.withAlpha(1.0f), 0.18f) : c.controlInputActive;
}

Color accentToggleFill(const Look& look, State state) {
    const Colors& c = look.colors;
    switch (state) {
    case State::Rest: return c.accent;
    case State::Hover: return c.accentHover;
    case State::Pressed: return c.accentPressed;
    case State::Disabled: return c.accentDisabled;
    }
    return c.accent;
}

Color accentToggleGlyph(const Look& look, State state) {
    return state == State::Disabled ? look.colors.onAccentDisabled : look.colors.onAccent;
}

Color altFill(const Look& look, State state) {
    const Colors& c = look.colors;
    switch (state) {
    case State::Rest: return c.controlAlt;
    case State::Hover: return c.controlAltHover;
    case State::Pressed: return c.controlAltPressed;
    case State::Disabled: return clear;
    }
    return c.controlAlt;
}

Color strongStroke(const Look& look, State state) {
    return state == State::Disabled ? look.colors.controlStrongStrokeDisabled : look.colors.controlStrongStroke;
}
// ---- end (A)

} // namespace imlui::fluent
