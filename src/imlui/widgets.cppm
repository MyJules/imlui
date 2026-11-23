// Material 3 widgets implemented on top of ImGui primitives (InvisibleButton
// + draw list). They follow the current imlui::theme() and respect
// ImGui::BeginDisabled() via ImGui::GetColorU32 (which applies style.Alpha).
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
import imlui.theme;

namespace imlui::md {

// ------------------------------------------------------------- helpers
inline ImVec2 add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
inline ImVec2 sub(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
inline float dp(float v) { return v * dpiScale(); }
inline ImU32 col(Color c) { return ImGui::GetColorU32(c.vec4()); }
inline const Scheme& S() { return theme().scheme; }
inline constexpr float kPi = 3.14159265358979f;

/// End of the visible part of an ImGui label ("Text##id" -> "Text").
inline const char* labelEnd(const char* label) {
    std::string_view sv{label};
    auto pos = sv.find("##");
    return label + (pos == std::string_view::npos ? sv.size() : pos);
}

/// Smoothly moves a per-id stored value towards `target`.
inline float animate(ImGuiID id, float target, float speed = 14.0f) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    float dt = ImGui::GetIO().DeltaTime;
    *v += (target - *v) * std::min(1.0f, dt * speed);
    if (std::fabs(*v - target) < 0.001f) *v = target;
    return *v;
}

/// Hover/press state layer opacity, animated (Material: 8% hover, 10% press).
inline float stateLayer(ImGuiID id, bool hovered, bool active) {
    return animate(id + 0x5157, active ? 0.10f : (hovered ? 0.08f : 0.0f), 20.0f);
}

inline float itemWidth(float requested, float natural) {
    if (requested > 0) return requested;
    if (requested < 0) return std::max(1.0f, ImGui::GetContentRegionAvail().x + (requested < -1.0f ? requested : 0.0f));
    return natural;
}

inline void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float strength) {
    for (int i = 1; i <= 4; ++i) {
        float g = dp(static_cast<float>(i) * 0.75f);
        dl->AddRectFilled({a.x - g * 0.5f, a.y - g * 0.25f + dp(1)}, {b.x + g * 0.5f, b.y + g + dp(1)},
                          col(S().shadow.withAlpha(0.05f * strength)), rounding + g);
    }
}

inline void drawCheck(ImDrawList* dl, ImVec2 c, float s, ImU32 color, float thickness) {
    ImVec2 pts[3] = {{c.x - s * 0.35f, c.y + s * 0.02f}, {c.x - s * 0.1f, c.y + s * 0.27f}, {c.x + s * 0.38f, c.y - s * 0.25f}};
    dl->AddPolyline(pts, 3, color, ImDrawFlags_None, thickness);
}

struct FontScope {
    explicit FontScope(TextStyle s) { ImGui::PushFont(font(s)); }
    ~FontScope() { ImGui::PopFont(); }
};

/// Draws the label part of `label` to the right of an indicator and returns
/// the width it occupies (0 if no visible label).
inline float labelWidth(const char* label) {
    const char* end = labelEnd(label);
    if (end == label) return 0.0f;
    return ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label, end).x;
}

} // namespace imlui::md

export namespace imlui::md {

// ------------------------------------------------------------- text

void text(std::string_view s, TextStyle style, std::optional<Color> color = std::nullopt, bool wrap = false) {
    FontScope fs{style};
    if (color) ImGui::PushStyleColor(ImGuiCol_Text, color->vec4());
    if (wrap) ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(s.data(), s.data() + s.size());
    if (wrap) ImGui::PopTextWrapPos();
    if (color) ImGui::PopStyleColor();
}

void tooltip(std::string_view s) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, S().inverseSurface.vec4());
    ImGui::PushStyleColor(ImGuiCol_Text, S().inverseOnSurface.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(8), dp(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(theme().shape.extraSmall));
    ImGui::BeginTooltip();
    {
        FontScope fs{TextStyle::BodySmall};
        ImGui::TextUnformatted(s.data(), s.data() + s.size());
    }
    ImGui::EndTooltip();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ------------------------------------------------------------- buttons

enum class ButtonVariant { Filled, Tonal, Outlined, Text, Elevated, Fab };

ButtonVariant buttonVariant(std::string_view name) {
    if (name == "tonal") return ButtonVariant::Tonal;
    if (name == "outlined") return ButtonVariant::Outlined;
    if (name == "text") return ButtonVariant::Text;
    if (name == "elevated") return ButtonVariant::Elevated;
    if (name == "fab") return ButtonVariant::Fab;
    return ButtonVariant::Filled;
}

bool button(const char* label, ButtonVariant variant = ButtonVariant::Filled, ImVec2 size = {0, 0}) {
    FontScope fs{TextStyle::LabelLarge};
    const Scheme& s = S();
    const char* end = labelEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    const bool fab = variant == ButtonVariant::Fab;
    const float padX = dp(variant == ButtonVariant::Text ? 12 : (fab ? 20 : 24));
    const float h = size.y > 0 ? size.y : dp(fab ? 56 : 40);
    const float w = itemWidth(size.x, std::max(ts.x + padX * 2, fab ? dp(56) : dp(48)));
    const float rounding = fab ? dp(theme().shape.large) : h * 0.5f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    ImGuiID id = ImGui::GetID(label);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    Color bg{0, 0, 0, 0}, fg = s.primary;
    bool border = false, shadow = false;
    switch (variant) {
    case ButtonVariant::Filled:   bg = s.primary; fg = s.onPrimary; break;
    case ButtonVariant::Tonal:    bg = s.secondaryContainer; fg = s.onSecondaryContainer; break;
    case ButtonVariant::Outlined: border = true; break;
    case ButtonVariant::Text:     break;
    case ButtonVariant::Elevated: bg = s.surfaceContainerLow; shadow = true; break;
    case ButtonVariant::Fab:      bg = s.primaryContainer; fg = s.onPrimaryContainer; shadow = true; break;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q = add(p, {w, h});
    float layer = stateLayer(id, hov, act);
    if (shadow) drawShadow(dl, p, q, rounding, 1.0f + layer * 6.0f);
    if (bg.a > 0) dl->AddRectFilled(p, q, col(bg), rounding);
    if (layer > 0) dl->AddRectFilled(p, q, col(fg.withAlpha(layer)), rounding);
    if (border) dl->AddRect(p, q, col(s.outline), rounding, ImDrawFlags_None, dp(1));
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f}, col(fg), label, end);
    return clicked;
}

// ------------------------------------------------------------- selection controls

bool checkbox(const char* label, bool* v) {
    const Scheme& s = S();
    const float box = dp(18), h = std::max(ImGui::GetFrameHeight(), dp(32));
    const float w = h + labelWidth(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    if (clicked) *v = !*v;
    ImGuiID id = ImGui::GetID(label);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c{p.x + h * 0.5f, p.y + h * 0.5f};
    float layer = stateLayer(id, hov, act);
    if (layer > 0) dl->AddCircleFilled(c, h * 0.5f, col((*v ? s.primary : s.onSurface).withAlpha(layer)));
    ImVec2 a{c.x - box * 0.5f, c.y - box * 0.5f}, b{c.x + box * 0.5f, c.y + box * 0.5f};
    float t = animate(id + 1, *v ? 1.0f : 0.0f);
    if (t > 0.01f) {
        dl->AddRectFilled(a, b, col(s.primary.withAlpha(t)), dp(2));
        drawCheck(dl, c, box * 0.8f, col(s.onPrimary.withAlpha(t)), dp(2));
    }
    if (t < 0.99f) dl->AddRect(a, b, col(s.onSurfaceVariant.withAlpha(1 - t)), dp(2), ImDrawFlags_None, dp(2));

    const char* end = labelEnd(label);
    if (end != label) {
        ImVec2 ts = ImGui::CalcTextSize(label, end);
        dl->AddText({p.x + h + ImGui::GetStyle().ItemInnerSpacing.x, p.y + (h - ts.y) * 0.5f}, col(s.onSurface), label, end);
    }
    return clicked;
}

bool radio(const char* label, bool selected) {
    const Scheme& s = S();
    const float h = std::max(ImGui::GetFrameHeight(), dp(32));
    const float w = h + labelWidth(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    ImGuiID id = ImGui::GetID(label);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c{p.x + h * 0.5f, p.y + h * 0.5f};
    float layer = stateLayer(id, hov, act);
    if (layer > 0) dl->AddCircleFilled(c, h * 0.5f, col((selected ? s.primary : s.onSurface).withAlpha(layer)));
    Color ring = selected ? s.primary : s.onSurfaceVariant;
    dl->AddCircle(c, dp(9), col(ring), 0, dp(2));
    float t = animate(id + 1, selected ? 1.0f : 0.0f, 18.0f);
    if (t > 0.01f) dl->AddCircleFilled(c, dp(5) * t, col(s.primary));

    const char* end = labelEnd(label);
    if (end != label) {
        ImVec2 ts = ImGui::CalcTextSize(label, end);
        dl->AddText({p.x + h + ImGui::GetStyle().ItemInnerSpacing.x, p.y + (h - ts.y) * 0.5f}, col(s.onSurface), label, end);
    }
    return clicked;
}

bool switchToggle(const char* label, bool* v) {
    const Scheme& s = S();
    const float tw = dp(52), th = dp(32);
    const float h = std::max(ImGui::GetFrameHeight(), th);
    const float w = tw + labelWidth(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    if (clicked) *v = !*v;
    ImGuiID id = ImGui::GetID(label);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a{p.x, p.y + (h - th) * 0.5f}, b{a.x + tw, a.y + th};
    float t = animate(id + 1, *v ? 1.0f : 0.0f, 16.0f);

    Color track = mix(s.surfaceContainerHighest, s.primary, t);
    dl->AddRectFilled(a, b, col(track), th * 0.5f);
    if (t < 0.99f) dl->AddRect(a, b, col(s.outline.withAlpha(1 - t)), th * 0.5f, ImDrawFlags_None, dp(2));

    float r = (act ? dp(14) : dp(8) + dp(4) * t);
    ImVec2 c{a.x + th * 0.5f + (tw - th) * t, a.y + th * 0.5f};
    float layer = stateLayer(id, hov, act);
    if (layer > 0) dl->AddCircleFilled(c, dp(20), col((*v ? s.primary : s.onSurface).withAlpha(layer)));
    Color thumb = mix(s.outline, s.onPrimary, t);
    if (hov && !*v) thumb = s.onSurfaceVariant;
    if (hov && *v) thumb = s.primaryContainer;
    dl->AddCircleFilled(c, r, col(thumb));

    const char* end = labelEnd(label);
    if (end != label) {
        ImVec2 ts = ImGui::CalcTextSize(label, end);
        dl->AddText({p.x + tw + ImGui::GetStyle().ItemInnerSpacing.x, p.y + (h - ts.y) * 0.5f}, col(s.onSurface), label, end);
    }
    return clicked;
}

// ------------------------------------------------------------- slider

/// Material slider. `step` <= 0 means continuous. Returns true while changed.
bool slider(const char* id, float* v, float vmin, float vmax, float step = 0.0f, float width = 0.0f,
            const char* valueFormat = "%.2f") {
    const Scheme& s = S();
    const float w = itemWidth(width == 0.0f ? -1.0f : width, dp(200));
    const float h = dp(40);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {w, h});
    ImGuiID iid = ImGui::GetID(id);
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

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f, th = dp(4);
    const float hx = x0 + (x1 - x0) * tv;
    dl->AddRectFilled({x0, cy - th * 0.5f}, {x1, cy + th * 0.5f}, col(s.secondaryContainer), th);
    dl->AddRectFilled({x0, cy - th * 0.5f}, {hx, cy + th * 0.5f}, col(s.primary), th);
    if (step > 0 && vmax > vmin && (vmax - vmin) / step <= 40) {
        for (float sv = vmin; sv <= vmax + step * 0.5f; sv += step) {
            float sx = x0 + (x1 - x0) * ((sv - vmin) / (vmax - vmin));
            dl->AddCircleFilled({sx, cy}, dp(1), col(sx <= hx ? s.onPrimary : s.onSurfaceVariant));
        }
    }
    float layer = stateLayer(iid, hov, act);
    if (layer > 0) dl->AddCircleFilled({hx, cy}, dp(20), col(s.primary.withAlpha(layer * 1.5f)));
    dl->AddCircleFilled({hx, cy}, r, col(s.primary));

    if (act && valueFormat && *valueFormat) {
        char buf[64];
        std::snprintf(buf, sizeof buf, valueFormat, static_cast<double>(*v));
        FontScope fs{TextStyle::LabelMedium};
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float bw = std::max(ts.x + dp(16), dp(28)), bh = dp(28);
        ImVec2 ba{hx - bw * 0.5f, cy - r - dp(8) - bh};
        fg->AddRectFilled(ba, {ba.x + bw, ba.y + bh}, col(s.inverseSurface), bh * 0.5f);
        fg->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {hx - ts.x * 0.5f, ba.y + (bh - ts.y) * 0.5f}, col(s.inverseOnSurface), buf);
    }
    return changed;
}

// ------------------------------------------------------------- progress

/// Linear progress. `fraction` < 0 draws the indeterminate animation.
void progress(float fraction, float width = -1.0f) {
    const Scheme& s = S();
    const float w = itemWidth(width, dp(200)), h = dp(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({w, h + dp(8)});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = p.y + dp(4);
    dl->AddRectFilled({p.x, y}, {p.x + w, y + h}, col(s.secondaryContainer), h);
    if (fraction >= 0.0f) {
        dl->AddRectFilled({p.x, y}, {p.x + w * std::clamp(fraction, 0.0f, 1.0f), y + h}, col(s.primary), h);
    } else {
        float t = static_cast<float>(std::fmod(ImGui::GetTime() * 0.8, 1.0));
        float a = std::clamp(t * 1.6f - 0.4f, 0.0f, 1.0f), b = std::clamp(t * 1.6f, 0.0f, 1.0f);
        dl->AddRectFilled({p.x + w * a, y}, {p.x + w * b, y + h}, col(s.primary), h);
    }
}

/// Circular indeterminate progress (Material "loading indicator").
void spinner(float diameter = 0.0f) {
    const Scheme& s = S();
    const float d = diameter > 0 ? diameter : dp(40);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({d, d});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = static_cast<float>(ImGui::GetTime());
    float a0 = t * 4.5f;
    float span = 0.6f + 0.5f * (1.0f + std::sin(t * 2.5f)) * kPi * 0.6f;
    dl->PathArcTo({p.x + d * 0.5f, p.y + d * 0.5f}, d * 0.5f - dp(3), a0, a0 + span, 32);
    dl->PathStroke(col(s.primary), ImDrawFlags_None, dp(4));
}

// ------------------------------------------------------------- chips & segmented

bool chip(const char* label, bool selected) {
    FontScope fs{TextStyle::LabelLarge};
    const Scheme& s = S();
    const char* end = labelEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float h = dp(32), icon = selected ? dp(18) + dp(8) : 0.0f;
    const float w = ts.x + dp(32) + icon;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, {w, h});
    ImGuiID id = ImGui::GetID(label);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q = add(p, {w, h});
    const float rnd = dp(theme().shape.small);
    Color fg = selected ? s.onSecondaryContainer : s.onSurfaceVariant;
    if (selected) dl->AddRectFilled(p, q, col(s.secondaryContainer), rnd);
    else dl->AddRect(p, q, col(s.outline), rnd, ImDrawFlags_None, dp(1));
    float layer = stateLayer(id, hov, act);
    if (layer > 0) dl->AddRectFilled(p, q, col(fg.withAlpha(layer)), rnd);
    float x = p.x + dp(16);
    if (selected) {
        drawCheck(dl, {p.x + dp(8) + dp(9), p.y + h * 0.5f}, dp(16), col(fg), dp(2));
        x = p.x + dp(8) + icon;
    }
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {x, p.y + (h - ts.y) * 0.5f}, col(fg), label, end);
    return clicked;
}

/// Segmented button: one of `items` selected. Returns true when changed.
bool segmented(const char* id, const std::vector<std::string>& items, int* current, float width = 0.0f) {
    if (items.empty()) return false;
    FontScope fs{TextStyle::LabelLarge};
    const Scheme& s = S();
    const float h = dp(40);
    float natural = 0;
    for (auto& it : items) natural = std::max(natural, ImGui::CalcTextSize(it.c_str()).x + dp(48) + dp(18));
    const float w = itemWidth(width, natural * static_cast<float>(items.size()));
    const float segW = w / static_cast<float>(items.size());
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    const int n = static_cast<int>(items.size());
    for (int i = 0; i < n; ++i) {
        ImVec2 a{p.x + segW * static_cast<float>(i), p.y}, b{a.x + segW, p.y + h};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", {segW, h}) && *current != i) { *current = i; changed = true; }
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        ImGuiID sid = ImGui::GetID("##seg");
        ImGui::PopID();
        ImDrawFlags corners = n == 1 ? ImDrawFlags_RoundCornersAll
                            : i == 0 ? ImDrawFlags_RoundCornersLeft
                            : i == n - 1 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
        bool sel = *current == i;
        Color fg = sel ? s.onSecondaryContainer : s.onSurface;
        if (sel) dl->AddRectFilled(a, b, col(s.secondaryContainer), h * 0.5f, corners);
        float layer = stateLayer(sid, hov, act);
        if (layer > 0) dl->AddRectFilled(a, b, col(fg.withAlpha(layer)), h * 0.5f, corners);
        ImVec2 ts = ImGui::CalcTextSize(items[i].c_str());
        float iconW = sel ? dp(18) + dp(8) : 0.0f;
        float x = a.x + (segW - ts.x - iconW) * 0.5f;
        if (sel) drawCheck(dl, {x + dp(9), p.y + h * 0.5f}, dp(16), col(fg), dp(2));
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {x + iconW, p.y + (h - ts.y) * 0.5f}, col(fg), items[i].c_str());
        if (i > 0) dl->AddLine({a.x, a.y}, {a.x, b.y}, col(s.outline), dp(1));
    }
    dl->AddRect(p, {p.x + w, p.y + h}, col(s.outline), h * 0.5f, ImDrawFlags_None, dp(1));
    ImGui::PopID();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    return changed;
}

// ------------------------------------------------------------- text field

namespace detail {
inline int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}
} // namespace detail

struct TextFieldResult {
    bool changed = false;
    bool accepted = false; ///< Enter pressed (single line)
};

/// Material "filled" text field with a floating label.
TextFieldResult textField(const char* id, std::string& value, const char* label, const char* placeholder,
                          bool password, bool multiline, float width, float height, bool readOnly = false) {
    const Scheme& s = S();
    TextFieldResult res;
    const float w = itemWidth(width == 0.0f ? -1.0f : width, dp(280));
    const float h = multiline ? (height > 0 ? height : dp(120)) : dp(56);
    const bool hasLabel = label && *label;

    ImGui::PushID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID focusKey = ImGui::GetID("##focused");
    bool focused = st->GetBool(focusKey, false);

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(theme().shape.extraSmall);
    ImGui::BeginGroup();
    dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.surfaceContainerHighest), rnd, ImDrawFlags_RoundCornersTop);

    const float padX = dp(16);
    const bool floating = focused || !value.empty() || !hasLabel;
    float labelT = animate(ImGui::GetID("##lbl"), floating ? 1.0f : 0.0f, 18.0f);
    float inputY = hasLabel ? dp(24) : (multiline ? dp(12) : (h - ImGui::GetFontSize()) * 0.5f);

    ImGui::SetCursorScreenPos({p.x + padX, p.y + inputY});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (password) flags |= ImGuiInputTextFlags_Password;
    if (readOnly) flags |= ImGuiInputTextFlags_ReadOnly;
    const float innerW = w - padX * 2;
    if (multiline) {
        res.changed = ImGui::InputTextMultiline("##input", value.data(), value.capacity() + 1,
                                                {innerW, h - inputY - dp(8)}, flags, detail::resizeCallback, &value);
    } else {
        flags |= ImGuiInputTextFlags_EnterReturnsTrue;
        ImGui::SetNextItemWidth(innerW);
        const char* hint = (placeholder && (!hasLabel || focused)) ? placeholder : "";
        res.accepted = ImGui::InputTextWithHint("##input", hint, value.data(), value.capacity() + 1, flags,
                                                detail::resizeCallback, &value);
        res.changed = ImGui::IsItemEdited();
    }
    bool active = ImGui::IsItemActive();
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    st->SetBool(focusKey, active);

    if (hovered && !active) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.onSurface.withAlpha(0.06f)), rnd, ImDrawFlags_RoundCornersTop);
    if (hasLabel) {
        const char* lend = labelEnd(label);
        Color lc = active ? s.primary : s.onSurfaceVariant;
        ImFont* small = font(TextStyle::BodySmall);
        ImFont* big = font(TextStyle::BodyLarge);
        float scale = ImGui::GetIO().FontGlobalScale;
        float size = (small->FontSize + (big->FontSize - small->FontSize) * (1 - labelT)) * scale;
        float y0 = multiline ? p.y + dp(14) : p.y + (h - big->FontSize * scale) * 0.5f;
        float y = y0 + (p.y + dp(8) - y0) * labelT;
        dl->AddText(labelT > 0.5f ? small : big, size, {p.x + padX, y}, col(lc), label, lend);
    }
    float lineH = active ? dp(2) : dp(1);
    dl->AddRectFilled({p.x, p.y + h - lineH}, {p.x + w, p.y + h}, col(active ? s.primary : s.onSurfaceVariant));

    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    ImGui::EndGroup();
    ImGui::PopID();
    return res;
}

// ------------------------------------------------------------- surfaces

enum class CardVariant { Elevated, Filled, Outlined };

CardVariant cardVariant(std::string_view n) {
    if (n == "filled") return CardVariant::Filled;
    if (n == "outlined") return CardVariant::Outlined;
    return CardVariant::Elevated;
}

namespace detail {
struct CardFrame { ImGuiID sizeKey; };
inline std::vector<CardFrame> g_cards;
} // namespace detail

/// Begins a card container (auto-height). Always pair with endCard().
void beginCard(const char* id, CardVariant v, float width = 0.0f, float padding = 16.0f, float height = 0.0f) {
    const Scheme& s = S();
    ImGuiID key = ImGui::GetID(id);
    const float rnd = dp(theme().shape.medium);
    const float w = itemWidth(width == 0.0f ? -1.0f : width, dp(320));
    Color bg = v == CardVariant::Filled ? s.surfaceContainerHighest
             : v == CardVariant::Outlined ? s.surface : s.surfaceContainerLow;
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
    detail::g_cards.push_back({key});
}

void endCard() {
    float h = ImGui::GetWindowSize().y;
    ImGui::EndChild();
    if (!detail::g_cards.empty()) {
        ImGui::GetStateStorage()->SetFloat(detail::g_cards.back().sizeKey, h);
        detail::g_cards.pop_back();
    }
}

/// One- or two-line list item. Returns true when clicked.
bool listItem(const char* id, std::string_view headline, std::string_view supporting, bool selected, float width = -1.0f) {
    const Scheme& s = S();
    const bool two = !supporting.empty();
    const float h = dp(two ? 72 : 56);
    const float w = itemWidth(width, dp(320));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {w, h});
    ImGuiID iid = ImGui::GetID(id);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(theme().shape.large);
    if (selected) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.secondaryContainer), rnd);
    float layer = stateLayer(iid, hov, act);
    if (layer > 0) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.onSurface.withAlpha(layer)), rnd);
    ImFont* hf = font(TextStyle::BodyLarge);
    ImFont* sf = font(TextStyle::BodyMedium);
    float scale = ImGui::GetIO().FontGlobalScale;
    float hs = hf->FontSize * scale, ss = sf->FontSize * scale;
    float total = hs + (two ? ss + dp(2) : 0);
    float y = p.y + (h - total) * 0.5f;
    Color fg = selected ? s.onSecondaryContainer : s.onSurface;
    dl->AddText(hf, hs, {p.x + dp(16), y}, col(fg), headline.data(), headline.data() + headline.size());
    if (two)
        dl->AddText(sf, ss, {p.x + dp(16), y + hs + dp(2)}, col(s.onSurfaceVariant), supporting.data(),
                    supporting.data() + supporting.size());
    return clicked;
}

// ------------------------------------------------------------- dialog

/// Opens/closes a Material dialog driven by `*open`. Returns true while the
/// dialog's contents should be rendered; then call endDialog().
bool beginDialog(const char* title, bool* open, float width = 0.0f) {
    const Scheme& s = S();
    if (*open && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s.surfaceContainerHigh.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(theme().shape.extraLarge));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(24), dp(24)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({width > 0 ? width : dp(280), 0}, {width > 0 ? width : dp(560), vp->Size.y * 0.9f});
    bool visible = ImGui::BeginPopupModal(title, nullptr,
                                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    if (!visible) { *open = false; return false; }
    if (!*open) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return false; }
    const char* end = labelEnd(title);
    if (end != title) {
        text(std::string_view{title, static_cast<std::size_t>(end - title)}, TextStyle::HeadlineSmall);
        ImGui::Dummy({0, dp(8)});
    }
    return true;
}

void endDialog() { ImGui::EndPopup(); }

} // namespace imlui::md
