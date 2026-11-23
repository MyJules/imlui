// Material 3 widgets implemented on top of ImGui primitives (InvisibleButton
// + draw list), following the current imlui::theme().
//
// Disabled state: widgets detect ImGui::BeginDisabled() through the item
// flags and switch to Material's disabled colors (onSurface @ 12% container,
// @ 38% content) instead of ImGui's global alpha.
module;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module imlui.widgets;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.theme.theme;
import imlui.theme.font_scope;

namespace imlui::md {

// ===================================================================== helpers

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kHuge = 3.4e38f;

inline ImVec2 add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
inline float dp(float v) { return v * dpiScale(); }
inline const Scheme& S() { return theme().scheme; }
inline const Theme::Shape& shape() { return theme().shape; }

/// Converts without ImGui's style alpha: disabled colors are explicit.
inline ImU32 col(Color c) { return ImGui::ColorConvertFloat4ToU32(c.vec4()); }

inline bool itemDisabled() { return (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0; }
inline Color disabledContent() { return S().onSurface.withAlpha(0.38f); }
inline Color disabledContainer() { return S().onSurface.withAlpha(0.12f); }

/// End of the visible part of an ImGui label ("Text##id" -> "Text").
inline const char* labelEnd(const char* label) {
    std::string_view sv{label};
    auto pos = sv.find("##");
    return label + (pos == std::string_view::npos ? sv.size() : pos);
}
inline std::string_view visible(const char* label) {
    return {label, static_cast<std::size_t>(labelEnd(label) - label)};
}

/// Smoothly moves a per-id stored value towards `target`.
inline float animate(ImGuiID id, float target, float speed = 14.0f) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    float dt = ImGui::GetIO().DeltaTime;
    *v += (target - *v) * std::min(1.0f, dt * speed);
    if (std::fabs(*v - target) < 0.0015f) *v = target;
    return *v;
}

/// Hover/press state-layer opacity (Material: 8% hover, 10% pressed), animated.
inline float stateLayer(ImGuiID id, bool hovered, bool active) {
    return animate(id + 0x5157, active ? 0.10f : (hovered ? 0.08f : 0.0f), 22.0f);
}

inline float itemWidth(float requested, float natural) {
    if (requested > 0) return requested;
    if (requested < 0) return std::max(1.0f, ImGui::GetContentRegionAvail().x + (requested < -1.0f ? requested : 0.0f));
    return natural;
}

// ------------------------------------------------------------- text & icons

inline ImVec2 measure(TextStyle st, std::string_view s) {
    return font(st)->CalcTextSizeA(fontPixels(st), kHuge, 0.0f, s.data(), s.data() + s.size());
}
inline void drawText(ImDrawList* dl, TextStyle st, ImVec2 pos, Color c, std::string_view s) {
    dl->AddText(font(st), fontPixels(st), pos, col(c), s.data(), s.data() + s.size());
}
/// Draws `s` vertically centered on `cy`, starting at x. Returns its width.
inline float drawTextV(ImDrawList* dl, TextStyle st, float x, float cy, Color c, std::string_view s) {
    ImVec2 sz = measure(st, s);
    drawText(dl, st, {x, cy - sz.y * 0.5f}, c, s);
    return sz.x;
}

/// Draws a Material icon centered at `c`, `size` in dp.
inline void drawIcon(ImDrawList* dl, std::string_view name, ImVec2 c, float size, Color color) {
    ImFont* f = iconFont();
    if (!f || name.empty()) return;
    std::string t = iconText(name);
    if (t.empty()) return;
    float px = dp(size);
    ImVec2 sz = f->CalcTextSizeA(px, kHuge, 0.0f, t.c_str());
    dl->AddText(f, px, {std::floor(c.x - sz.x * 0.5f), std::floor(c.y - sz.y * 0.5f)}, col(color), t.c_str());
}

// ------------------------------------------------------------- surfaces

namespace detail {
inline std::vector<Color> g_surfaces;
} // namespace detail

inline void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float level) {
    if (level <= 0) return;
    const bool dark = theme().dark;
    const float base = dark ? 0.22f : 0.10f;
    for (int i = 1; i <= 6; ++i) {
        float g = dp(static_cast<float>(i)) * (0.6f + 0.4f * level);
        float a0 = base * level / static_cast<float>(i + 1);
        dl->AddRectFilled({a.x - g * 0.6f, a.y - g * 0.2f + dp(1) * level}, {b.x + g * 0.6f, b.y + g + dp(1) * level},
                          col(S().shadow.withAlpha(a0)), rounding + g);
    }
}

} // namespace imlui::md

export namespace imlui::md {

/// Color of the surface widgets are currently drawn on (for cutouts).
Color surfaceColor() {
    if (!detail::g_surfaces.empty()) return detail::g_surfaces.back();
    return Color::from(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
}

void pushSurface(Color c) { detail::g_surfaces.push_back(c); }
void popSurface() { if (!detail::g_surfaces.empty()) detail::g_surfaces.pop_back(); }

// ===================================================================== text

void text(std::string_view s, TextStyle style, std::optional<Color> color = std::nullopt, bool wrap = false,
          float align = 0.0f) {
    FontScope fs{style};
    if (color) ImGui::PushStyleColor(ImGuiCol_Text, color->vec4());
    if (wrap) ImGui::PushTextWrapPos(0.0f);
    if (align > 0.0f && !wrap) {
        float w = ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * align);
    }
    ImGui::TextUnformatted(s.data(), s.data() + s.size());
    if (wrap) ImGui::PopTextWrapPos();
    if (color) ImGui::PopStyleColor();
}

/// A standalone icon item (size in dp).
void icon(std::string_view name, float size = 24.0f, std::optional<Color> color = std::nullopt) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({dp(size), dp(size)});
    drawIcon(ImGui::GetWindowDrawList(), name, {p.x + dp(size) * 0.5f, p.y + dp(size) * 0.5f}, size,
             itemDisabled() ? disabledContent() : color.value_or(S().onSurfaceVariant));
}

/// Plain tooltip (inverseSurface) for the last item.
void tooltip(std::string_view s) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, S().inverseSurface.vec4());
    ImGui::PushStyleColor(ImGuiCol_Text, S().inverseOnSurface.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(8), dp(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(shape().extraSmall));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::BeginTooltip();
    {
        FontScope fs{TextStyle::BodySmall};
        ImGui::TextUnformatted(s.data(), s.data() + s.size());
    }
    ImGui::EndTooltip();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

// ===================================================================== buttons

enum class ButtonVariant { Filled, Tonal, Outlined, Text, Elevated, Fab };

ButtonVariant buttonVariant(std::string_view name) {
    if (name == "tonal") return ButtonVariant::Tonal;
    if (name == "outlined") return ButtonVariant::Outlined;
    if (name == "text") return ButtonVariant::Text;
    if (name == "elevated") return ButtonVariant::Elevated;
    if (name == "fab") return ButtonVariant::Fab;
    return ButtonVariant::Filled;
}

/// Common button. `iconName` is an optional leading Material icon.
/// A FAB with an empty label is a regular 56dp FAB, otherwise extended.
bool button(const char* label, ButtonVariant variant = ButtonVariant::Filled, ImVec2 size = {0, 0},
            std::string_view iconName = {}) {
    const Scheme& s = S();
    const std::string_view text = visible(label);
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
    ImVec2 q = add(p, {w, h});
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

enum class IconButtonVariant { Standard, Filled, Tonal, Outlined };

IconButtonVariant iconButtonVariant(std::string_view n) {
    if (n == "filled") return IconButtonVariant::Filled;
    if (n == "tonal") return IconButtonVariant::Tonal;
    if (n == "outlined") return IconButtonVariant::Outlined;
    return IconButtonVariant::Standard;
}

/// Icon button (40dp container, 48dp touch target). `toggle` enables the
/// selected/unselected look.
bool iconButton(const char* id, std::string_view iconName, IconButtonVariant v = IconButtonVariant::Standard,
                bool toggle = false, bool selected = false, std::optional<Color> tint = std::nullopt) {
    const Scheme& s = S();
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

// ===================================================================== selection controls

inline float controlRowHeight() { return dp(40); }

bool checkbox(const char* label, bool* v) {
    const Scheme& s = S();
    const float box = dp(18), h = controlRowHeight();
    const std::string_view text = visible(label);
    const float labelW = text.empty() ? 0.0f : dp(4) + measure(TextStyle::BodyLarge, text).x + dp(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {h + labelW, h});
    if (clicked) *v = !*v;
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c{p.x + h * 0.5f, p.y + h * 0.5f};
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) dl->AddCircleFilled(c, h * 0.5f, col((*v ? s.primary : s.onSurface).withAlpha(layer)));
    ImVec2 a{c.x - box * 0.5f, c.y - box * 0.5f}, b{c.x + box * 0.5f, c.y + box * 0.5f};
    float t = animate(id + 1, *v ? 1.0f : 0.0f, 20.0f);
    Color fill = dis ? disabledContent() : s.primary;
    Color outline = dis ? disabledContent() : s.onSurfaceVariant;
    if (t > 0.01f) {
        dl->AddRectFilled(a, b, col(fill.withAlpha(fill.a * t)), dp(2));
        drawIcon(dl, "check", c, 18.0f * (0.6f + 0.4f * t), (dis ? s.surface : s.onPrimary).withAlpha(t));
    }
    if (t < 0.99f) dl->AddRect(a, b, col(outline.withAlpha(outline.a * (1 - t))), dp(2), dp(2));
    if (!text.empty()) drawTextV(dl, TextStyle::BodyLarge, p.x + h + dp(4), c.y, dis ? disabledContent() : s.onSurface, text);
    return clicked;
}

bool radio(const char* label, bool selected) {
    const Scheme& s = S();
    const float h = controlRowHeight();
    const std::string_view text = visible(label);
    const float labelW = text.empty() ? 0.0f : dp(4) + measure(TextStyle::BodyLarge, text).x + dp(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {h + labelW, h});
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c{p.x + h * 0.5f, p.y + h * 0.5f};
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) dl->AddCircleFilled(c, h * 0.5f, col((selected ? s.primary : s.onSurface).withAlpha(layer)));
    Color ring = dis ? disabledContent() : (selected ? s.primary : s.onSurfaceVariant);
    dl->AddCircle(c, dp(9), col(ring), 0, dp(2));
    float t = animate(id + 1, selected ? 1.0f : 0.0f, 18.0f);
    if (t > 0.01f) dl->AddCircleFilled(c, dp(5) * t, col(ring));
    if (!text.empty()) drawTextV(dl, TextStyle::BodyLarge, p.x + h + dp(4), c.y, dis ? disabledContent() : s.onSurface, text);
    return clicked;
}

bool switchToggle(const char* label, bool* v, bool withIcons = true) {
    const Scheme& s = S();
    const float tw = dp(52), th = dp(32), h = controlRowHeight();
    const std::string_view text = visible(label);
    const float labelW = text.empty() ? 0.0f : dp(12) + measure(TextStyle::BodyLarge, text).x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {tw + labelW, h});
    if (clicked) *v = !*v;
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a{p.x, p.y + (h - th) * 0.5f}, b{a.x + tw, a.y + th};
    float t = animate(id + 1, *v ? 1.0f : 0.0f, 16.0f);

    Color trackOff = s.surfaceContainerHighest, trackOn = s.primary;
    Color thumbOff = hov ? s.onSurfaceVariant : s.outline, thumbOn = hov ? s.primaryContainer : s.onPrimary;
    Color border = s.outline;
    if (dis) {
        trackOff = s.surfaceContainerHighest.withAlpha(0.12f);
        trackOn = s.onSurface.withAlpha(0.12f);
        thumbOff = s.onSurface.withAlpha(0.38f);
        thumbOn = s.surface;
        border = s.onSurface.withAlpha(0.12f);
    }
    dl->AddRectFilled(a, b, col(mix(trackOff, trackOn, t)), th * 0.5f);
    if (t < 0.99f) dl->AddRect(a, b, col(border.withAlpha(border.a * (1 - t))), th * 0.5f, dp(2));

    const bool showIcon = withIcons && t > 0.5f;
    float r = act ? dp(14) : (showIcon ? dp(12) : dp(8) + dp(4) * t);
    ImVec2 c{a.x + th * 0.5f + (tw - th) * t, a.y + th * 0.5f};
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) dl->AddCircleFilled(c, dp(20), col((*v ? s.primary : s.onSurface).withAlpha(layer)));
    dl->AddCircleFilled(c, r, col(mix(thumbOff, thumbOn, t)));
    if (showIcon) drawIcon(dl, "check", c, 16.0f, dis ? s.onSurface.withAlpha(0.38f) : s.onPrimaryContainer);
    if (!text.empty()) drawTextV(dl, TextStyle::BodyLarge, p.x + tw + dp(12), p.y + h * 0.5f, dis ? disabledContent() : s.onSurface, text);
    return clicked;
}

// ===================================================================== slider

/// Material slider. `step` <= 0 means continuous. Returns true when the value changed.
bool slider(const char* id, float* v, float vmin, float vmax, float step = 0.0f, float width = 0.0f,
            const char* valueFormat = "%.2f") {
    const Scheme& s = S();
    const float w = itemWidth(width == 0.0f ? -1.0f : width, dp(200));
    const float h = dp(44);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {w, h});
    const bool dis = itemDisabled();
    ImGuiID iid = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    const float r = dp(10);
    const float x0 = p.x + r, x1 = p.x + w - r;
    bool changed = false;
    if (act && vmax > vmin) {
        float t = std::clamp((ImGui::GetMousePos().x - x0) / std::max(1.0f, x1 - x0), 0.0f, 1.0f);
        float nv = vmin + t * (vmax - vmin);
        if (step > 0) nv = vmin + std::round((nv - vmin) / step) * step;
        nv = std::clamp(nv, vmin, vmax);
        if (nv != *v) { *v = nv; changed = true; }
    }
    float t = vmax > vmin ? std::clamp((*v - vmin) / (vmax - vmin), 0.0f, 1.0f) : 0.0f;
    float tv = animate(iid + 2, t, 30.0f);

    Color active = dis ? s.onSurface.withAlpha(0.38f) : s.primary;
    Color inactive = dis ? s.onSurface.withAlpha(0.12f) : s.secondaryContainer;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f, th = dp(4);
    const float hx = x0 + (x1 - x0) * tv;
    dl->AddRectFilled({x0, cy - th * 0.5f}, {x1, cy + th * 0.5f}, col(inactive), th);
    if (hx > x0) dl->AddRectFilled({x0, cy - th * 0.5f}, {hx, cy + th * 0.5f}, col(active), th);
    if (step > 0 && vmax > vmin && (vmax - vmin) / step <= 40) {
        for (float sv = vmin; sv <= vmax + step * 0.5f; sv += step) {
            float sx = x0 + (x1 - x0) * ((sv - vmin) / (vmax - vmin));
            dl->AddCircleFilled({sx, cy}, dp(1), col(sx <= hx ? s.onPrimary.withAlpha(0.7f) : s.onSecondaryContainer.withAlpha(0.7f)));
        }
    }
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddCircleFilled({hx, cy}, dp(20), col(s.primary.withAlpha(layer * 1.5f)));
    dl->AddCircleFilled({hx, cy}, r * (act ? 1.1f : 1.0f), col(active));

    if (act && valueFormat && *valueFormat) {
        char buf[64];
        std::snprintf(buf, sizeof buf, valueFormat, static_cast<double>(*v));
        ImVec2 ts = measure(TextStyle::LabelMedium, buf);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float bw = std::max(ts.x + dp(16), dp(28)), bh = dp(28);
        ImVec2 ba{hx - bw * 0.5f, cy - r - dp(8) - bh};
        fg->AddRectFilled(ba, {ba.x + bw, ba.y + bh}, col(s.inverseSurface), bh * 0.5f);
        drawText(fg, TextStyle::LabelMedium, {hx - ts.x * 0.5f, ba.y + (bh - ts.y) * 0.5f}, s.inverseOnSurface, buf);
    }
    return changed;
}

// ===================================================================== progress

/// Linear progress (M3 2024: gap + stop indicator). `fraction` < 0 = indeterminate.
void progress(float fraction, float width = -1.0f) {
    const Scheme& s = S();
    const float w = itemWidth(width, dp(240)), h = dp(4), gap = dp(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({w, h + dp(8)});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = p.y + dp(4), x1 = p.x + w;
    auto seg = [&](float a, float b, Color c) {
        if (b - a > 0.5f) dl->AddRectFilled({a, y}, {b, y + h}, col(c), h * 0.5f);
    };
    if (fraction >= 0.0f) {
        float ax = p.x + w * std::clamp(fraction, 0.0f, 1.0f);
        seg(p.x, ax, s.primary);
        seg(fraction > 0.0f ? ax + gap : p.x, x1, s.secondaryContainer);
        if (fraction < 1.0f) dl->AddCircleFilled({x1 - h * 0.5f, y + h * 0.5f}, h * 0.5f, col(s.primary));
    } else {
        float t = static_cast<float>(std::fmod(ImGui::GetTime() * 0.55, 1.0));
        auto ease = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); };
        float a1 = ease(t * 1.4f - 0.1f), b1 = ease(t * 1.4f + 0.25f);
        float a2 = ease(t * 1.6f - 0.9f), b2 = ease(t * 1.6f - 0.55f);
        float xa1 = p.x + w * a1, xb1 = p.x + w * b1, xa2 = p.x + w * a2, xb2 = p.x + w * b2;
        // track pieces around the two moving segments
        seg(p.x, std::min(xa2, xa1) - gap, s.secondaryContainer);
        seg(xb2 + gap, xa1 - gap, s.secondaryContainer);
        seg(xb1 + gap, x1, s.secondaryContainer);
        seg(xa1, xb1, s.primary);
        seg(xa2, xb2, s.primary);
    }
}

/// Circular progress. `fraction` < 0 = indeterminate.
void circularProgress(float fraction, float diameter = 0.0f, float thickness = 4.0f) {
    const Scheme& s = S();
    const float d = diameter > 0 ? diameter : dp(48);
    const float th = dp(thickness);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({d, d});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c{p.x + d * 0.5f, p.y + d * 0.5f};
    const float r = d * 0.5f - th * 0.5f - dp(2);
    const float gapA = (th + dp(4)) / r;
    const float top = -kPi * 0.5f;
    auto arc = [&](float a0, float a1, Color cl) {
        if (a1 - a0 < 0.01f) return;
        dl->PathArcTo(c, r, a0, a1, 48);
        dl->PathStroke(col(cl), th);
    };
    if (fraction >= 0.0f) {
        float f = std::clamp(fraction, 0.0f, 1.0f);
        float end = top + f * 2 * kPi;
        arc(top, end, s.primary);
        if (f < 1.0f) arc(end + gapA, top + 2 * kPi - (f > 0 ? gapA : 0.0f), s.secondaryContainer);
    } else {
        float t = static_cast<float>(ImGui::GetTime());
        float a0 = t * 5.0f;
        float span = 0.35f + 0.5f * (1.0f + std::sin(t * 2.6f)) * kPi * 0.65f;
        arc(a0, a0 + span, s.primary);
    }
}

// ===================================================================== chips & segmented

enum class ChipVariant { Filter, Assist, Suggestion, Input };

ChipVariant chipVariant(std::string_view n) {
    if (n == "assist") return ChipVariant::Assist;
    if (n == "suggestion") return ChipVariant::Suggestion;
    if (n == "input") return ChipVariant::Input;
    return ChipVariant::Filter;
}

bool chip(const char* label, bool selected, ChipVariant v = ChipVariant::Filter, std::string_view leadingIcon = {}) {
    const Scheme& s = S();
    const std::string_view text = visible(label);
    ImVec2 ts = measure(TextStyle::LabelLarge, text);
    const bool check = v == ChipVariant::Filter && selected;
    const bool lead = check || hasIcon(leadingIcon);
    const float h = dp(32), iconW = lead ? dp(18) + dp(8) : 0.0f;
    const float padL = lead ? dp(8) : dp(16), padR = dp(16);
    const float w = padL + iconW + ts.x + padR;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    const bool dis = itemDisabled();
    ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q = add(p, {w, h});
    const float rnd = dp(shape().small);
    const bool filled = selected && v != ChipVariant::Assist;
    Color fg = filled ? s.onSecondaryContainer : (v == ChipVariant::Assist ? s.onSurface : s.onSurfaceVariant);
    Color iconCol = filled ? s.onSecondaryContainer : s.primary;
    if (dis) { fg = disabledContent(); iconCol = fg; }
    if (filled) dl->AddRectFilled(p, q, col(dis ? disabledContainer() : s.secondaryContainer), rnd);
    else dl->AddRect(p, q, col(dis ? disabledContainer() : s.outline), rnd, dp(1));
    float layer = dis ? 0.0f : stateLayer(id, hov, act);
    if (layer > 0) dl->AddRectFilled(p, q, col(fg.withAlpha(layer)), rnd);
    float x = p.x + padL;
    if (lead) {
        drawIcon(dl, check ? std::string_view{"check"} : leadingIcon, {x + dp(9), p.y + h * 0.5f}, 18.0f, iconCol);
        x += iconW;
    }
    drawText(dl, TextStyle::LabelLarge, {x, p.y + (h - ts.y) * 0.5f}, fg, text);
    return clicked;
}

/// Segmented button: one of `items` selected. Returns true when changed.
bool segmented(const char* id, const std::vector<std::string>& items, int* current, float width = 0.0f,
               const std::vector<std::string>& icons = {}) {
    if (items.empty()) return false;
    const Scheme& s = S();
    const float h = dp(40);
    float natural = 0;
    for (auto& it : items) natural = std::max(natural, measure(TextStyle::LabelLarge, it).x + dp(24) * 2 + dp(26));
    const float w = itemWidth(width, natural * static_cast<float>(items.size()));
    const float segW = w / static_cast<float>(items.size());
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    const int n = static_cast<int>(items.size());
    bool anyDisabled = false;
    for (int i = 0; i < n; ++i) {
        ImVec2 a{p.x + segW * static_cast<float>(i), p.y}, b{a.x + segW, p.y + h};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", {segW, h}) && *current != i) { *current = i; changed = true; }
        const bool dis = itemDisabled();
        anyDisabled = anyDisabled || dis;
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        ImGuiID sid = ImGui::GetItemID();
        ImGui::PopID();
        ImDrawFlags corners = n == 1 ? ImDrawFlags_RoundCornersAll
                            : i == 0 ? ImDrawFlags_RoundCornersLeft
                            : i == n - 1 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
        bool sel = *current == i;
        Color fg = dis ? disabledContent() : (sel ? s.onSecondaryContainer : s.onSurface);
        float selT = animate(sid + 7, sel ? 1.0f : 0.0f, 18.0f);
        if (selT > 0.01f)
            dl->AddRectFilled(a, b, col((dis ? disabledContainer() : s.secondaryContainer).withAlpha(selT)), h * 0.5f, corners);
        float layer = dis ? 0.0f : stateLayer(sid, hov, act);
        if (layer > 0) dl->AddRectFilled(a, b, col(fg.withAlpha(layer)), h * 0.5f, corners);
        ImVec2 ts = measure(TextStyle::LabelLarge, items[i]);
        const std::string_view ic = sel ? std::string_view{"check"}
                                        : (i < static_cast<int>(icons.size()) ? std::string_view{icons[i]} : std::string_view{});
        const bool showIcon = hasIcon(ic);
        float iconW = showIcon ? dp(18) + dp(8) : 0.0f;
        float x = a.x + (segW - ts.x - iconW) * 0.5f;
        if (showIcon) drawIcon(dl, ic, {x + dp(9), p.y + h * 0.5f}, 18.0f, fg);
        drawText(dl, TextStyle::LabelLarge, {x + iconW, p.y + (h - ts.y) * 0.5f}, fg, items[i]);
        if (i > 0) dl->AddLine({a.x, a.y}, {a.x, b.y}, col(anyDisabled ? disabledContainer() : s.outline), dp(1));
    }
    dl->AddRect(p, {p.x + w, p.y + h}, col(anyDisabled ? disabledContainer() : s.outline), h * 0.5f, dp(1));
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    return changed;
}

// ===================================================================== text fields

namespace detail {
inline int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

struct FieldFrame {
    ImVec2 p;          ///< top-left of the container
    float w = 0, h = 0;
    float inputX = 0;  ///< where content starts
    float inputW = 0;  ///< content width
    bool outlined = false;
};

/// Draws the container, label, icons and indicator of a text-field-like
/// control. `focused` drives the active colors, `filled` = has content.
inline void drawFieldDecor(const FieldFrame& f, std::string_view label, bool focused, bool hovered, bool filled,
                           bool error, bool dis, std::string_view leadingIcon, std::string_view trailingIcon,
                           ImGuiID animId, bool multiline) {
    const Scheme& s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = f.p, q{p.x + f.w, p.y + f.h};
    const float rnd = dp(shape().extraSmall);
    const Color accent = error ? s.error : s.primary;
    const float labelT = animate(animId, (focused || filled) ? 1.0f : 0.0f, 18.0f);

    if (!f.outlined) {
        float lineH = focused ? dp(2) : dp(1);
        Color lc = dis ? s.onSurface.withAlpha(0.38f) : (focused || error ? accent : (hovered ? s.onSurface : s.onSurfaceVariant));
        dl->AddRectFilled({p.x, q.y - lineH}, q, col(lc));
    }

    // Label: floats from the middle (bodyLarge) to the top (bodySmall).
    float labelGapX0 = 0, labelGapX1 = 0;
    if (!label.empty()) {
        const float big = fontPixels(TextStyle::BodyLarge), small = fontPixels(TextStyle::BodySmall);
        const float size = big + (small - big) * labelT;
        ImFont* fnt = font(TextStyle::BodyLarge);
        const float restY = multiline ? p.y + dp(16) : p.y + (f.h - big) * 0.5f;
        const float topY = f.outlined ? p.y - small * 0.5f : p.y + dp(8);
        const float y = restY + (topY - restY) * labelT;
        float x = f.inputX;
        if (f.outlined && labelT > 0.0f) x = f.inputX + ((p.x + dp(16)) - f.inputX) * labelT;
        Color lc = dis ? s.onSurface.withAlpha(0.38f) : (error ? s.error : (focused ? s.primary : s.onSurfaceVariant));
        float tw = fnt->CalcTextSizeA(size, kHuge, 0.0f, label.data(), label.data() + label.size()).x;
        if (f.outlined && labelT > 0.0f) {
            labelGapX0 = x - dp(4);
            labelGapX1 = x + tw + dp(4);
        }
        dl->AddText(fnt, size, {x, y}, col(lc), label.data(), label.data() + label.size());
    }

    if (f.outlined) {
        Color bc = dis ? s.onSurface.withAlpha(0.12f) : (error ? s.error : (focused ? s.primary : (hovered ? s.onSurface : s.outline)));
        float th = focused ? dp(2) : dp(1);
        if (labelGapX1 > labelGapX0) {
            // Border with a notch where the floating label sits.
            dl->PushClipRect({p.x - dp(4), p.y - dp(4)}, {labelGapX0, q.y + dp(4)}, true);
            dl->AddRect(p, q, col(bc), rnd, th);
            dl->PopClipRect();
            dl->PushClipRect({labelGapX1, p.y - dp(4)}, {q.x + dp(4), q.y + dp(4)}, true);
            dl->AddRect(p, q, col(bc), rnd, th);
            dl->PopClipRect();
            dl->PushClipRect({labelGapX0, p.y + dp(6)}, {labelGapX1, q.y + dp(4)}, true);
            dl->AddRect(p, q, col(bc), rnd, th);
            dl->PopClipRect();
        } else {
            dl->AddRect(p, q, col(bc), rnd, th);
        }
    }

    const Color ic = dis ? s.onSurface.withAlpha(0.38f) : s.onSurfaceVariant;
    if (hasIcon(leadingIcon)) drawIcon(dl, leadingIcon, {p.x + dp(12) + dp(12), p.y + (multiline ? dp(28) : f.h * 0.5f)}, 24.0f, ic);
    if (hasIcon(trailingIcon))
        drawIcon(dl, trailingIcon, {q.x - dp(12) - dp(12), p.y + (multiline ? dp(28) : f.h * 0.5f)}, 24.0f,
                 error && !dis ? s.error : ic);
}

/// Container of a filled field. Drawn *before* the input so text stays on top.
inline void drawFieldBackground(const FieldFrame& f, bool focused, bool hovered, bool dis) {
    if (f.outlined) return;
    const Scheme& s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{f.p.x + f.w, f.p.y + f.h};
    const float rnd = dp(shape().extraSmall);
    dl->AddRectFilled(f.p, q, col(dis ? s.onSurface.withAlpha(0.04f) : s.surfaceContainerHighest), rnd, ImDrawFlags_RoundCornersTop);
    if (hovered && !focused && !dis) dl->AddRectFilled(f.p, q, col(s.onSurface.withAlpha(0.08f)), rnd, ImDrawFlags_RoundCornersTop);
}
/// True inside ImGui::BeginDisabled() (usable before the item is submitted).
inline bool scopeDisabled() { return ImGui::GetStyle().Alpha < 0.99f; }

inline void drawSupporting(ImVec2 at, float w, std::string_view text, bool error, bool dis) {
    if (text.empty()) return;
    const Scheme& s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Color c = dis ? s.onSurface.withAlpha(0.38f) : (error ? s.error : s.onSurfaceVariant);
    dl->PushClipRect(at, {at.x + w, at.y + dp(20)}, true);
    drawText(dl, TextStyle::BodySmall, {at.x + dp(16), at.y + dp(4)}, c, text);
    dl->PopClipRect();
}
} // namespace detail

struct TextFieldOptions {
    std::string_view label, placeholder, leadingIcon, trailingIcon, supporting;
    bool outlined = false, password = false, multiline = false, readOnly = false, error = false;
    float width = 0, height = 0;
};

struct TextFieldResult {
    bool changed = false;
    bool accepted = false;         ///< Enter pressed (single line)
    bool trailingClicked = false;  ///< trailing icon clicked
};

/// Material text field (filled or outlined) with floating label, icons and supporting text.
TextFieldResult textField(const char* id, std::string& value, const TextFieldOptions& o) {
    const Scheme& s = S();
    TextFieldResult res;
    const float w = itemWidth(o.width == 0.0f ? -1.0f : o.width, dp(280));
    const float h = o.multiline ? (o.height > 0 ? o.height : dp(120)) : dp(56);
    const bool hasLabel = !o.label.empty();
    const bool lead = hasIcon(o.leadingIcon);
    // Password fields get a visibility toggle unless another trailing icon was asked for.
    ImGui::PushID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID focusKey = ImGui::GetID("##focused"), revealKey = ImGui::GetID("##reveal");
    const bool focused = st->GetBool(focusKey, false);
    bool reveal = st->GetBool(revealKey, false);
    std::string_view trailing = o.trailingIcon;
    if (o.password && trailing.empty()) trailing = reveal ? "visibility_off" : "visibility";
    const bool trail = hasIcon(trailing);

    detail::FieldFrame f;
    f.p = ImGui::GetCursorScreenPos();
    f.w = w;
    f.h = h;
    f.outlined = o.outlined;
    f.inputX = f.p.x + (lead ? dp(52) : dp(16));
    f.inputW = w - (f.inputX - f.p.x) - (trail ? dp(52) : dp(16));

    ImGui::BeginGroup();
    detail::drawFieldBackground(f, focused, ImGui::IsMouseHoveringRect(f.p, {f.p.x + w, f.p.y + h}), detail::scopeDisabled());
    // Body text at bodyLarge, vertically placed per variant.
    pushFont(TextStyle::BodyLarge);
    const float lineH = ImGui::GetFontSize();
    float inputY;
    if (o.multiline) inputY = (hasLabel && !o.outlined) ? dp(24) : dp(16);
    else if (hasLabel && !o.outlined) inputY = dp(24) + (dp(24) - lineH) * 0.5f;
    else inputY = (h - lineH) * 0.5f;

    ImGui::SetCursorScreenPos({f.inputX, f.p.y + inputY});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, (o.error ? s.error : s.primary).vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (o.password && !reveal) flags |= ImGuiInputTextFlags_Password;
    if (o.readOnly) flags |= ImGuiInputTextFlags_ReadOnly;
    if (o.multiline) {
        res.changed = ImGui::InputTextMultiline("##input", value.data(), value.capacity() + 1,
                                                {f.inputW, h - inputY - dp(8)}, flags, detail::resizeCallback, &value);
    } else {
        flags |= ImGuiInputTextFlags_EnterReturnsTrue;
        ImGui::SetNextItemWidth(f.inputW);
        std::string hint{(!o.placeholder.empty() && (!hasLabel || focused)) ? o.placeholder : std::string_view{}};
        res.accepted = ImGui::InputTextWithHint("##input", hint.c_str(), value.data(), value.capacity() + 1, flags,
                                                detail::resizeCallback, &value);
        res.changed = ImGui::IsItemEdited();
    }
    const bool active = ImGui::IsItemActive();
    const bool dis = itemDisabled();
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    st->SetBool(focusKey, active);

    // Clicking anywhere in the container focuses the input.
    if (!active && !dis && ImGui::IsMouseHoveringRect(f.p, {f.p.x + w, f.p.y + h})) {
        hovered = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) {
            ImGui::SetCursorScreenPos({f.inputX, f.p.y + inputY});
        }
    }
    if (trail) {
        ImGui::SetCursorScreenPos({f.p.x + w - dp(48) - dp(2), f.p.y + (o.multiline ? dp(4) : (h - dp(48)) * 0.5f)});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        if (ImGui::InvisibleButton("##trailing", {dp(48), dp(48)})) {
            res.trailingClicked = true;
            if (o.password && o.trailingIcon.empty()) st->SetBool(revealKey, !reveal);
        }
        ImGui::PopStyleVar();
    }
    detail::drawFieldDecor(f, o.label, active, hovered, !value.empty(), o.error, dis, o.leadingIcon, trailing,
                           ImGui::GetID("##lbl"), o.multiline);

    ImGui::SetCursorScreenPos(f.p);
    ImGui::Dummy({w, h});
    if (!o.supporting.empty()) {
        ImVec2 sp = ImGui::GetCursorScreenPos();
        detail::drawSupporting({f.p.x, f.p.y + h}, w, o.supporting, o.error, dis);
        ImGui::SetCursorScreenPos({sp.x, f.p.y + h});
        ImGui::Dummy({w, dp(20)});
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return res;
}

// ===================================================================== menus

namespace detail {
inline void drawMenuShadow(ImGuiID key) {
    // Popups can't draw outside themselves: draw the shadow of last frame's
    // popup rect behind it, in the parent window.
    ImGuiStorage* st = ImGui::GetStateStorage();
    float x = st->GetFloat(key + 1, 0), y = st->GetFloat(key + 2, 0), w = st->GetFloat(key + 3, 0), h = st->GetFloat(key + 4, 0);
    if (w <= 0) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();
    drawShadow(dl, {x, y}, {x + w, y + h}, dp(shape().extraSmall), 2.0f);
    dl->PopClipRect();
}
inline void storeMenuRect(ImGuiStorage* parent, ImGuiID key) {
    ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    parent->SetFloat(key + 1, p.x);
    parent->SetFloat(key + 2, p.y);
    parent->SetFloat(key + 3, sz.x);
    parent->SetFloat(key + 4, sz.y);
}
} // namespace detail

/// Push before BeginPopup/BeginMenu so the popup gets Material menu styling.
void pushMenuStyle() {
    const Scheme& s = S();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s.surfaceContainer.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, s.outlineVariant.withAlpha(0.5f).vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, dp(8)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(shape().extraSmall));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, theme().dark ? 0.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
}
void popMenuStyle() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

/// One menu row (48dp). Must be inside a popup/menu. Returns true when chosen.
bool menuItem(const char* id, std::string_view label, std::string_view leadingIcon = {}, std::string_view trailing = {},
              bool selected = false, bool closeOnClick = true, bool reserveIcon = false) {
    const Scheme& s = S();
    const float h = dp(48);
    const bool lead = hasIcon(leadingIcon) || reserveIcon;
    float natural = dp(12) + (lead ? dp(24) + dp(12) : 0.0f) + measure(TextStyle::BodyLarge, label).x + dp(12);
    if (!trailing.empty()) natural += dp(32) + measure(TextStyle::LabelLarge, trailing).x;
    natural = std::max(natural, dp(160));
    const float w = std::max(natural, ImGui::GetContentRegionAvail().x);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Header, s.secondaryContainer.vec4());
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, s.onSurface.withAlpha(0.08f).vec4());
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, s.onSurface.withAlpha(0.12f).vec4());
    bool clicked = ImGui::Selectable(id, selected, closeOnClick ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_NoAutoClosePopups, {w, h});
    ImGui::PopStyleColor(3);
    const bool dis = itemDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    float x = p.x + dp(12);
    Color fg = dis ? disabledContent() : (selected ? s.onSecondaryContainer : s.onSurface);
    if (lead) {
        drawIcon(dl, leadingIcon, {x + dp(12), cy}, 24.0f, dis ? disabledContent() : (selected ? fg : s.onSurfaceVariant));
        x += dp(24) + dp(12);
    }
    drawTextV(dl, TextStyle::BodyLarge, x, cy, fg, label);
    if (!trailing.empty()) {
        float tw = measure(TextStyle::LabelLarge, trailing).x;
        drawTextV(dl, TextStyle::LabelLarge, p.x + w - dp(12) - tw, cy, dis ? disabledContent() : s.onSurfaceVariant, trailing);
    }
    return clicked;
}

void menuDivider() {
    const Scheme& s = S();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy({w, dp(17)});
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y + dp(8)}, {p.x + w, p.y + dp(8)}, col(s.outlineVariant), dp(1));
}

/// Begins a dropdown-style popup anchored below [a, b]. Returns true while open.
bool beginAnchoredMenu(const char* popupId, ImVec2 a, ImVec2 b, float minWidth) {
    const ImGuiID key = ImGui::GetID(popupId);
    if (ImGui::IsPopupOpen(popupId)) detail::drawMenuShadow(key);
    ImGuiStorage* parent = ImGui::GetStateStorage();
    ImGui::SetNextWindowPos({a.x, b.y + dp(2)}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({minWidth, 0}, {kHuge, dp(48) * 8 + dp(16)});
    pushMenuStyle();
    bool open = ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
    popMenuStyle();
    if (open) {
        detail::storeMenuRect(parent, key);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    } else {
        parent->SetFloat(key + 3, 0);
    }
    (void)b;
    return open;
}
void endAnchoredMenu() {
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

// ===================================================================== dropdown / spin / color

struct DropdownOptions {
    std::string_view label, leadingIcon, supporting;
    bool outlined = false, error = false;
    float width = 0;
};

/// Exposed dropdown menu (text-field look + menu). `current` is 0-based, -1 = none.
bool dropdown(const char* id, const std::vector<std::string>& items, int* current, const DropdownOptions& o,
              const std::vector<std::string>& icons = {}) {
    const float w = itemWidth(o.width == 0.0f ? dp(240) : o.width, dp(240));
    const float h = dp(56);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    detail::FieldFrame f;
    f.p = ImGui::GetCursorScreenPos();
    f.w = w;
    f.h = h;
    f.outlined = o.outlined;
    const bool lead = hasIcon(o.leadingIcon);
    f.inputX = f.p.x + (lead ? dp(52) : dp(16));
    f.inputW = w - (f.inputX - f.p.x) - dp(52);

    const bool clicked = ImGui::InvisibleButton("##field", {w, h});
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered();
    if (clicked) ImGui::OpenPopup("##menu");
    const bool open = ImGui::IsPopupOpen("##menu");
    detail::drawFieldBackground(f, open, hov, dis);
    const bool has = *current >= 0 && *current < static_cast<int>(items.size());
    detail::drawFieldDecor(f, o.label, open, hov, has, o.error, dis, o.leadingIcon, open ? "arrow_drop_up" : "arrow_drop_down",
                           ImGui::GetID("##lbl"), false);
    if (has) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Scheme& s = S();
        float y = (o.label.empty() || o.outlined) ? f.p.y + h * 0.5f : f.p.y + dp(24) + dp(12);
        dl->PushClipRect({f.inputX, f.p.y}, {f.inputX + f.inputW, f.p.y + h}, true);
        drawTextV(dl, TextStyle::BodyLarge, f.inputX, y, dis ? disabledContent() : s.onSurface, items[*current]);
        dl->PopClipRect();
    }
    bool changed = false;
    if (beginAnchoredMenu("##menu", f.p, {f.p.x + w, f.p.y + h}, w)) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i);
            std::string_view ic = i < static_cast<int>(icons.size()) ? std::string_view{icons[i]} : std::string_view{};
            if (menuItem("##it", items[i], ic, {}, i == *current) && i != *current) { *current = i; changed = true; }
            if (i == *current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        endAnchoredMenu();
    }
    if (!o.supporting.empty()) {
        detail::drawSupporting({f.p.x, f.p.y + h}, w, o.supporting, o.error, dis);
        ImGui::Dummy({w, dp(20)});
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

/// Numeric field with decrement / increment icon buttons.
bool spinBox(const char* id, double* v, double vmin, double vmax, double step, int decimals, std::string_view label,
             float width = 0.0f, bool outlined = false) {
    const Scheme& s = S();
    const float w = itemWidth(width == 0.0f ? dp(200) : width, dp(200));
    const float h = dp(56);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    detail::FieldFrame f;
    f.p = ImGui::GetCursorScreenPos();
    f.w = w;
    f.h = h;
    f.outlined = outlined;
    f.inputX = f.p.x + dp(16);
    f.inputW = w - dp(16) - dp(96);
    bool changed = false;
    detail::drawFieldBackground(f, false, ImGui::IsMouseHoveringRect(f.p, {f.p.x + w, f.p.y + h}), detail::scopeDisabled());

    pushFont(TextStyle::BodyLarge);
    const float lineH = ImGui::GetFontSize();
    const float inputY = (!label.empty() && !outlined) ? dp(24) + (dp(24) - lineH) * 0.5f : (h - lineH) * 0.5f;
    ImGui::SetCursorScreenPos({f.inputX, f.p.y + inputY});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::SetNextItemWidth(f.inputW);
    char fmt[16];
    std::snprintf(fmt, sizeof fmt, "%%.%df", std::max(0, decimals));
    if (ImGui::InputScalar("##value", ImGuiDataType_Double, v, nullptr, nullptr, fmt)) changed = true;
    const bool active = ImGui::IsItemActive();
    const bool hov = ImGui::IsItemHovered();
    const bool dis = itemDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopFont();

    detail::drawFieldDecor(f, label, active, hov, true, false, dis, {}, {}, ImGui::GetID("##lbl"), false);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::SetCursorScreenPos({f.p.x + w - dp(96) - dp(2), f.p.y + (h - dp(48)) * 0.5f});
    ImGui::BeginDisabled(*v <= vmin);
    if (iconButton("##dec", "remove")) { *v -= step; changed = true; }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 0);
    ImGui::BeginDisabled(*v >= vmax);
    if (iconButton("##inc", "add")) { *v += step; changed = true; }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    (void)s;
    if (changed) *v = std::clamp(*v, vmin, vmax);

    ImGui::SetCursorScreenPos(f.p);
    ImGui::Dummy({w, h});
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

/// Color swatch + hex, opening a picker popup. `c` is edited in place.
bool colorField(const char* id, Color* c, std::string_view label, bool alpha, float width = 0.0f) {
    const Scheme& s = S();
    const float h = dp(56);
    const std::string hex = toHex(*c);
    const float natural = dp(8) + dp(40) + dp(16) + std::max(measure(TextStyle::BodyLarge, hex).x, measure(TextStyle::BodySmall, label).x) + dp(16) + dp(24) + dp(16);
    const float w = itemWidth(width, natural);
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##swatch", {w, h});
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImGuiID iid = ImGui::GetItemID();
    if (clicked) ImGui::OpenPopup("##picker");
    const bool open = ImGui::IsPopupOpen("##picker");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(shape().medium);
    dl->AddRect(p, {p.x + w, p.y + h}, col(dis ? disabledContainer() : (open ? s.primary : s.outlineVariant)), rnd, open ? dp(2) : dp(1));
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.onSurface.withAlpha(layer)), rnd);
    const ImVec2 sw{p.x + dp(8), p.y + dp(8)};
    dl->AddRectFilled(sw, {sw.x + dp(40), sw.y + dp(40)}, col(c->withAlpha(1.0f)), dp(shape().small));
    dl->AddRect(sw, {sw.x + dp(40), sw.y + dp(40)}, col(s.outlineVariant), dp(shape().small), dp(1));
    const float tx = sw.x + dp(40) + dp(16);
    if (!label.empty()) {
        drawText(dl, TextStyle::BodySmall, {tx, p.y + dp(9)}, dis ? disabledContent() : s.onSurfaceVariant, label);
        drawText(dl, TextStyle::BodyLarge, {tx, p.y + dp(26)}, dis ? disabledContent() : s.onSurface, hex);
    } else {
        drawTextV(dl, TextStyle::BodyLarge, tx, p.y + h * 0.5f, dis ? disabledContent() : s.onSurface, hex);
    }
    drawIcon(dl, "palette", {p.x + w - dp(16) - dp(12), p.y + h * 0.5f}, 24.0f, dis ? disabledContent() : s.onSurfaceVariant);

    bool changed = false;
    const ImGuiID key = ImGui::GetID("##picker");
    if (open) detail::drawMenuShadow(key);
    ImGuiStorage* parent = ImGui::GetStateStorage();
    ImGui::SetNextWindowPos({p.x, p.y + h + dp(4)}, ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s.surfaceContainerHigh.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(16), dp(16)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(shape().large));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    const bool popup = ImGui::BeginPopup("##picker");
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    if (popup) {
        detail::storeMenuRect(parent, key);
        float v[4] = {c->r, c->g, c->b, c->a};
        ImGui::SetNextItemWidth(dp(240));
        ImGuiColorEditFlags fl = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                 ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoInputs;
        if (alpha) fl |= ImGuiColorEditFlags_AlphaBar;
        else fl |= ImGuiColorEditFlags_NoAlpha;
        if (ImGui::ColorPicker4("##pick", v, fl)) {
            *c = {v[0], v[1], v[2], alpha ? v[3] : 1.0f};
            changed = true;
        }
        ImGui::Dummy({0, dp(4)});
        std::string buf = toHex(*c);
        TextFieldOptions o;
        o.label = "Hex";
        o.outlined = true;
        o.width = dp(240);
        o.leadingIcon = "tag";
        auto r = textField("##hex", buf, o);
        if (r.changed)
            if (auto nc = parseHex(buf)) { *c = *nc; changed = true; }
        ImGui::EndPopup();
    } else {
        parent->SetFloat(key + 3, 0);
    }
    ImGui::PopID();
    return changed;
}

// ===================================================================== tabs & navigation

struct TabInfo {
    std::string label, icon;
};

/// Material tabs header. `current` 0-based. `secondary` = secondary tabs style.
bool tabs(const char* id, const std::vector<TabInfo>& items, int* current, bool secondary = false, float width = -1.0f) {
    if (items.empty()) return false;
    const Scheme& s = S();
    bool anyIcon = false;
    for (auto& t : items) anyIcon = anyIcon || hasIcon(t.icon);
    const bool stacked = anyIcon && !secondary;
    const float h = dp(stacked ? 64 : 48);
    const float w = itemWidth(width, dp(90) * static_cast<float>(items.size()));
    const float tw = w / static_cast<float>(items.size());
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    float indX0 = 0, indX1 = 0;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const ImVec2 a{p.x + tw * static_cast<float>(i), p.y};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", {tw, h}) && *current != i) { *current = i; changed = true; }
        const bool dis = itemDisabled();
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const ImGuiID tid = ImGui::GetItemID();
        ImGui::PopID();
        const bool sel = i == *current;
        Color fg = dis ? disabledContent() : (sel ? (secondary ? s.onSurface : s.primary) : s.onSurfaceVariant);
        float layer = dis ? 0.0f : stateLayer(tid, hov, act);
        if (layer > 0) dl->AddRectFilled(a, {a.x + tw, a.y + h}, col((sel ? s.primary : s.onSurface).withAlpha(layer)));
        const auto& t = items[i];
        ImVec2 ts = measure(TextStyle::TitleSmall, t.label);
        const bool ic = hasIcon(t.icon);
        float contentW = ts.x;
        if (stacked) {
            if (ic) drawIcon(dl, t.icon, {a.x + tw * 0.5f, a.y + dp(10) + dp(12)}, 24.0f, fg);
            drawText(dl, TextStyle::TitleSmall, {a.x + (tw - ts.x) * 0.5f, a.y + dp(38)}, fg, t.label);
        } else {
            const float iw = ic ? dp(24) + dp(8) : 0.0f;
            contentW = ts.x + iw;
            float x = a.x + (tw - contentW) * 0.5f;
            if (ic) drawIcon(dl, t.icon, {x + dp(12), a.y + h * 0.5f}, 24.0f, fg);
            drawText(dl, TextStyle::TitleSmall, {x + iw, a.y + (h - ts.y) * 0.5f}, fg, t.label);
        }
        if (sel) {
            contentW = std::max(contentW, dp(24));
            if (secondary) { indX0 = a.x; indX1 = a.x + tw; }
            else { indX0 = a.x + (tw - contentW) * 0.5f; indX1 = indX0 + contentW; }
        }
    }
    dl->AddLine({p.x, p.y + h - dp(0.5f)}, {p.x + w, p.y + h - dp(0.5f)}, col(s.surfaceVariant), dp(1));
    // Sliding indicator.
    const ImGuiID k = ImGui::GetID("##ind");
    float x0 = animate(k, indX0, 16.0f), x1 = animate(k + 1, indX1, 16.0f);
    if (x1 > x0) {
        float ih = secondary ? dp(2) : dp(3);
        dl->AddRectFilled({x0, p.y + h - ih}, {x1, p.y + h}, col(s.primary), secondary ? 0.0f : dp(3), ImDrawFlags_RoundCornersTop);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    return changed;
}

/// Navigation drawer item: 56dp pill with icon, label and optional badge text.
bool navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                    std::string_view badge = {}, float width = -1.0f) {
    const Scheme& s = S();
    const float h = dp(56);
    const float w = itemWidth(width, dp(280));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {w, h});
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImGuiID iid = ImGui::GetItemID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = h * 0.5f;
    float t = animate(iid + 3, selected ? 1.0f : 0.0f, 18.0f);
    if (t > 0.01f) {
        // Indicator grows from the center.
        float inset = (1.0f - t) * w * 0.15f;
        dl->AddRectFilled({p.x + inset, p.y}, {p.x + w - inset, p.y + h}, col(s.secondaryContainer.withAlpha(t)), rnd);
    }
    Color fg = dis ? disabledContent() : (selected ? s.onSecondaryContainer : s.onSurfaceVariant);
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddRectFilled(p, {p.x + w, p.y + h}, col((selected ? s.onSecondaryContainer : s.onSurface).withAlpha(layer)), rnd);
    float x = p.x + dp(16);
    const float cy = p.y + h * 0.5f;
    if (hasIcon(iconName)) {
        drawIcon(dl, iconName, {x + dp(12), cy}, 24.0f, fg);
        x += dp(24) + dp(12);
    }
    drawTextV(dl, TextStyle::LabelLarge, x, cy, selected ? s.onSecondaryContainer : (dis ? disabledContent() : s.onSurfaceVariant), label);
    if (!badge.empty()) {
        float bw = measure(TextStyle::LabelLarge, badge).x;
        drawTextV(dl, TextStyle::LabelLarge, p.x + w - dp(24) - bw, cy, fg, badge);
    }
    return clicked;
}

// ===================================================================== lists & surfaces

struct ListItemOptions {
    std::string_view headline, supporting, leadingIcon, trailingText, trailingIcon;
    bool selected = false;
    float width = -1.0f;
};

/// One- or two-line list item. Returns true when clicked.
bool listItem(const char* id, const ListItemOptions& o) {
    const Scheme& s = S();
    const bool two = !o.supporting.empty();
    const float h = dp(two ? 72 : 56);
    const float w = itemWidth(o.width, dp(320));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {w, h});
    const bool dis = itemDisabled();
    const ImGuiID iid = ImGui::GetItemID();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(shape().medium);
    float st = animate(iid + 3, o.selected ? 1.0f : 0.0f, 18.0f);
    if (st > 0.01f) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.secondaryContainer.withAlpha(st)), rnd);
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.onSurface.withAlpha(layer)), rnd);

    const float cy = p.y + h * 0.5f;
    float x = p.x + dp(16);
    Color fg = dis ? disabledContent() : (o.selected ? s.onSecondaryContainer : s.onSurface);
    Color sub = dis ? disabledContent() : s.onSurfaceVariant;
    if (hasIcon(o.leadingIcon)) {
        drawIcon(dl, o.leadingIcon, {x + dp(12), cy}, 24.0f, o.selected ? fg : sub);
        x += dp(24) + dp(16);
    }
    float right = p.x + w - dp(16);
    if (hasIcon(o.trailingIcon)) {
        drawIcon(dl, o.trailingIcon, {right - dp(12), cy}, 24.0f, sub);
        right -= dp(24) + dp(16);
    }
    if (!o.trailingText.empty()) {
        float tw = measure(TextStyle::LabelSmall, o.trailingText).x;
        drawTextV(dl, TextStyle::LabelSmall, right - tw, cy, sub, o.trailingText);
        right -= tw + dp(16);
    }
    dl->PushClipRect({x, p.y}, {right, p.y + h}, true);
    const float hh = measure(TextStyle::BodyLarge, "Ag").y, sh = measure(TextStyle::BodyMedium, "Ag").y;
    const float total = hh + (two ? sh : 0.0f);
    const float y = cy - total * 0.5f;
    drawText(dl, TextStyle::BodyLarge, {x, y}, fg, o.headline);
    if (two) drawText(dl, TextStyle::BodyMedium, {x, y + hh}, sub, o.supporting);
    dl->PopClipRect();
    return clicked;
}

/// Collapsible section header. Returns the (possibly toggled) open state.
bool expander(const char* id, std::string_view title, std::string_view supporting, bool* open, float width = -1.0f) {
    const Scheme& s = S();
    const bool two = !supporting.empty();
    const float h = dp(two ? 64 : 48);
    const float w = itemWidth(width, dp(320));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {w, h});
    if (clicked) *open = !*open;
    const bool dis = itemDisabled();
    const ImGuiID iid = ImGui::GetItemID();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(shape().medium);
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.onSurface.withAlpha(layer)), rnd);
    const float cy = p.y + h * 0.5f;
    const Color fg = dis ? disabledContent() : s.onSurface;
    if (two) {
        float th = measure(TextStyle::TitleMedium, title).y, sh = measure(TextStyle::BodyMedium, supporting).y;
        float y = cy - (th + sh) * 0.5f;
        drawText(dl, TextStyle::TitleMedium, {p.x + dp(16), y}, fg, title);
        drawText(dl, TextStyle::BodyMedium, {p.x + dp(16), y + th}, dis ? disabledContent() : s.onSurfaceVariant, supporting);
    } else {
        drawTextV(dl, TextStyle::TitleMedium, p.x + dp(16), cy, fg, title);
    }
    drawIcon(dl, *open ? "expand_less" : "expand_more", {p.x + w - dp(16) - dp(12), cy}, 24.0f, dis ? disabledContent() : s.onSurfaceVariant);
    return clicked;
}

enum class CardVariant { Elevated, Filled, Outlined };

CardVariant cardVariant(std::string_view n) {
    if (n == "filled") return CardVariant::Filled;
    if (n == "outlined") return CardVariant::Outlined;
    return CardVariant::Elevated;
}

namespace detail {
struct CardFrame {
    ImGuiID sizeKey;
};
inline std::vector<CardFrame> g_cards;
} // namespace detail

/// Begins a card container (auto-height unless `height` > 0). Pair with endCard().
void beginCard(const char* id, CardVariant v, float width = 0.0f, float padding = 16.0f, float height = 0.0f,
               std::optional<Color> color = std::nullopt, float rounding = -1.0f) {
    const Scheme& s = S();
    ImGuiID key = ImGui::GetID(id);
    const float rnd = rounding >= 0 ? dp(rounding) : dp(shape().medium);
    const float w = itemWidth(width == 0.0f ? -1.0f : width, dp(320));
    Color bg = color.value_or(v == CardVariant::Filled ? s.surfaceContainerHighest
                            : v == CardVariant::Outlined ? s.surface : s.surfaceContainerLow);
    if (v == CardVariant::Elevated) {
        float lastH = ImGui::GetStateStorage()->GetFloat(key, 0.0f);
        if (lastH > 0) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            drawShadow(ImGui::GetWindowDrawList(), p, {p.x + w, p.y + lastH}, rnd, 1.0f);
        }
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, s.outlineVariant.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rnd);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, v == CardVariant::Outlined ? dp(1) : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(padding), dp(padding)});
    ImGuiChildFlags cf = ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0) cf |= ImGuiChildFlags_AutoResizeY;
    if (v == CardVariant::Outlined) cf |= ImGuiChildFlags_Borders;
    ImGui::BeginChild(id, {w, height > 0 ? height : 0.0f}, cf, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    pushSurface(bg);
    detail::g_cards.push_back({key});
}

void endCard() {
    popSurface();
    float h = ImGui::GetWindowSize().y;
    ImGui::EndChild();
    if (!detail::g_cards.empty()) {
        ImGui::GetStateStorage()->SetFloat(detail::g_cards.back().sizeKey, h);
        detail::g_cards.pop_back();
    }
}

void divider(float insetStart = 0.0f, float insetEnd = 0.0f) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy({w, dp(1)});
    ImGui::GetWindowDrawList()->AddLine({p.x + dp(insetStart), p.y}, {p.x + w - dp(insetEnd), p.y}, col(S().outlineVariant), dp(1));
}

// ===================================================================== dialog

/// Opens/closes a Material dialog driven by `*open`. Returns true while the
/// contents should be rendered; then call endDialog().
bool beginDialog(const char* title, bool* open, float width = 0.0f, std::string_view heroIcon = {}) {
    const Scheme& s = S();
    if (*open && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s.surfaceContainerHigh.vec4());
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, s.scrim.withAlpha(0.32f).vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(shape().extraLarge));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(24), dp(24)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    const float minW = width > 0 ? width : dp(280), maxW = width > 0 ? width : dp(560);
    ImGui::SetNextWindowSizeConstraints({minW, 0}, {maxW, vp->Size.y * 0.9f});
    bool shown = ImGui::BeginPopupModal(title, nullptr,
                                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    if (!shown) { *open = false; return false; }
    if (!*open || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        *open = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return false;
    }
    pushSurface(s.surfaceContainerHigh);
    const bool hero = hasIcon(heroIcon);
    if (hero) {
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - dp(24)) * 0.5f);
        icon(heroIcon, 24.0f, s.secondary);
        ImGui::Dummy({0, dp(4)});
    }
    std::string_view t = visible(title);
    if (!t.empty()) {
        text(t, TextStyle::HeadlineSmall, s.onSurface, false, hero ? 0.5f : 0.0f);
        ImGui::Dummy({0, dp(8)});
    }
    return true;
}

void endDialog() {
    popSurface();
    ImGui::EndPopup();
}

// ===================================================================== snackbar

/// Draws a snackbar at the bottom of the viewport. `t` in [0, 1] is its
/// show animation. Returns true when the action was clicked.
bool snackbar(std::string_view message, std::string_view action, float t, bool closeIcon, bool* closed) {
    if (t <= 0.001f) return false;
    const Scheme& s = S();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ease = 1.0f - (1.0f - t) * (1.0f - t);
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - dp(16) + dp(24) * (1 - ease)},
                            ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowSizeConstraints({dp(344), 0}, {std::min(dp(672), vp->WorkSize.x - dp(32)), kHuge});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, s.inverseSurface.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(shape().extraSmall));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(16), dp(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {dp(8), 0});
    ImGui::Begin("##imlui_snackbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor();
    pushSurface(s.inverseSurface);
    bool clicked = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = dp(48);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float msgW = measure(TextStyle::BodyMedium, message).x;
    ImGui::Dummy({msgW, h});
    drawTextV(dl, TextStyle::BodyMedium, p.x, p.y + h * 0.5f, s.inverseOnSurface.withAlpha(ease), message);
    // Push the actions to the right edge of the (min-width) bar.
    float right = 0;
    if (!action.empty()) right += measure(TextStyle::LabelLarge, action).x + dp(24) + dp(8);
    if (closeIcon) right += dp(48);
    float avail = std::max(dp(344) - dp(32), ImGui::GetWindowWidth() - dp(32));
    float spacer = avail - msgW - right;
    if (spacer > 0) { ImGui::SameLine(0, 0); ImGui::Dummy({spacer, 1}); }
    if (!action.empty()) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (h - dp(40)) * 0.5f);
        std::string a{action};
        a += "###snack_action";
        // Text button in inversePrimary.
        ImVec2 bp = ImGui::GetCursorScreenPos();
        float bw = measure(TextStyle::LabelLarge, action).x + dp(24);
        clicked = ImGui::InvisibleButton(a.c_str(), {bw, dp(40)});
        float layer = stateLayer(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive());
        if (layer > 0) dl->AddRectFilled(bp, {bp.x + bw, bp.y + dp(40)}, col(s.inversePrimary.withAlpha(layer)), dp(20));
        drawTextV(dl, TextStyle::LabelLarge, bp.x + dp(12), bp.y + dp(20), s.inversePrimary.withAlpha(ease), action);
    }
    if (closeIcon) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY());
        if (iconButton("##snack_close", "close", IconButtonVariant::Standard, false, false, s.inverseOnSurface) && closed)
            *closed = true;
    }
    popSurface();
    ImGui::End();
    return clicked;
}

// ===================================================================== data

struct ChartOptions {
    float min = 0, max = 1;
    bool autoRange = true;
    bool bars = false;
    bool fill = true;
    float width = -1.0f, height = 0.0f;
    std::optional<Color> color;
    std::string_view title;
};

/// Line / bar chart with grid, gradient area and hover readout.
void chart(const char* id, const std::vector<float>& values, const ChartOptions& o) {
    const Scheme& s = S();
    const float w = itemWidth(o.width, dp(320));
    const float h = o.height > 0 ? o.height : dp(160);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {w, h});
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color c = o.color.value_or(s.primary);

    float mn = o.min, mx = o.max;
    if (o.autoRange && !values.empty()) {
        mn = *std::min_element(values.begin(), values.end());
        mx = *std::max_element(values.begin(), values.end());
        if (o.bars) mn = std::min(mn, 0.0f);
    }
    if (mx <= mn) mx = mn + 1;
    // "Nice" axis: step of 1, 2, 2.5 or 5 x 10^n, range snapped to whole steps.
    int divisions = 4;
    if (o.autoRange) {
        float raw = (mx - mn) / 4.0f;
        float mag = std::pow(10.0f, std::floor(std::log10(raw)));
        float norm = raw / mag;
        float step = (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5f ? 2.5f : norm <= 5 ? 5 : 10) * mag;
        mn = std::floor(mn / step) * step;
        mx = std::ceil(mx / step) * step;
        if (mx <= mn) mx = mn + step;
        divisions = std::max(1, static_cast<int>(std::lround((mx - mn) / step)));
    }

    const float gutter = dp(40), top = p.y + (o.title.empty() ? dp(8) : dp(32)), bottom = p.y + h - dp(8);
    const float x0 = p.x + gutter, x1 = p.x + w - dp(8);
    if (!o.title.empty()) drawText(dl, TextStyle::TitleSmall, {p.x, p.y}, s.onSurface, o.title);
    // grid + labels
    for (int i = 0; i <= divisions; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(divisions);
        float y = bottom - (bottom - top) * t;
        dl->AddLine({x0, y}, {x1, y}, col(s.outlineVariant.withAlpha(i == 0 ? 1.0f : 0.5f)), dp(1));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", std::round(static_cast<double>(mn + (mx - mn) * t) * 1000.0) / 1000.0);
        ImVec2 ts = measure(TextStyle::LabelSmall, buf);
        drawText(dl, TextStyle::LabelSmall, {x0 - dp(8) - ts.x, y - ts.y * 0.5f}, s.onSurfaceVariant, buf);
    }
    const int n = static_cast<int>(values.size());
    if (n == 0) return;
    auto px = [&](int i) {
        if (o.bars) return x0 + (x1 - x0) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        return n == 1 ? (x0 + x1) * 0.5f : x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(n - 1);
    };
    auto py = [&](float v) { return bottom - (bottom - top) * std::clamp((v - mn) / (mx - mn), 0.0f, 1.0f); };

    int hoverIdx = -1;
    if (hov) {
        float mxp = ImGui::GetMousePos().x;
        float best = kHuge;
        for (int i = 0; i < n; ++i)
            if (std::fabs(px(i) - mxp) < best) { best = std::fabs(px(i) - mxp); hoverIdx = i; }
    }

    if (o.bars) {
        float bw = std::max(dp(2), (x1 - x0) / static_cast<float>(n) * 0.6f);
        for (int i = 0; i < n; ++i) {
            float x = px(i);
            float y = py(values[i]), y0 = py(std::max(mn, 0.0f));
            Color bc = i == hoverIdx ? mix(c, s.onSurface, 0.15f) : c;
            dl->AddRectFilled({x - bw * 0.5f, std::min(y, y0)}, {x + bw * 0.5f, std::max(y, y0)}, col(bc), std::min(bw * 0.5f, dp(6)),
                              ImDrawFlags_RoundCornersTop);
        }
    } else {
        std::vector<ImVec2> pts(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) pts[static_cast<std::size_t>(i)] = {px(i), py(values[i])};
        if (o.fill && n >= 2) {
            // Vertical gradient under the line.
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            const ImU32 cTop = col(c.withAlpha(0.28f)), cBot = col(c.withAlpha(0.0f));
            for (int i = 0; i + 1 < n; ++i) {
                ImVec2 a = pts[static_cast<std::size_t>(i)], b = pts[static_cast<std::size_t>(i + 1)];
                dl->PrimReserve(6, 4);
                auto base = static_cast<unsigned>(dl->_VtxCurrentIdx);
                dl->PrimWriteVtx(a, uv, cTop);
                dl->PrimWriteVtx(b, uv, cTop);
                dl->PrimWriteVtx({b.x, bottom}, uv, cBot);
                dl->PrimWriteVtx({a.x, bottom}, uv, cBot);
                dl->PrimWriteIdx(static_cast<unsigned short>(base));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 1));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 2));
                dl->PrimWriteIdx(static_cast<unsigned short>(base));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 2));
                dl->PrimWriteIdx(static_cast<unsigned short>(base + 3));
            }
        }
        dl->AddPolyline(pts.data(), n, col(c), dp(2));
    }
    if (hoverIdx >= 0) {
        float x = px(hoverIdx), y = py(values[static_cast<std::size_t>(hoverIdx)]);
        if (!o.bars) {
            dl->AddLine({x, top}, {x, bottom}, col(s.outline.withAlpha(0.6f)), dp(1));
            dl->AddCircleFilled({x, y}, dp(5), col(c));
            dl->AddCircle({x, y}, dp(5), col(surfaceColor()), 0, dp(2));
        }
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.3g", static_cast<double>(values[static_cast<std::size_t>(hoverIdx)]));
        ImVec2 ts = measure(TextStyle::LabelMedium, buf);
        float bw = ts.x + dp(16), bh = dp(24);
        float bx = std::clamp(x - bw * 0.5f, p.x, p.x + w - bw), by = std::max(p.y, y - bh - dp(10));
        dl->AddRectFilled({bx, by}, {bx + bw, by + bh}, col(s.inverseSurface), dp(shape().extraSmall));
        drawText(dl, TextStyle::LabelMedium, {bx + dp(8), by + (bh - ts.y) * 0.5f}, s.inverseOnSurface, buf);
    }
}

/// Header and row helpers for Material data tables (inside BeginTable).
void tableHeaderCell(std::string_view text, float rowH) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = measure(TextStyle::TitleSmall, text);
    ImGui::Dummy({ts.x, rowH});
    drawText(ImGui::GetWindowDrawList(), TextStyle::TitleSmall, {p.x, p.y + (rowH - ts.y) * 0.5f}, S().onSurfaceVariant, text);
}
void tableCell(std::string_view text, float rowH, std::optional<Color> color = std::nullopt) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = measure(TextStyle::BodyMedium, text);
    ImGui::Dummy({ts.x, rowH});
    drawText(ImGui::GetWindowDrawList(), TextStyle::BodyMedium, {p.x, p.y + (rowH - ts.y) * 0.5f},
             itemDisabled() ? disabledContent() : color.value_or(S().onSurface), text);
}

} // namespace imlui::md
