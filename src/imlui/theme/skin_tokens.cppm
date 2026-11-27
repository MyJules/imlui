// Skin tokens: the metrics of a flat design (control height, corner radii, stroke widths, focus and underline styles,
// paddings, density), so a token-driven skin (Fluent) and the ImGui style under it read one set of numbers, and a
// script restyles them with `theme { tokens = { ... } }` (Theme::tokens).
//
// Each design has its defaults (materialTokens(), fluentTokens()); a theme's overrides are applied over them by name
// (applyTokenOverrides). The token table below (tokenInfos()) is the one list of names, kinds and ranges: the Lua
// theme table checks against it, setTheme() drops what it rejects, the docs and the Lua definitions list it.
//
// It lives in the theme layer, not with the skins: the theme maps the tokens onto ImGuiStyle, and imlui.theme can't
// import imlui.skin (the widgets import the theme, the skins import the widgets).
export module imlui.theme.skin_tokens;

import std;

export namespace imlui {

/// How a skin draws the button variants that map to its accent (filled, tonal).
enum class ButtonFill {
    Filled,  ///< an accent background, text on accent
    Outline, ///< transparent inside, an accent stroke and accent text
};

/// A flat design's metrics. Lengths are dp (scaled by the DPI factor and zoom when drawn). `density` multiplies the
/// heights, paddings and gaps, not the radii and strokes.
struct SkinTokens {
    float controlHeight = 32.0f;      ///< a standard control's height: buttons, text fields, combo boxes
    float compactHeight = 24.0f;      ///< a compact control's height: list rows in menus, small buttons
    float controlRadius = 4.0f;       ///< the corner radius of controls
    float overlayRadius = 8.0f;       ///< the corner radius of overlays and containers: cards, flyouts, dialogs
    float strokeWidth = 1.0f;         ///< a control's border width (0: none)
    float focusStrokeWidth = 2.0f;    ///< the keyboard focus rectangle's outer stroke width (0: none)
    float focusOffset = 3.0f;         ///< how far the focus rectangle lies outside the control
    bool inputUnderline = true;       ///< inputs carry an accent underline at their bottom edge when focused
    float inputUnderlineWidth = 2.0f; ///< the focused underline's width (1dp at rest)
    ButtonFill buttonFill = ButtonFill::Filled; ///< how accent buttons are drawn
    float paddingX = 12.0f;                     ///< the horizontal padding inside controls (before the label)
    float gap = 8.0f;                           ///< the gap between a control's parts (icon and label, label and glyph)
    float iconSize = 16.0f;                     ///< the size of icons inside controls
    float density = 1.0f;                       ///< multiplies heights, paddings and gaps: < 1 compact, > 1 roomy

    friend bool operator==(const SkinTokens&, const SkinTokens&) = default;
};

/// Fluent 2 (Windows 11) metrics: 32dp controls, 4dp corners, 8dp overlays, 1dp strokes, a 2dp focus stroke 3dp out.
[[nodiscard]] SkinTokens fluentTokens();
/// The Material skin's metrics, informational (the Material widgets don't read tokens): 40dp buttons with full pill
/// corners (radius 20), 28dp dialog corners, 24dp button padding, 18dp icons.
[[nodiscard]] SkinTokens materialTokens();

/// The kind of a token's value.
enum class TokenKind { Number, Bool, ButtonFill };

/// A token by name: its kind, its range (numbers) and its member.
struct TokenInfo {
    std::string_view name;
    TokenKind kind;
    float min = 0.0f, max = 0.0f; ///< the accepted range of a Number
    std::variant<float SkinTokens::*, bool SkinTokens::*, ButtonFill SkinTokens::*> member;
    std::string_view doc;
};

/// Every token, in declaration order.
[[nodiscard]] std::span<const TokenInfo> tokenInfos();
/// The token named `name`, or nullptr.
[[nodiscard]] const TokenInfo* findToken(std::string_view name);

/// A token's value: a number, a bool, or a ButtonFill's name ("filled", "outline").
using TokenValue = std::variant<float, bool, std::string>;

/// One token override, by name (`theme { tokens = { controlHeight = 40 } }`).
struct TokenOverride {
    std::string name;
    TokenValue value;
};

/// Why `value` can't set token `name` ("" when it can): an unknown name, a value of the wrong kind, a number that
/// isn't finite or is out of the token's range, an unknown ButtonFill name.
[[nodiscard]] std::string tokenProblem(std::string_view name, const TokenValue& value);

/// `base` with `overrides` applied in order; an override with a tokenProblem() is skipped.
[[nodiscard]] SkinTokens applyTokenOverrides(SkinTokens base, std::span<const TokenOverride> overrides);

/// "filled" / "outline".
[[nodiscard]] std::string_view buttonFillName(ButtonFill fill);
/// The ButtonFill named `name`, or nullopt.
[[nodiscard]] std::optional<ButtonFill> buttonFillFromName(std::string_view name);

} // namespace imlui

module :private;

namespace imlui {

namespace {

const TokenInfo infos[] = {
    {"controlHeight", TokenKind::Number, 8.0f, 200.0f, &SkinTokens::controlHeight,
     "A standard control's height (dp): buttons, text fields, combo boxes."},
    {"compactHeight", TokenKind::Number, 8.0f, 200.0f, &SkinTokens::compactHeight,
     "A compact control's height (dp): menu rows, small buttons."},
    {"controlRadius", TokenKind::Number, 0.0f, 100.0f, &SkinTokens::controlRadius,
     "The corner radius of controls (dp)."},
    {"overlayRadius", TokenKind::Number, 0.0f, 100.0f, &SkinTokens::overlayRadius,
     "The corner radius of overlays and containers (dp): cards, flyouts, dialogs."},
    {"strokeWidth", TokenKind::Number, 0.0f, 8.0f, &SkinTokens::strokeWidth, "A control's border width (dp); 0: none."},
    {"focusStrokeWidth", TokenKind::Number, 0.0f, 8.0f, &SkinTokens::focusStrokeWidth,
     "The keyboard focus rectangle's outer stroke width (dp); 0: none."},
    {"focusOffset", TokenKind::Number, 0.0f, 16.0f, &SkinTokens::focusOffset,
     "How far the focus rectangle lies outside the control (dp)."},
    {"inputUnderline", TokenKind::Bool, 0.0f, 0.0f, &SkinTokens::inputUnderline,
     "Whether inputs carry an accent underline at their bottom edge when focused."},
    {"inputUnderlineWidth", TokenKind::Number, 0.0f, 8.0f, &SkinTokens::inputUnderlineWidth,
     "The focused input underline's width (dp)."},
    {"buttonFill", TokenKind::ButtonFill, 0.0f, 0.0f, &SkinTokens::buttonFill,
     "How accent buttons (`filled`, `tonal`) are drawn: `\"filled\"` or `\"outline\"`."},
    {"paddingX", TokenKind::Number, 0.0f, 100.0f, &SkinTokens::paddingX,
     "The horizontal padding inside controls (dp)."},
    {"gap", TokenKind::Number, 0.0f, 100.0f, &SkinTokens::gap,
     "The gap between a control's parts (dp): icon and label, label and glyph."},
    {"iconSize", TokenKind::Number, 8.0f, 64.0f, &SkinTokens::iconSize, "The size of icons inside controls (dp)."},
    {"density", TokenKind::Number, 0.5f, 2.0f, &SkinTokens::density,
     "Multiplies heights, paddings and gaps (not radii or strokes): below 1 compact, above 1 roomy."},
};

} // namespace

SkinTokens fluentTokens() { return SkinTokens{}; }

SkinTokens materialTokens() {
    SkinTokens t;
    t.controlHeight = 40.0f;
    t.compactHeight = 32.0f;
    t.controlRadius = 20.0f;
    t.overlayRadius = 28.0f;
    t.strokeWidth = 1.0f;
    t.focusStrokeWidth = 2.0f;
    t.focusOffset = 2.0f;
    t.inputUnderline = true;
    t.inputUnderlineWidth = 2.0f;
    t.buttonFill = ButtonFill::Filled;
    t.paddingX = 24.0f;
    t.gap = 8.0f;
    t.iconSize = 18.0f;
    t.density = 1.0f;
    return t;
}

std::span<const TokenInfo> tokenInfos() { return infos; }

const TokenInfo* findToken(std::string_view name) {
    const auto it = std::ranges::find(infos, name, &TokenInfo::name);
    return it == std::ranges::end(infos) ? nullptr : &*it;
}

std::string tokenProblem(std::string_view name, const TokenValue& value) {
    const TokenInfo* info = findToken(name);
    if (!info) return "unknown token";
    switch (info->kind) {
    case TokenKind::Number: {
        const float* v = std::get_if<float>(&value);
        const std::string range = std::format("expected a number in {}..{}", info->min, info->max);
        if (!v || !std::isfinite(*v) || *v < info->min || *v > info->max) return range;
        return {};
    }
    case TokenKind::Bool:
        return std::holds_alternative<bool>(value) ? std::string{} : std::string{"expected a boolean"};
    case TokenKind::ButtonFill: {
        const std::string* s = std::get_if<std::string>(&value);
        return s && buttonFillFromName(*s) ? std::string{} : std::string{R"(expected "filled" or "outline")"};
    }
    }
    return {};
}

SkinTokens applyTokenOverrides(SkinTokens base, std::span<const TokenOverride> overrides) {
    for (const TokenOverride& o : overrides) {
        if (!tokenProblem(o.name, o.value).empty()) continue;
        const TokenInfo& info = *findToken(o.name);
        if (const auto* f = std::get_if<float SkinTokens::*>(&info.member))
            base.*(*f) = std::get<float>(o.value);
        else if (const auto* b = std::get_if<bool SkinTokens::*>(&info.member))
            base.*(*b) = std::get<bool>(o.value);
        else
            base.*std::get<ButtonFill SkinTokens::*>(info.member) = *buttonFillFromName(std::get<std::string>(o.value));
    }
    return base;
}

std::string_view buttonFillName(ButtonFill fill) { return fill == ButtonFill::Outline ? "outline" : "filled"; }

std::optional<ButtonFill> buttonFillFromName(std::string_view name) {
    if (name == "filled") return ButtonFill::Filled;
    if (name == "outline") return ButtonFill::Outline;
    return std::nullopt;
}

} // namespace imlui
