// Fluent colors: the Fluent 2 (Windows 11) drawing colors, derived from a theme's scheme roles, so a script's
// `colors = { ... }` overrides restyle the Fluent skin like the Material one (`primary` is the accent).
//
// Fluent's neutral brushes are white or black at an opacity, per mode (dark: white fills and strokes on a dark
// background, light: white fills with black strokes). Here "white" and "black" are scheme roles, so they carry the
// scheme's tint and follow overrides: in a dark scheme white is onSurface and black is surfaceContainerLowest, in a
// light one the other way round. The opacities are Fluent's (the WinUI 3 theme resources).
//
// In the theme layer, not with the Fluent skin: the theme maps these onto ImGuiStyle under design "fluent", and
// imlui.theme can't import imlui.skin.
export module imlui.theme.fluent_colors;

import std;

import imlui.color;
import imlui.theme.scheme;

export namespace imlui::fluent {

/// The design name the Fluent skin is registered under (`theme { design = "fluent" }`).
inline constexpr std::string_view designName = "fluent";

/// Fluent's drawing colors for one scheme. Translucent colors are meant to be painted over the surface below
/// (Fluent's brushes are); the backgrounds (`background`, `layer`, `flyout`) are opaque.
struct Colors {
    // Accent (the scheme's primary): accent buttons, checked toggles, sliders, the focused input underline.
    Color accent, accentHover, accentPressed, accentDisabled;
    Color onAccent;           ///< text and icons on accent
    Color onAccentDisabled;   ///< text on a disabled accent fill
    Color accentStroke;       ///< an accent control's border (top and sides)
    Color accentBottomStroke; ///< an accent control's bottom edge, darker (Fluent's elevation border)
    Color accentText;         ///< accent-colored text: hyperlinks, an outline accent button's label

    // Standard controls: subtle neutral fills over the surface.
    Color control, controlHover, controlPressed, controlDisabled;
    Color controlInputActive;  ///< a focused text input's fill
    Color controlStroke;       ///< a control's border (top and sides)
    Color controlBottomStroke; ///< a control's bottom edge, darker (Fluent's elevation border)
    Color controlStrongStroke; ///< strong outlines: an unchecked checkbox or radio, a slider's track
    Color controlStrongStrokeDisabled;
    // Alternate fills: what an unchecked checkbox or radio is filled with.
    Color controlAlt, controlAltHover, controlAltPressed;

    // Subtle controls: transparent at rest (text buttons, list rows, menu items).
    Color subtle, subtleHover, subtlePressed;

    // Text.
    Color text, textSecondary, textTertiary, textDisabled;

    // Surfaces.
    Color background; ///< the window's background (Mica's solid fallback)
    Color layer;      ///< a content layer over the background
    Color card, cardStroke;
    Color flyout, flyoutStroke; ///< menus, popups, tooltips, dialogs
    Color smoke;                ///< the dim behind a modal
    Color divider;

    // Keyboard focus: an outer stroke (high contrast) and an inner one (against the control).
    Color focusOuter, focusInner;

    friend bool operator==(const Colors&, const Colors&) = default;
};

/// The Fluent colors of `scheme`, a dark or a light one.
[[nodiscard]] Colors colorsFor(const Scheme& scheme, bool dark);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

Colors colorsFor(const Scheme& s, bool dark) {
    const Color white = dark ? s.onSurface : s.surfaceContainerLowest;
    const Color black = dark ? s.surfaceContainerLowest : s.onSurface;
    // `d` in a dark scheme, `l` in a light one.
    const auto pick = [dark](Color d, Color l) { return dark ? d : l; };

    Colors c;
    c.accent = s.primary;
    c.accentHover = s.primary.withAlpha(s.primary.a * 0.9f);
    c.accentPressed = s.primary.withAlpha(s.primary.a * 0.8f);
    c.accentDisabled = pick(white.withAlpha(0.1581f), black.withAlpha(0.2169f));
    c.onAccent = s.onPrimary;
    c.onAccentDisabled = pick(white.withAlpha(0.5302f), white);
    c.accentStroke = white.withAlpha(0.08f);
    c.accentBottomStroke = pick(black.withAlpha(0.14f), black.withAlpha(0.40f));
    c.accentText = s.primary;

    c.control = pick(white.withAlpha(0.0605f), white.withAlpha(0.70f));
    c.controlHover = pick(white.withAlpha(0.0837f), white.withAlpha(0.50f));
    c.controlPressed = pick(white.withAlpha(0.0326f), white.withAlpha(0.30f));
    c.controlDisabled = pick(white.withAlpha(0.0419f), white.withAlpha(0.30f));
    c.controlInputActive = pick(black.withAlpha(0.70f), white);
    c.controlStroke = pick(white.withAlpha(0.0698f), black.withAlpha(0.0578f));
    c.controlBottomStroke = pick(white.withAlpha(0.0930f), black.withAlpha(0.1622f));
    c.controlStrongStroke = pick(white.withAlpha(0.5442f), black.withAlpha(0.4458f));
    c.controlStrongStrokeDisabled = pick(white.withAlpha(0.1581f), black.withAlpha(0.2169f));
    c.controlAlt = pick(black.withAlpha(0.10f), black.withAlpha(0.0241f));
    c.controlAltHover = pick(white.withAlpha(0.0419f), black.withAlpha(0.0578f));
    c.controlAltPressed = pick(white.withAlpha(0.0698f), black.withAlpha(0.0924f));

    c.subtle = white.withAlpha(0.0f);
    c.subtleHover = pick(white.withAlpha(0.0605f), black.withAlpha(0.0373f));
    c.subtlePressed = pick(white.withAlpha(0.0419f), black.withAlpha(0.0241f));

    c.text = pick(white, black.withAlpha(0.8956f));
    c.textSecondary = pick(white.withAlpha(0.786f), black.withAlpha(0.6063f));
    c.textTertiary = pick(white.withAlpha(0.5442f), black.withAlpha(0.4458f));
    c.textDisabled = pick(white.withAlpha(0.3628f), black.withAlpha(0.3614f));

    // Fluent's solid backgrounds: #202020 dark, #F3F3F3 light; the scheme's containers of about those tones.
    c.background = pick(s.surfaceContainer, s.surfaceContainerLow);
    c.layer = pick(mix(c.background, s.surfaceContainerHigh, 0.6f), mix(c.background, white, 0.5f));
    c.card = pick(white.withAlpha(0.0512f), white.withAlpha(0.70f));
    c.cardStroke = pick(black.withAlpha(0.10f), black.withAlpha(0.0578f));
    c.flyout = pick(s.surfaceContainerHigh, mix(c.background, white, 0.6f));
    c.flyoutStroke = pick(black.withAlpha(0.20f), black.withAlpha(0.0578f));
    c.smoke = black.withAlpha(0.30f);
    c.divider = pick(white.withAlpha(0.0837f), black.withAlpha(0.0803f));

    c.focusOuter = c.text;
    c.focusInner = pick(black.withAlpha(0.70f), white.withAlpha(0.70f));
    return c;
}

} // namespace imlui::fluent
