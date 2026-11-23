// Common buttons (filled, tonal, outlined, text, elevated, FAB) and icon buttons.
module;

#include <algorithm>
#include <optional>
#include <string_view>

export module imlui.widgets.button;

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

bool button(const char* label, ButtonVariant variant, ImVec2 size, std::string_view iconName) {
    const Scheme& s = scheme();
    const std::string_view text = visibleLabel(label);
    const bool fab = variant == ButtonVariant::Fab;
    const bool withIcon = hasIcon(iconName);
    const float iconSz = fab ? 24.0f : 18.0f;
    const ImVec2 ts = text.empty() ? ImVec2{0, 0} : measure(TextStyle::LabelLarge, text);
    const float gap = text.empty() || !withIcon ? 0.0f : dp(fab ? 12 : 8);
    float padL = dp(variant == ButtonVariant::Text ? 12 : (fab ? 16 : 24)), padR = padL;
    if (withIcon && !fab && variant != ButtonVariant::Text) padL = dp(16);
    const float content = (withIcon ? dp(iconSz) : 0.0f) + gap + ts.x;
    const float h = size.y > 0 ? size.y : dp(fab ? 56 : 40);
    float natural = content + padL + padR;
    if (fab && text.empty()) natural = h;
    const float w = itemWidth(size.x, std::max(natural, fab ? dp(56) : dp(48)));
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
    case ButtonVariant::Outlined: border = s.outlineVariant; fg = s.primary; break;
    case ButtonVariant::Text:     break;
    case ButtonVariant::Elevated: bg = s.surfaceContainerLow; elevation = 1.0f; break;
    case ButtonVariant::Fab:      bg = s.primaryContainer; fg = s.onPrimaryContainer; elevation = 2.0f; break;
    }
    if (variant == ButtonVariant::Outlined) border = s.outline;
    if (dis) {
        fg = disabledContent();
        if (bg.a > 0) bg = disabledContainer();
        if (border.a > 0) border = disabledContainer();
        elevation = 0.0f;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{p.x + w, p.y + h};
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    drawShadow(dl, p, q, rounding, elevation + (hov ? 0.6f : 0.0f));
    if (bg.a > 0) dl->AddRectFilled(p, q, col(bg), rounding);
    if (layer > 0) dl->AddRectFilled(p, q, col(fg.withAlpha(layer)), rounding);
    if (border.a > 0) dl->AddRect(p, q, col(border), rounding, dp(1));

    float x = p.x + (w - content) * 0.5f;
    const float cy = p.y + h * 0.5f;
    if (withIcon) {
        drawIcon(dl, iconName, {x + dp(iconSz) * 0.5f, cy}, iconSz, fg);
        x += dp(iconSz) + gap;
    }
    if (!text.empty()) drawText(dl, TextStyle::LabelLarge, {x, cy - ts.y * 0.5f}, fg, text);
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
    const float target = dp(48), box = dp(40);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {target, target});
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
    ImVec2 c{p.x + target * 0.5f, p.y + target * 0.5f};
    if (bg.a > 0) dl->AddCircleFilled(c, box * 0.5f, col(bg));
    if (border.a > 0) dl->AddCircle(c, box * 0.5f - dp(0.5f), col(border), 0, dp(1));
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddCircleFilled(c, box * 0.5f, col(fg.withAlpha(layer)));
    drawIcon(dl, iconName, c, 24.0f, fg);
    return clicked;
}

} // namespace imlui::md
