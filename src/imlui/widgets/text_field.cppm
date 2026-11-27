// Material text field (filled or outlined) with floating label, icons and supporting text.
export module imlui.widgets.text_field;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;
import imlui.widgets.field;
import imlui.ui.frame_request;

export namespace imlui::md {

struct TextFieldOptions {
    std::string_view label, placeholder, leadingIcon, trailingIcon, supporting;
    bool outlined = false, password = false, multiline = false, readOnly = false, error = false;
    /// resolveSize values (itemWidth): a width of 0 is the natural 280dp, -1 fills (the TextField element passes -1
    /// when its width is unset).
    float width = 0, height = 0;
};

struct TextFieldResult {
    bool changed = false;
    bool accepted = false;         ///< Enter pressed (single line)
    bool committed = false;        ///< the input lost focus (Enter, Tab, a click elsewhere) after an edit
    bool trailingClicked = false;  ///< trailing icon clicked
};

/// Material text field (filled or outlined) with floating label, icons and supporting text.
TextFieldResult textField(const char* id, std::string& value, const TextFieldOptions& o);
/// The size of textField() with the same options (`o.width` resolved like itemWidth()).
[[nodiscard]] ImVec2 textFieldSize(const TextFieldOptions& o);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

/// A text field's measurements.
struct TextFieldLayout {
    float w = 0.0f;
    float h = 0.0f;
    float lineH = 0.0f;  ///< the input's line (bodyLarge)
    float inputY = 0.0f; ///< the input's top, from the container's
    bool trail = false;  ///< a trailing icon button (also a password's visibility toggle)
    ImVec2 size;         ///< the whole: the container, what overhangs it (a tall font, a short multiline field), the
                         ///< supporting text below
};

/// The trailing icon of a field with options `o`: its own, else a password's visibility toggle.
std::string_view trailingIcon(const TextFieldOptions& o, bool reveal) {
    if (o.password && o.trailingIcon.empty()) return reveal ? "visibility_off" : "visibility";
    return o.trailingIcon;
}

TextFieldLayout textFieldLayout(const TextFieldOptions& o) {
    TextFieldLayout l;
    l.w = itemWidth(o.width, dp(280));
    l.h = o.multiline ? (o.height > 0 ? o.height : dp(120)) : dp(56);
    l.trail = hasIcon(trailingIcon(o, false));
    l.lineH = linePixels(TextStyle::BodyLarge); // GetFontSize() inside pushFont(BodyLarge), without the push
    const bool hasLabel = !o.label.empty();
    if (o.multiline) l.inputY = (hasLabel && !o.outlined) ? dp(24) : dp(16);
    else if (hasLabel && !o.outlined) l.inputY = dp(24) + (dp(24) - l.lineH) * 0.5f;
    else l.inputY = (l.h - l.lineH) * 0.5f;
    // The items textField() submits: the input (at least a line), the trailing button, the container's dummy and
    // the supporting text's below it.
    const float input = o.multiline ? l.inputY + std::max(l.h - l.inputY - dp(8), l.lineH) : l.inputY + l.lineH;
    const float button = l.trail ? (o.multiline ? dp(4) : (l.h - dp(48)) * 0.5f) + dp(48) : 0.0f;
    l.size = {l.w, std::max({l.h, input, button})};
    if (!o.supporting.empty()) l.size.y = std::max(l.size.y, l.h + dp(20));
    return l;
}

} // namespace

ImVec2 textFieldSize(const TextFieldOptions& o) { return textFieldLayout(o).size; }

TextFieldResult textField(const char* id, std::string& value, const TextFieldOptions& o) {
    const Scheme& s = scheme();
    TextFieldResult res;
    const TextFieldLayout l = textFieldLayout(o);
    const float w = l.w, h = l.h;
    const bool hasLabel = !o.label.empty();
    const bool lead = hasIcon(o.leadingIcon);
    // Password fields get a visibility toggle unless another trailing icon was asked for.
    ImGui::PushID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID focusKey = ImGui::GetID("##focused"), revealKey = ImGui::GetID("##reveal");
    const ImGuiID clickFocusKey = ImGui::GetID("##clickFocus");
    const bool focused = st->GetBool(focusKey, false);
    bool reveal = st->GetBool(revealKey, false);
    const std::string_view trailing = trailingIcon(o, reveal);
    const bool trail = l.trail;

    field::FieldFrame f;
    f.p = ImGui::GetCursorScreenPos();
    f.w = w;
    f.h = h;
    f.outlined = o.outlined;
    f.inputX = f.p.x + (lead ? dp(52) : dp(16));
    // At least a pixel, also in a field narrower than its insets: ImGui takes a width <= 0 as "the available width
    // minus that", which would stretch the input past the field, over its siblings.
    f.inputW = std::max(1.0f, w - (f.inputX - f.p.x) - (trail ? dp(52) : dp(16)));

    ImGui::BeginGroup();
    field::drawFieldBackground(f, focused, field::containerHovered(f), field::scopeDisabled());
    // Body text at bodyLarge, vertically placed per variant.
    pushFont(TextStyle::BodyLarge);
    const float lineH = l.lineH, inputY = l.inputY;

    ImGui::SetCursorScreenPos({f.inputX, f.p.y + inputY});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, (o.error ? s.error : s.primary).vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (o.password && !reveal) flags |= ImGuiInputTextFlags_Password;
    if (o.readOnly) flags |= ImGuiInputTextFlags_ReadOnly;
    if (st->GetBool(clickFocusKey, false)) {
        st->SetBool(clickFocusKey, false);
        ImGui::SetKeyboardFocusHere();
    }
    if (o.multiline) {
        // At least a line, also in a short field: ImGui takes a height <= 0 as "the available height minus that",
        // which would grow the input to the window's height.
        const float inputH = std::max(h - inputY - dp(8), lineH);
        res.changed = ImGui::InputTextMultiline("##input", value.data(), value.capacity() + 1, {f.inputW, inputH},
                                                flags, field::inputResizeCallback, &value);
    } else {
        flags |= ImGuiInputTextFlags_EnterReturnsTrue;
        ImGui::SetNextItemWidth(f.inputW);
        // ImGui takes a C string; one buffer, reused (the UI runs on one thread), saves an allocation per field and
        // frame.
        static std::string hint;
        hint.assign((!o.placeholder.empty() && (!hasLabel || focused)) ? o.placeholder : std::string_view{});
        res.accepted = ImGui::InputTextWithHint("##input", hint.c_str(), value.data(), value.capacity() + 1, flags,
                                                field::inputResizeCallback, &value);
        res.changed = ImGui::IsItemEdited();
    }
    res.committed = ImGui::IsItemDeactivatedAfterEdit();
    const bool active = ImGui::IsItemActive();
    const bool dis = itemDisabled();
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    st->SetBool(focusKey, active);

    // Clicking anywhere in the container (but the trailing icon) focuses the input, on the next frame.
    if (!active && !dis && field::containerHovered(f)) {
        hovered = true;
        const bool onTrailing = trail && ImGui::GetMousePos().x >= f.p.x + w - dp(50);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive() && !onTrailing &&
            ImGui::IsWindowHovered()) {
            st->SetBool(clickFocusKey, true);
            requestFrame();
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
    field::drawFieldDecor(f, o.label, active, hovered, !value.empty(), o.error, dis, o.leadingIcon, trailing,
                          ImGui::GetID("##lbl"), o.multiline);

    ImGui::SetCursorScreenPos(f.p);
    ImGui::Dummy({w, h});
    if (!o.supporting.empty()) {
        ImVec2 sp = ImGui::GetCursorScreenPos();
        field::drawSupporting({f.p.x, f.p.y + h}, w, o.supporting, o.error, dis);
        ImGui::SetCursorScreenPos({sp.x, f.p.y + h});
        ImGui::Dummy({w, dp(20)});
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return res;
}

} // namespace imlui::md
