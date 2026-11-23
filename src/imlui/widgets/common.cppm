// Shared helpers for the Material 3 widgets: theme access, disabled colors,
// label parsing, animation, text / icon drawing and elevation shadows.
//
// Disabled state: widgets detect ImGui::BeginDisabled() through the item
// flags and switch to Material's disabled colors (onSurface @ 12% container,
// @ 38% content) instead of ImGui's global alpha.
module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

export module imlui.widgets.common;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.theme.theme;

export namespace imlui::md {

inline constexpr float pi = 3.14159265358979f;
/// "Unbounded" extent for text measuring and size constraints.
inline constexpr float hugeExtent = 3.4e38f;

/// Color scheme of the current theme.
[[nodiscard]] const Scheme& scheme();
/// Shape scale of the current theme.
[[nodiscard]] const Theme::Shape& shape();

/// Converts without ImGui's style alpha: disabled colors are explicit.
[[nodiscard]] ImU32 col(Color c);

/// True when the last submitted item is disabled.
[[nodiscard]] bool itemDisabled();
[[nodiscard]] Color disabledContent();
[[nodiscard]] Color disabledContainer();

/// End of the visible part of an ImGui label ("Text##id" -> "Text").
[[nodiscard]] const char* labelEnd(const char* label);
/// Visible part of an ImGui label ("Text##id" -> "Text").
[[nodiscard]] std::string_view visibleLabel(const char* label);

/// Smoothly moves a per-id stored value towards `target`.
float animate(ImGuiID id, float target, float speed = 14.0f);
/// Hover/press state-layer opacity (Material: 8% hover, 10% pressed), animated.
float stateLayer(ImGuiID id, bool hovered, bool active);

/// Resolves a requested width: > 0 fixed, < 0 fill the available width (minus |requested| if < -1), 0 natural.
[[nodiscard]] float itemWidth(float requested, float natural);

[[nodiscard]] ImVec2 measure(TextStyle style, std::string_view s);
void drawText(ImDrawList* dl, TextStyle style, ImVec2 pos, Color c, std::string_view s);
/// Draws `s` vertically centered on `cy`, starting at x. Returns its width.
float drawTextV(ImDrawList* dl, TextStyle style, float x, float cy, Color c, std::string_view s);
/// Draws a Material icon centered at `c`, `size` in dp.
void drawIcon(ImDrawList* dl, std::string_view name, ImVec2 c, float size, Color color);
/// Soft elevation shadow behind the rect [a, b]. `level` 0 = none.
void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float level);

} // namespace imlui::md

module :private;

namespace imlui::md {

const Scheme& scheme() { return theme().scheme; }
const Theme::Shape& shape() { return theme().shape; }

ImU32 col(Color c) { return ImGui::ColorConvertFloat4ToU32(c.vec4()); }

bool itemDisabled() { return (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0; }
Color disabledContent() { return scheme().onSurface.withAlpha(0.38f); }
Color disabledContainer() { return scheme().onSurface.withAlpha(0.12f); }

const char* labelEnd(const char* label) {
    std::string_view sv{label};
    auto pos = sv.find("##");
    return label + (pos == std::string_view::npos ? sv.size() : pos);
}

std::string_view visibleLabel(const char* label) {
    return {label, static_cast<std::size_t>(labelEnd(label) - label)};
}

float animate(ImGuiID id, float target, float speed) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    float dt = ImGui::GetIO().DeltaTime;
    *v += (target - *v) * std::min(1.0f, dt * speed);
    if (std::fabs(*v - target) < 0.0015f) *v = target;
    return *v;
}

float stateLayer(ImGuiID id, bool hovered, bool active) {
    return animate(id + 0x5157, active ? 0.10f : (hovered ? 0.08f : 0.0f), 22.0f);
}

float itemWidth(float requested, float natural) {
    if (requested > 0) return requested;
    if (requested < 0) return std::max(1.0f, ImGui::GetContentRegionAvail().x + (requested < -1.0f ? requested : 0.0f));
    return natural;
}

ImVec2 measure(TextStyle style, std::string_view s) {
    return font(style)->CalcTextSizeA(fontPixels(style), hugeExtent, 0.0f, s.data(), s.data() + s.size());
}

void drawText(ImDrawList* dl, TextStyle style, ImVec2 pos, Color c, std::string_view s) {
    dl->AddText(font(style), fontPixels(style), pos, col(c), s.data(), s.data() + s.size());
}

float drawTextV(ImDrawList* dl, TextStyle style, float x, float cy, Color c, std::string_view s) {
    ImVec2 sz = measure(style, s);
    drawText(dl, style, {x, cy - sz.y * 0.5f}, c, s);
    return sz.x;
}

void drawIcon(ImDrawList* dl, std::string_view name, ImVec2 c, float size, Color color) {
    ImFont* f = iconFont();
    if (!f || name.empty()) return;
    std::string t = iconText(name);
    if (t.empty()) return;
    float px = dp(size);
    ImVec2 sz = f->CalcTextSizeA(px, hugeExtent, 0.0f, t.c_str());
    dl->AddText(f, px, {std::floor(c.x - sz.x * 0.5f), std::floor(c.y - sz.y * 0.5f)}, col(color), t.c_str());
}

void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float level) {
    if (level <= 0) return;
    const bool dark = theme().dark;
    const float base = dark ? 0.22f : 0.10f;
    for (int i = 1; i <= 6; ++i) {
        float g = dp(static_cast<float>(i)) * (0.6f + 0.4f * level);
        float a0 = base * level / static_cast<float>(i + 1);
        dl->AddRectFilled({a.x - g * 0.6f, a.y - g * 0.2f + dp(1) * level}, {b.x + g * 0.6f, b.y + g + dp(1) * level},
                          col(scheme().shadow.withAlpha(a0)), rounding + g);
    }
}

} // namespace imlui::md
