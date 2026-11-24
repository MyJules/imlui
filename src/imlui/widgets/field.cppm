// Shared plumbing of text-field-like controls (text field, dropdown, spin box):
// container, floating label, icons, indicator and supporting text.
export module imlui.widgets.field;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;

export namespace imlui::md::field {

/// Geometry of a field container.
struct FieldFrame {
    ImVec2 p;          ///< top-left of the container
    float w = 0, h = 0;
    float inputX = 0;  ///< where content starts
    float inputW = 0;  ///< content width
    bool outlined = false;
};

/// Draws the container, label, icons and indicator of a text-field-like
/// control. `focused` drives the active colors, `filled` = has content.
void drawFieldDecor(const FieldFrame& f, std::string_view label, bool focused, bool hovered, bool filled, bool error,
                    bool dis, std::string_view leadingIcon, std::string_view trailingIcon, ImGuiID animId,
                    bool multiline);

/// Container of a filled field. Drawn *before* the input so text stays on top.
void drawFieldBackground(const FieldFrame& f, bool focused, bool hovered, bool dis);

/// Supporting / error text below a field of width `w` whose bottom-left is `at`.
void drawSupporting(ImVec2 at, float w, std::string_view text, bool error, bool dis);

/// True inside ImGui::BeginDisabled() (usable before the item is submitted): the disabled item flag, not the style
/// alpha, which a theme may lower (Alpha) or leave at 1 when disabled (DisabledAlpha).
[[nodiscard]] bool scopeDisabled();

/// True when the mouse is over the field's container in its window or a child of it (a multiline input), and not
/// under a popup or another window, nor while another item is held: the field's hover before its input is submitted.
[[nodiscard]] bool containerHovered(const FieldFrame& f);

/// ImGui InputText resize callback for a `std::string*` passed as user data.
int inputResizeCallback(ImGuiInputTextCallbackData* data);

} // namespace imlui::md::field

module :private;

namespace imlui::md::field {

void drawFieldDecor(const FieldFrame& f, std::string_view label, bool focused, bool hovered, bool filled, bool error,
                    bool dis, std::string_view leadingIcon, std::string_view trailingIcon, ImGuiID animId,
                    bool multiline) {
    const Scheme& s = scheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = f.p, q{p.x + f.w, p.y + f.h};
    const float rnd = dp(shape().extraSmall);
    const Color accent = error ? s.error : s.primary;
    const float labelT = animate(animId, (focused || filled) ? 1.0f : 0.0f, 18.0f);

    if (!f.outlined) {
        float lineH = focused ? dp(2) : dp(1);
        Color lc = dis ? s.onSurface.withAlpha(0.38f)
                       : (focused || error ? accent : (hovered ? s.onSurface : s.onSurfaceVariant));
        fillRoundRect(dl, {p.x, q.y - lineH}, q, 0.0f, lc);
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
        float tw = fnt->CalcTextSizeA(size, hugeExtent, 0.0f, label.data(), label.data() + label.size()).x;
        if (f.outlined && labelT > 0.0f) {
            labelGapX0 = x - dp(4);
            labelGapX1 = x + tw + dp(4);
        }
        dl->AddText(fnt, size, {x, y}, col(lc), label.data(), label.data() + label.size());
    }

    if (f.outlined) {
        Color bc = dis ? s.onSurface.withAlpha(0.12f)
                       : (error ? s.error : (focused ? s.primary : (hovered ? s.onSurface : s.outline)));
        float th = focused ? dp(2) : dp(1);
        if (labelGapX1 > labelGapX0) {
            // Border with a notch where the floating label sits.
            dl->PushClipRect({p.x - dp(4), p.y - dp(4)}, {labelGapX0, q.y + dp(4)}, true);
            strokeRoundRect(dl, p, q, rnd, bc, th);
            dl->PopClipRect();
            dl->PushClipRect({labelGapX1, p.y - dp(4)}, {q.x + dp(4), q.y + dp(4)}, true);
            strokeRoundRect(dl, p, q, rnd, bc, th);
            dl->PopClipRect();
            dl->PushClipRect({labelGapX0, p.y + dp(6)}, {labelGapX1, q.y + dp(4)}, true);
            strokeRoundRect(dl, p, q, rnd, bc, th);
            dl->PopClipRect();
        } else {
            strokeRoundRect(dl, p, q, rnd, bc, th);
        }
    }

    const Color ic = dis ? s.onSurface.withAlpha(0.38f) : s.onSurfaceVariant;
    if (hasIcon(leadingIcon))
        drawIcon(dl, leadingIcon, {p.x + dp(12) + dp(12), p.y + (multiline ? dp(28) : f.h * 0.5f)}, 24.0f, ic);
    if (hasIcon(trailingIcon))
        drawIcon(dl, trailingIcon, {q.x - dp(12) - dp(12), p.y + (multiline ? dp(28) : f.h * 0.5f)}, 24.0f,
                 error && !dis ? s.error : ic);
}

void drawFieldBackground(const FieldFrame& f, bool focused, bool hovered, bool dis) {
    if (f.outlined) return;
    const Scheme& s = scheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q{f.p.x + f.w, f.p.y + f.h};
    const float rnd = dp(shape().extraSmall);
    fillRoundRect(dl, f.p, q, rnd, dis ? s.onSurface.withAlpha(0.04f) : s.surfaceContainerHighest,
                  ImDrawFlags_RoundCornersTop);
    if (hovered && !focused && !dis)
        fillRoundRect(dl, f.p, q, rnd, s.onSurface.withAlpha(0.08f), ImDrawFlags_RoundCornersTop);
}

void drawSupporting(ImVec2 at, float w, std::string_view text, bool error, bool dis) {
    if (text.empty()) return;
    const Scheme& s = scheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Color c = dis ? s.onSurface.withAlpha(0.38f) : (error ? s.error : s.onSurfaceVariant);
    dl->PushClipRect(at, {at.x + w, at.y + dp(20)}, true);
    drawText(dl, TextStyle::BodySmall, {at.x + dp(16), at.y + dp(4)}, c, text);
    dl->PopClipRect();
}

bool scopeDisabled() { return (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0; }

bool containerHovered(const FieldFrame& f) {
    // Child windows (a multiline input) count, but not popups opened from this window (a menu over the field).
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy) &&
           ImGui::IsMouseHoveringRect(f.p, {f.p.x + f.w, f.p.y + f.h});
}

int inputResizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

} // namespace imlui::md::field
