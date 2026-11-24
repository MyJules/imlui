// Common buttons (filled, tonal, outlined, text, elevated, FAB) and icon buttons.
export module imlui.widgets.button;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;

export namespace imlui::md {

enum class ButtonVariant { Filled, Tonal, Outlined, Text, Elevated, Fab };

/// "filled" (default), "tonal", "outlined", "text", "elevated", "fab".
[[nodiscard]] ButtonVariant buttonVariant(std::string_view name);

/// Common button. `iconName` is an optional leading Material icon.
/// A FAB with an empty label is a regular 56dp FAB, otherwise extended.
bool button(const char* label, ButtonVariant variant = ButtonVariant::Filled, ImVec2 size = {0, 0},
            std::string_view iconName = {});
/// The size button() takes with the same arguments (`size.x` resolved like itemWidth(): fixed, natural, or a fill of
/// the layout box), without drawing it.
[[nodiscard]] ImVec2 buttonSize(const char* label, ButtonVariant variant = ButtonVariant::Filled, ImVec2 size = {0, 0},
                                std::string_view iconName = {});

/// The size of iconButton(): the 48dp touch target.
[[nodiscard]] ImVec2 iconButtonSize();

enum class IconButtonVariant { Standard, Filled, Tonal, Outlined };

/// "standard" (default), "filled", "tonal", "outlined".
[[nodiscard]] IconButtonVariant iconButtonVariant(std::string_view n);

/// Icon button (40dp container, 48dp touch target). `toggle` enables the
/// selected/unselected look.
bool iconButton(const char* id, std::string_view iconName, IconButtonVariant v = IconButtonVariant::Standard,
                bool toggle = false, bool selected = false, std::optional<Color> tint = std::nullopt);

} // namespace imlui::md

module :private;

namespace imlui::md {

ButtonVariant buttonVariant(std::string_view name) {
    if (name == "tonal") return ButtonVariant::Tonal;
    if (name == "outlined") return ButtonVariant::Outlined;
    if (name == "text") return ButtonVariant::Text;
    if (name == "elevated") return ButtonVariant::Elevated;
    if (name == "fab") return ButtonVariant::Fab;
    return ButtonVariant::Filled;
}

namespace {

/// A button's measurements: its size, and the width of its content (icon, gap, label), centered in it.
struct ButtonLayout {
    std::string_view text;
    bool withIcon = false;
    float iconSz = 0.0f;
    float gap = 0.0f;
    float content = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

ButtonLayout buttonLayout(const char* label, ButtonVariant variant, ImVec2 size, std::string_view iconName) {
    ButtonLayout l;
    l.text = visibleLabel(label);
    const bool fab = variant == ButtonVariant::Fab;
    l.withIcon = hasIcon(iconName);
    l.iconSz = fab ? 24.0f : 18.0f;
    const ImVec2 ts = l.text.empty() ? ImVec2{0, 0} : measure(TextStyle::LabelLarge, l.text);
    l.gap = l.text.empty() || !l.withIcon ? 0.0f : dp(fab ? 12 : 8);
    float padL = dp(variant == ButtonVariant::Text ? 12 : (fab ? 16 : 24)), padR = padL;
    if (l.withIcon && !fab && variant != ButtonVariant::Text) padL = dp(16);
    l.content = (l.withIcon ? dp(l.iconSz) : 0.0f) + l.gap + ts.x;
    l.h = size.y > 0 ? size.y : dp(fab ? 56 : 40);
    float natural = l.content + padL + padR;
    if (fab && l.text.empty()) natural = l.h;
    l.w = itemWidth(size.x, std::max(natural, fab ? dp(56) : dp(48)));
    return l;
}

} // namespace

ImVec2 buttonSize(const char* label, ButtonVariant variant, ImVec2 size, std::string_view iconName) {
    const ButtonLayout l = buttonLayout(label, variant, size, iconName);
    return {l.w, l.h};
}

ImVec2 iconButtonSize() { return {dp(48), dp(48)}; }

bool button(const char* label, ButtonVariant variant, ImVec2 size, std::string_view iconName) {
    const Scheme& s = scheme();
    const bool fab = variant == ButtonVariant::Fab;
    const ButtonLayout l = buttonLayout(label, variant, size, iconName);
    const std::string_view text = l.text;
    const bool withIcon = l.withIcon;
    const float iconSz = l.iconSz, gap = l.gap, content = l.content, w = l.w, h = l.h;
    const float rounding = fab ? dp(shape().large) : h * 0.5f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    Color bg{0, 0, 0, 0}, fg = s.primary, border{0, 0, 0, 0};
    float elevation = 0.0f;
    switch (variant) {
    case ButtonVariant::Filled:   bg = s.primary; fg = s.onPrimary; break;
    case ButtonVariant::Tonal:    bg = s.secondaryContainer; fg = s.onSecondaryContainer; break;
    case ButtonVariant::Outlined: border = s.outline; fg = s.primary; break;
    case ButtonVariant::Text:     break;
    case ButtonVariant::Elevated: bg = s.surfaceContainerLow; elevation = 1.0f; break;
    case ButtonVariant::Fab:      bg = s.primaryContainer; fg = s.onPrimaryContainer; elevation = 2.0f; break;
    }
    if (dis) {
        fg = disabledContent();
        if (bg.a > 0) bg = disabledContainer();
        if (border.a > 0) border = disabledContainer();
        elevation = 0.0f;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{p.x + w, p.y + h};
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    // Material 3: hovering raises a button with a container by one level; Text and Outlined have no surface to lift.
    const bool lifts = !dis && variant != ButtonVariant::Text && variant != ButtonVariant::Outlined;
    drawShadow(dl, p, q, rounding, elevation + (hov && lifts ? 1.0f : 0.0f));
    fillRoundRect(dl, p, q, rounding, bg);
    if (layer > 0) fillRoundRect(dl, p, q, rounding, fg.withAlpha(layer));
    strokeRoundRect(dl, p, q, rounding, border, dp(1));

    float x = p.x + (w - content) * 0.5f;
    const float cy = p.y + h * 0.5f;
    if (withIcon) {
        drawIcon(dl, iconName, {x + dp(iconSz) * 0.5f, cy}, iconSz, fg);
        x += dp(iconSz) + gap;
    }
    if (!text.empty()) drawText(dl, TextStyle::LabelLarge, {x, textTop(TextStyle::LabelLarge, cy)}, fg, text);
    return clicked;
}

IconButtonVariant iconButtonVariant(std::string_view n) {
    if (n == "filled") return IconButtonVariant::Filled;
    if (n == "tonal") return IconButtonVariant::Tonal;
    if (n == "outlined") return IconButtonVariant::Outlined;
    return IconButtonVariant::Standard;
}

bool iconButton(const char* id, std::string_view iconName, IconButtonVariant v, bool toggle, bool selected,
                std::optional<Color> tint) {
    const Scheme& s = scheme();
    const float target = iconButtonSize().x, box = dp(40);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, iconButtonSize());
    const bool dis = itemDisabled();
    ImGuiID iid = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const bool on = !toggle || selected;

    Color bg{0, 0, 0, 0}, fg = s.onSurfaceVariant, border{0, 0, 0, 0};
    switch (v) {
    case IconButtonVariant::Standard:
        fg = toggle && selected ? s.primary : s.onSurfaceVariant;
        break;
    case IconButtonVariant::Filled:
        bg = on ? s.primary : s.surfaceContainerHighest;
        fg = on ? s.onPrimary : s.primary;
        break;
    case IconButtonVariant::Tonal:
        bg = on ? s.secondaryContainer : s.surfaceContainerHighest;
        fg = on ? s.onSecondaryContainer : s.onSurfaceVariant;
        break;
    case IconButtonVariant::Outlined:
        if (toggle && selected) { bg = s.inverseSurface; fg = s.inverseOnSurface; }
        else border = s.outline;
        break;
    }
    if (tint) fg = *tint;
    if (dis) {
        fg = disabledContent();
        if (bg.a > 0) bg = disabledContainer();
        if (border.a > 0) border = disabledContainer();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Whole-pixel center: the 40dp circle and its 1dp ring then sit symmetrically on the pixel grid.
    const ImVec2 c{std::round(p.x + target * 0.5f), std::round(p.y + target * 0.5f)};
    fillCircle(dl, c, box * 0.5f, bg);
    strokeCircle(dl, c, box * 0.5f, border, dp(1));
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) fillCircle(dl, c, box * 0.5f, fg.withAlpha(layer));
    drawIcon(dl, iconName, c, 24.0f, fg);
    return clicked;
}

} // namespace imlui::md
