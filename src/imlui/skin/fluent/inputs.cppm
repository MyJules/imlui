// Fluent inputs: the text box, number box, combo box and color field of Fluent 2 (WinUI 3), for md::textField,
// md::spinBox, md::dropdown and md::colorField. Same items, IDs, editing, callbacks and return values as the md::
// widgets; only the look and the metrics are Fluent's:
//
//   textField  -> TextBox: a header (the label) above a controlHeight box (multiline: `height`, header included),
//                 the control fill (the input-active fill while editing), a stroke whose bottom edge is the strong
//                 stroke, the accent underline while editing (tokens.inputUnderline; else an accent border), the
//                 placeholder in the secondary text color, the leading icon inside, the trailing icon (or a
//                 password's reveal toggle) as a subtle inner button, the supporting text as a description below.
//   spinBox    -> NumberBox with inline spin buttons: the text box with two subtle inner buttons (up, down).
//   dropdown   -> ComboBox: a standard control with the selected item and a chevron; a flyout of rows (the overlay
//                 radius, the flyout colors) with the selected row's accent pill.
//   colorField -> a standard control (a drop-down button) with a swatch, the hex value and a chevron; a flyout with
//                 ImGui's color picker and a Fluent hex text box.
//
// `outlined` (Material's variant) doesn't change the Fluent look. `error` draws the bottom edge and the description
// in the scheme's error role (Fluent's critical color). Natural widths are the md:: widgets' (280, 200, 240dp), so a
// layout keeps its widths across designs.
export module imlui.skin.fluent.inputs;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.button;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.icons;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.ui.frame_request;
import imlui.ui.layout_box;
import imlui.widgets.anchored_popup;
import imlui.widgets.button;
import imlui.widgets.common;
import imlui.widgets.dropdown;
import imlui.widgets.field;
import imlui.widgets.text_field;

export namespace imlui::fluent {

/// Fluent constants without a token, in dp.
inline constexpr float innerButtonWidth = 30.0f;  ///< a text box's inner button (delete, reveal, spin); times the density
inline constexpr float innerButtonMargin = 4.0f;  ///< its margin inside the box (right, top, bottom)
inline constexpr float descriptionGap = 4.0f;     ///< between a box and its description below
inline constexpr float flyoutGap = 4.0f;          ///< between a control and its flyout
inline constexpr float flyoutPaddingY = 2.0f;     ///< a list flyout's padding above and below its rows
inline constexpr float rowInsetX = 4.0f;          ///< a flyout row's highlight inset, left and right
inline constexpr float rowInsetY = 2.0f;          ///< the same above and below
inline constexpr float swatchInset = 6.0f;        ///< a color field's swatch inset from the box's top and bottom
inline constexpr float pickerPadding = 16.0f;     ///< the color flyout's padding
inline constexpr float pickerWidth = 240.0f;      ///< the color picker's width (md::colorField's)

/// md::textField's parameters: a Fluent text box.
md::TextFieldResult textField(const Look& look, const char* id, std::string& value, const md::TextFieldOptions& o);
/// The size textField() takes with the same options.
[[nodiscard]] ImVec2 textFieldSize(const Look& look, const md::TextFieldOptions& o);

/// md::spinBox's parameters: a Fluent number box with inline spin buttons. Returns whether the value changed.
bool spinBox(const Look& look, const char* id, double* v, double vmin, double vmax, double step, int decimals,
             std::string_view label, float width, bool outlined);
/// The size spinBox() takes with the same label and width.
[[nodiscard]] ImVec2 spinBoxSize(const Look& look, std::string_view label, float width, bool outlined);

/// md::dropdown's parameters: a Fluent combo box. Returns whether `*current` changed.
bool dropdown(const Look& look, const char* id, const std::vector<std::string>& items, int* current,
              const md::DropdownOptions& o, const std::vector<std::string>& icons);
/// The size dropdown() takes with the same options.
[[nodiscard]] ImVec2 dropdownSize(const Look& look, const md::DropdownOptions& o);

/// md::colorField's parameters: a Fluent color drop-down. Returns whether `*c` changed.
bool colorField(const Look& look, const char* id, Color* c, std::string_view label, bool alpha, float width);
/// The size colorField() takes with the same color, label and width.
[[nodiscard]] ImVec2 colorFieldSize(const Look& look, Color c, std::string_view label, float width);

/// The height of a flyout row (controlHeight).
[[nodiscard]] float flyoutRowHeight(const Look& look);
/// The height of a header (an input's label above it): a body line and the gap; 0 without a label.
[[nodiscard]] float headerHeight(const Look& look, std::string_view label);
/// The height of a description (supporting text below an input): the gap and a caption line; 0 without one.
[[nodiscard]] float descriptionHeight(std::string_view text);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr Color clear{0, 0, 0, 0};
constexpr TextStyle captionStyle = TextStyle::BodySmall;

/// Fluent's critical color (validation errors): the scheme's error role, which Fluent's colors don't carry.
Color critical() { return md::scheme().error; }

float lineHeight(TextStyle style) {
    pushFont(style);
    const float h = ImGui::GetFontSize();
    ImGui::PopFont();
    return h;
}

float innerButtonW(const Look& look) { return dp(innerButtonWidth * look.tokens.density); }

/// An inner button's height: the control height less the margins above and below.
float innerButtonH(const Look& look) { return std::max(1.0f, controlHeight(look) - 2.0f * dp(innerButtonMargin)); }

/// The colors an input box's background is painted with (WinUI's TextControl brushes).
ControlColors inputColors(const Look& look, bool editing, bool hovered, bool dis) {
    const Colors& c = look.colors;
    if (dis) return {c.controlDisabled, c.controlStroke, c.controlStroke, c.textDisabled};
    const Color bottom = look.tokens.inputUnderline ? c.controlStrongStroke : c.controlBottomStroke;
    if (editing) return {c.controlInputActive, c.controlStroke, bottom, c.text};
    return {hovered ? c.controlHover : c.control, c.controlStroke, bottom, c.text};
}

/// Draws an input box's background and border in [a, b] (before its input, with the last frame's editing state).
void drawInputBox(ImDrawList* dl, const Look& look, ImVec2 a, ImVec2 b, bool editing, bool hovered, bool dis) {
    drawControl(dl, a, b, controlRadius(look), inputColors(look, editing, hovered, dis), strokeWidth(look));
}

/// Draws what marks an input box as being edited or in error (after its input, with this frame's state): the accent
/// underline (or, without tokens.inputUnderline, an accent border), a critical bottom edge.
void drawInputState(ImDrawList* dl, const Look& look, ImVec2 a, ImVec2 b, bool editing, bool error, bool dis) {
    if (dis) return;
    const float radius = controlRadius(look);
    const float underline = dp(look.tokens.inputUnderlineWidth);
    if (error) {
        if (look.tokens.inputUnderline || !editing)
            drawUnderline(dl, a, b, radius, critical(), editing ? underline : strokeWidth(look));
        else md::strokeRoundRect(dl, a, b, radius, critical(), strokeWidth(look));
        return;
    }
    if (!editing) return;
    if (look.tokens.inputUnderline) drawUnderline(dl, a, b, radius, look.colors.accent, underline);
    else if (strokeWidth(look) > 0) md::strokeRoundRect(dl, a, b, radius, look.colors.accent, strokeWidth(look));
}

/// Draws a header (the label above an input) at `p`.
void drawHeader(ImDrawList* dl, const Look& look, ImVec2 p, float w, std::string_view label, bool dis) {
    if (label.empty()) return;
    dl->PushClipRect(p, {p.x + w, p.y + lineHeight(bodyStyle)}, true);
    md::drawText(dl, bodyStyle, p, dis ? look.colors.textDisabled : look.colors.text, label);
    dl->PopClipRect();
}

/// Draws a description (supporting text below an input) under the box whose bottom-left is `at`.
void drawDescription(ImDrawList* dl, const Look& look, ImVec2 at, float w, std::string_view text, bool error,
                     bool dis) {
    if (text.empty()) return;
    const Color c = dis ? look.colors.textDisabled : (error ? critical() : look.colors.textSecondary);
    const ImVec2 p{at.x, at.y + dp(descriptionGap)};
    dl->PushClipRect(p, {p.x + w, p.y + lineHeight(captionStyle)}, true);
    md::drawText(dl, captionStyle, p, c, text);
    dl->PopClipRect();
}

/// Pushes the colors an input's ImGui item is drawn with: no frame (the box is drawn around it), Fluent's text and
/// placeholder colors, the caret, and no ImGui nav rectangle (drawFocusRect is drawn instead). popInputStyle() after.
void pushInputStyle(const Look& look, bool error, bool dis) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, clear.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, clear.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, clear.vec4());
    ImGui::PushStyleColor(ImGuiCol_NavCursor, clear.vec4());
    ImGui::PushStyleColor(ImGuiCol_Text, (dis ? look.colors.textDisabled : look.colors.text).vec4());
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, look.colors.textSecondary.vec4()); // the placeholder (hint)
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, (error ? critical() : look.colors.text).vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    pushFont(bodyStyle);
}

void popInputStyle() {
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(7);
}

// ---- Flyout
// A flyout surface anchored below a control, and its rows. Group (C) builds Fluent menus (menuItem,
// beginAnchoredMenu) with the same look; until those land, the combo box draws these private ones.

/// Begins a flyout popup anchored to [a, b] (md::placeAnchoredPopup's placement: below, kept in the viewport), at
/// least `minWidth` wide and at most `maxHeight` high, with `padding`. Returns true while open; then ImGui::EndPopup().
bool beginFlyout(const Look& look, const char* popupId, ImVec2 a, ImVec2 b, float minWidth, ImVec2 padding,
                 float maxHeight) {
    // The elevation shadow is md::placeAnchoredPopup's, drawn behind the popup.
    const md::AnchoredPopup placed = md::placeAnchoredPopup(popupId, a, b, minWidth, dp(flyoutGap));
    ImGui::SetNextWindowSizeConstraints({minWidth, 0}, {md::hugeExtent, maxHeight});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, look.colors.flyout.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, look.colors.flyoutStroke.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, overlayRadius(look));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, strokeWidth(look));
    const bool open = ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    md::trackAnchoredPopup(placed, open);
    return open;
}

/// One row of a list flyout (a combo box item): `controlHeight` high, as wide as the widest row (layout::claimWidth
/// in the flyout's shrink-to-fit box). A subtle highlight inset in the row, the selected row's accent pill. Returns
/// true when chosen (the popup closes).
bool flyoutRow(const Look& look, const char* id, std::string_view label, std::string_view icon, bool selected) {
    const Content ct = content(look, label, icon);
    const float h = flyoutRowHeight(look);
    const float insetX = dp(rowInsetX), insetY = dp(rowInsetY);
    const float w = layout::claimWidth(2.0f * insetX + 2.0f * paddingX(look) + ct.width);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Header, clear.vec4());
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, clear.vec4());
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, clear.vec4());
    ImGui::PushStyleColor(ImGuiCol_NavCursor, clear.vec4());
    const bool clicked = ImGui::Selectable(id, selected, ImGuiSelectableFlags_None, {w, h});
    ImGui::PopStyleColor(4);
    const ItemState item = itemState();
    const State state = stateOf(item);
    const Colors& c = look.colors;

    Color fill = clear;
    switch (state) {
    case State::Rest: fill = selected ? c.subtleHover : c.subtle; break;
    case State::Hover: fill = selected ? c.subtlePressed : c.subtleHover; break;
    case State::Pressed: fill = selected ? c.subtleHover : c.subtlePressed; break;
    case State::Disabled: break;
    }
    const Color text = state == State::Disabled ? c.textDisabled : (state == State::Pressed ? c.textSecondary : c.text);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ha{p.x + insetX, p.y + insetY}, hb{p.x + w - insetX, p.y + h - insetY};
    const float radius = controlRadius(look);
    if (fill.a > 0) md::fillRoundRect(dl, ha, hb, radius, fill);
    const float cy = p.y + h * 0.5f;
    if (selected) {
        // Fluent's selection indicator: a 3 x 16dp accent pill at the highlight's left edge.
        const float pw = dp(3), ph = std::min(dp(16), hb.y - ha.y);
        md::fillRoundRect(dl, {ha.x, cy - ph * 0.5f}, {ha.x + pw, cy + ph * 0.5f}, pw * 0.5f,
                          state == State::Disabled ? c.accentDisabled : c.accent);
    }
    drawContent(dl, ct, ha.x + paddingX(look), cy, text);
    if (item.focused) drawFocusRect(dl, ha, hb, radius, look);
    return clicked;
}

// ---- Text field

/// The trailing icon of a field with options `o`: its own, else a password's visibility toggle (md::textField's).
std::string_view trailingIcon(const md::TextFieldOptions& o, bool reveal) {
    if (o.password && o.trailingIcon.empty()) return reveal ? "visibility_off" : "visibility";
    return o.trailingIcon;
}

struct TextFieldLayout {
    float w = 0;
    float headerH = 0;
    float boxH = 0;
    float lineH = 0;
    float inputDy = 0;  ///< the input's top, from the box's
    float inputH = 0;   ///< multiline: the input's height
    float inputLeft = 0, inputRight = 0; ///< the input's insets from the box's left and right
    float leadX = 0;    ///< the leading icon's center, from the box's left
    bool lead = false, trail = false;
    ImVec2 size;        ///< header, box (or what of the input overhangs it), description
};

TextFieldLayout textFieldLayout(const Look& look, const md::TextFieldOptions& o) {
    TextFieldLayout l;
    l.w = md::itemWidth(o.width, dp(280));
    l.headerH = headerHeight(look, o.label);
    l.lineH = lineHeight(bodyStyle);
    const float ch = controlHeight(look);
    if (o.multiline) {
        // `height` is the header and the box, like md's container (its label is inside).
        const float total = o.height > 0 ? o.height : dp(120);
        l.boxH = std::max(total - l.headerH, ch);
    } else {
        l.boxH = ch;
    }
    // The first line sits where a single line does; a multiline input fills the box down to the same inset.
    l.inputDy = (ch - l.lineH) * 0.5f;
    if (o.multiline) l.inputH = std::max(l.boxH - 2.0f * l.inputDy, l.lineH);
    l.lead = hasIcon(o.leadingIcon);
    l.trail = hasIcon(trailingIcon(o, false));
    const float px = paddingX(look);
    const float icon = dp(iconSize(look));
    l.leadX = px + icon * 0.5f;
    l.inputLeft = l.lead ? px + icon + gap(look) : px;
    l.inputRight = l.trail ? dp(innerButtonMargin) + innerButtonW(look) + dp(innerButtonMargin) : px;
    const float input = l.inputDy + (o.multiline ? l.inputH : l.lineH);
    l.size = {l.w, l.headerH + std::max(l.boxH, input) + descriptionHeight(o.supporting)};
    return l;
}

} // namespace

float flyoutRowHeight(const Look& look) { return controlHeight(look); }

float headerHeight(const Look& look, std::string_view label) {
    return label.empty() ? 0.0f : lineHeight(bodyStyle) + gap(look);
}

float descriptionHeight(std::string_view text) {
    return text.empty() ? 0.0f : dp(descriptionGap) + lineHeight(captionStyle);
}

ImVec2 textFieldSize(const Look& look, const md::TextFieldOptions& o) { return textFieldLayout(look, o).size; }

md::TextFieldResult textField(const Look& look, const char* id, std::string& value, const md::TextFieldOptions& o) {
    md::TextFieldResult res;
    const TextFieldLayout l = textFieldLayout(look, o);
    ImGui::PushID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID focusKey = ImGui::GetID("##focused"), revealKey = ImGui::GetID("##reveal");
    const ImGuiID clickFocusKey = ImGui::GetID("##clickFocus");
    const bool focused = st->GetBool(focusKey, false);
    const bool reveal = st->GetBool(revealKey, false);
    const std::string_view trailing = trailingIcon(o, reveal);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 a{p.x, p.y + l.headerH}, b{p.x + l.w, a.y + l.boxH};
    md::field::FieldFrame f;
    f.p = a;
    f.w = l.w;
    f.h = l.boxH;
    const float inputX = a.x + l.inputLeft;
    // At least a pixel, also in a field narrower than its insets: ImGui takes a width <= 0 as "the available width
    // minus that", which would stretch the input past the field, over its siblings.
    const float inputW = std::max(1.0f, l.w - l.inputLeft - l.inputRight);

    ImGui::BeginGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool scopeDis = md::field::scopeDisabled();
    drawInputBox(dl, look, a, b, focused, md::field::containerHovered(f) && !focused, scopeDis);

    ImGui::SetCursorScreenPos({inputX, a.y + l.inputDy});
    pushInputStyle(look, o.error, scopeDis);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (o.password && !reveal) flags |= ImGuiInputTextFlags_Password;
    if (o.readOnly) flags |= ImGuiInputTextFlags_ReadOnly;
    if (st->GetBool(clickFocusKey, false)) {
        st->SetBool(clickFocusKey, false);
        ImGui::SetKeyboardFocusHere();
    }
    if (o.multiline) {
        res.changed = ImGui::InputTextMultiline("##input", value.data(), value.capacity() + 1, {inputW, l.inputH},
                                                flags, md::field::inputResizeCallback, &value);
    } else {
        flags |= ImGuiInputTextFlags_EnterReturnsTrue;
        ImGui::SetNextItemWidth(inputW);
        // The header is above the box, so the placeholder shows whenever the text is empty (WinUI's).
        const std::string hint{o.placeholder};
        res.accepted = ImGui::InputTextWithHint("##input", hint.c_str(), value.data(), value.capacity() + 1, flags,
                                                md::field::inputResizeCallback, &value);
        res.changed = ImGui::IsItemEdited();
    }
    const bool active = ImGui::IsItemActive();
    const bool dis = md::itemDisabled();
    const bool keyboardFocus = focusVisible() && !active;
    popInputStyle();
    st->SetBool(focusKey, active);

    // Clicking anywhere in the box (but the trailing button) focuses the input, on the next frame.
    const float trailX = b.x - dp(innerButtonMargin) - innerButtonW(look);
    if (!active && !dis && md::field::containerHovered(f)) {
        const bool onTrailing = l.trail && ImGui::GetMousePos().x >= trailX;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive() && !onTrailing &&
            ImGui::IsWindowHovered()) {
            st->SetBool(clickFocusKey, true);
            requestFrame();
        }
    }
    const Colors& c = look.colors;
    const Color iconColor = dis ? c.textDisabled : c.textSecondary;
    const float iconCy = a.y + controlHeight(look) * 0.5f; // on the first line
    if (l.trail) {
        // WinUI's delete button: subtle, inside the box, not a tab stop (like md's trailing icon).
        const ImVec2 ta{trailX, a.y + dp(innerButtonMargin)};
        const ImVec2 tb{ta.x + innerButtonW(look), ta.y + innerButtonH(look)};
        ImGui::SetCursorScreenPos(ta);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        if (ImGui::InvisibleButton("##trailing", {tb.x - ta.x, tb.y - ta.y})) {
            res.trailingClicked = true;
            if (o.password && o.trailingIcon.empty()) st->SetBool(revealKey, !reveal);
        }
        ImGui::PopStyleVar();
        const ControlColors tc = controlColors(look, Fill::Subtle, stateOf(itemState()));
        drawControl(dl, ta, tb, controlRadius(look), tc, 0.0f);
        md::drawIcon(dl, trailing, {(ta.x + tb.x) * 0.5f, (ta.y + tb.y) * 0.5f}, iconSize(look),
                     o.error && !dis ? critical() : iconColor);
    }
    if (l.lead) md::drawIcon(dl, o.leadingIcon, {a.x + l.leadX, iconCy}, iconSize(look), iconColor);
    drawInputState(dl, look, a, b, active, o.error, dis);
    drawHeader(dl, look, p, l.w, o.label, dis);
    drawDescription(dl, look, {a.x, a.y + l.size.y - l.headerH - descriptionHeight(o.supporting)}, l.w,
                    o.supporting, o.error, dis);
    if (keyboardFocus) drawFocusRect(dl, a, b, controlRadius(look), look);

    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(l.size);
    ImGui::EndGroup();
    ImGui::PopID();
    return res;
}

// ---- Spin box

namespace {

struct SpinBoxLayout {
    float w = 0, headerH = 0, boxH = 0, lineH = 0, inputDy = 0;
    float buttonW = 0, buttonH = 0;
    ImVec2 size;
};

SpinBoxLayout spinBoxLayout(const Look& look, std::string_view label, float width) {
    SpinBoxLayout l;
    l.w = md::itemWidth(width == 0.0f ? dp(200) : width, dp(200));
    l.headerH = headerHeight(look, label);
    l.boxH = controlHeight(look);
    l.lineH = lineHeight(bodyStyle);
    l.inputDy = (l.boxH - l.lineH) * 0.5f;
    l.buttonW = innerButtonW(look);
    l.buttonH = innerButtonH(look);
    // The box, and the input on its line (taller only with a large font).
    l.size = {l.w, l.headerH + std::max(l.boxH, l.inputDy + l.lineH)};
    return l;
}

} // namespace

ImVec2 spinBoxSize(const Look& look, std::string_view label, float width, bool) {
    return spinBoxLayout(look, label, width).size;
}

bool spinBox(const Look& look, const char* id, double* v, double vmin, double vmax, double step, int decimals,
             std::string_view label, float width, bool) {
    const SpinBoxLayout l = spinBoxLayout(look, label, width);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 a{p.x, p.y + l.headerH}, b{p.x + l.w, a.y + l.boxH};
    md::field::FieldFrame f;
    f.p = a;
    f.w = l.w;
    f.h = l.boxH;
    const float margin = dp(innerButtonMargin);
    const float buttonsX = b.x - margin - 2.0f * l.buttonW;
    const float inputX = a.x + paddingX(look);
    // At least a pixel, also in a box narrower than its insets (see textField).
    const float inputW = std::max(1.0f, buttonsX - margin - inputX);
    bool changed = false;
    // The input's focus from the last frame: the box is drawn before the input, to stay under its text.
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID focusKey = ImGui::GetID("##focused");
    const bool focused = st->GetBool(focusKey, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool scopeDis = md::field::scopeDisabled();
    drawInputBox(dl, look, a, b, focused, md::field::containerHovered(f) && !focused, scopeDis);

    ImGui::SetCursorScreenPos({inputX, a.y + l.inputDy});
    pushInputStyle(look, false, scopeDis);
    ImGui::SetNextItemWidth(inputW);
    char fmt[16];
    std::snprintf(fmt, sizeof fmt, "%%.%df", std::max(0, decimals));
    if (ImGui::InputScalar("##value", ImGuiDataType_Double, v, nullptr, nullptr, fmt)) changed = true;
    const bool active = ImGui::IsItemActive();
    const bool dis = md::itemDisabled();
    const bool keyboardFocus = focusVisible() && !active;
    st->SetBool(focusKey, active);
    popInputStyle();

    drawInputState(dl, look, a, b, active, false, dis);
    drawHeader(dl, look, p, l.w, label, dis);
    if (keyboardFocus) drawFocusRect(dl, a, b, controlRadius(look), look);

    // NumberBox's inline spin buttons: up (increment), then down (decrement).
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::SetCursorScreenPos({buttonsX, a.y + (l.boxH - l.buttonH) * 0.5f});
    ImGui::BeginDisabled(*v >= vmax);
    if (button(look, "##inc", md::ButtonVariant::Text, {l.buttonW, l.buttonH}, "keyboard_arrow_up")) {
        *v += step;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 0);
    ImGui::BeginDisabled(*v <= vmin);
    if (button(look, "##dec", md::ButtonVariant::Text, {l.buttonW, l.buttonH}, "keyboard_arrow_down")) {
        *v -= step;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    // from > to (or a NaN bound) is an empty range: like the Slider, leave the value alone instead of clamping.
    if (changed && vmin <= vmax) *v = std::clamp(*v, vmin, vmax);

    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(l.size);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

// ---- Dropdown

namespace {

struct DropdownLayout {
    float w = 0, headerH = 0, boxH = 0;
    ImVec2 size;
};

DropdownLayout dropdownLayout(const Look& look, const md::DropdownOptions& o) {
    DropdownLayout l;
    l.w = md::itemWidth(o.width == 0.0f ? dp(240) : o.width, dp(240));
    l.headerH = headerHeight(look, o.label);
    l.boxH = controlHeight(look);
    l.size = {l.w, l.headerH + l.boxH + descriptionHeight(o.supporting)};
    return l;
}

/// Draws a drop-down control's chevron at the right of [a, b].
void drawChevron(ImDrawList* dl, const Look& look, ImVec2 a, ImVec2 b, Color c) {
    const float icon = dp(iconSize(look));
    md::drawIcon(dl, "expand_more", {b.x - paddingX(look) - icon * 0.5f, (a.y + b.y) * 0.5f}, iconSize(look), c);
}

} // namespace

ImVec2 dropdownSize(const Look& look, const md::DropdownOptions& o) { return dropdownLayout(look, o).size; }

bool dropdown(const Look& look, const char* id, const std::vector<std::string>& items, int* current,
              const md::DropdownOptions& o, const std::vector<std::string>& icons) {
    const DropdownLayout l = dropdownLayout(look, o);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 a{p.x, p.y + l.headerH}, b{p.x + l.w, a.y + l.boxH};
    ImGui::SetCursorScreenPos(a);
    const bool clicked = invisibleButton("##field", {l.w, l.boxH});
    const ItemState item = itemState();
    if (clicked) ImGui::OpenPopup("##menu");
    const bool open = ImGui::IsPopupOpen("##menu");
    // Open, the combo box stays pressed.
    const State state = open && !item.disabled ? State::Pressed : stateOf(item);
    const ControlColors c = controlColors(look, Fill::Standard, state);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawControl(dl, a, b, controlRadius(look), c, strokeWidth(look));
    if (o.error && !item.disabled) drawUnderline(dl, a, b, controlRadius(look), critical(), strokeWidth(look));
    const float px = paddingX(look), icon = dp(iconSize(look));
    const float cy = (a.y + b.y) * 0.5f;
    const Color iconColor = item.disabled ? look.colors.textDisabled : look.colors.textSecondary;
    float textX = a.x + px;
    if (hasIcon(o.leadingIcon)) {
        md::drawIcon(dl, o.leadingIcon, {textX + icon * 0.5f, cy}, iconSize(look), iconColor);
        textX += icon + gap(look);
    }
    const bool has = *current >= 0 && *current < static_cast<int>(items.size());
    if (has) {
        const float textRight = b.x - px - icon - gap(look);
        dl->PushClipRect({textX, a.y}, {std::max(textX, textRight), b.y}, true);
        md::drawText(dl, bodyStyle, {textX, md::textTop(bodyStyle, cy)}, c.text,
                     items[static_cast<std::size_t>(*current)]);
        dl->PopClipRect();
    }
    drawChevron(dl, look, a, b, iconColor);
    drawHeader(dl, look, p, l.w, o.label, item.disabled);
    drawDescription(dl, look, {a.x, b.y}, l.w, o.supporting, o.error, item.disabled);
    if (item.focused) drawFocusRect(dl, a, b, controlRadius(look), look);

    bool changed = false;
    const float rowH = flyoutRowHeight(look);
    const float padY = dp(flyoutPaddingY);
    if (beginFlyout(look, "##menu", a, b, l.w, {0, padY}, rowH * 8 + 2.0f * padY)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        // The flyout takes its rows' width, at least the control's; a row fills the widest, never the current width.
        layout::pushBox(layout::shrinkToFitBox(l.w, md::hugeExtent));
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i);
            const auto idx = static_cast<std::size_t>(i);
            const std::string_view ic = idx < icons.size() ? std::string_view{icons[idx]} : std::string_view{};
            if (flyoutRow(look, "##it", items[idx], ic, i == *current) && i != *current) {
                *current = i;
                changed = true;
            }
            if (i == *current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        layout::popBox();
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(l.size);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

// ---- Color field

namespace {

struct ColorFieldLayout {
    float w = 0, headerH = 0, boxH = 0, swatch = 0;
    ImVec2 size;
};

ColorFieldLayout colorFieldLayout(const Look& look, Color c, std::string_view label, float width) {
    ColorFieldLayout l;
    l.headerH = headerHeight(look, label);
    l.boxH = controlHeight(look);
    l.swatch = std::max(1.0f, l.boxH - 2.0f * dp(swatchInset));
    const float px = paddingX(look), g = gap(look);
    const float box = px + l.swatch + g + md::measure(bodyStyle, toHex(c)).x + g + dp(iconSize(look)) + px;
    const float header = label.empty() ? 0.0f : md::measure(bodyStyle, label).x;
    l.w = md::itemWidth(width, std::max(box, header));
    l.size = {l.w, l.headerH + l.boxH};
    return l;
}

} // namespace

ImVec2 colorFieldSize(const Look& look, Color c, std::string_view label, float width) {
    return colorFieldLayout(look, c, label, width).size;
}

bool colorField(const Look& look, const char* id, Color* c, std::string_view label, bool alpha, float width) {
    const std::string hex = toHex(*c);
    const ColorFieldLayout l = colorFieldLayout(look, *c, label, width);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 a{p.x, p.y + l.headerH}, b{p.x + l.w, a.y + l.boxH};
    ImGui::SetCursorScreenPos(a);
    const bool clicked = invisibleButton("##swatch", {l.w, l.boxH});
    const ItemState item = itemState();
    if (clicked) ImGui::OpenPopup("##picker");
    const bool open = ImGui::IsPopupOpen("##picker");
    const State state = open && !item.disabled ? State::Pressed : stateOf(item);
    const ControlColors cc = controlColors(look, Fill::Standard, state);
    const Colors& fc = look.colors;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawControl(dl, a, b, controlRadius(look), cc, strokeWidth(look));
    const float px = paddingX(look), g = gap(look);
    const ImVec2 sa{a.x + px, a.y + (l.boxH - l.swatch) * 0.5f}, sb{sa.x + l.swatch, sa.y + l.swatch};
    const float sr = controlRadius(look) * 0.5f;
    md::fillRoundRect(dl, sa, sb, sr, c->withAlpha(1.0f));
    md::strokeRoundRect(dl, sa, sb, sr, item.disabled ? fc.controlStrongStrokeDisabled : fc.controlStrongStroke,
                        std::max(1.0f, strokeWidth(look)));
    const float cy = (a.y + b.y) * 0.5f;
    md::drawText(dl, bodyStyle, {sb.x + g, md::textTop(bodyStyle, cy)}, cc.text, hex);
    drawChevron(dl, look, a, b, item.disabled ? fc.textDisabled : fc.textSecondary);
    drawHeader(dl, look, p, l.w, label, item.disabled);
    if (item.focused) drawFocusRect(dl, a, b, controlRadius(look), look);

    bool changed = false;
    // The picker and the padding on each side: the width assumed before the first open.
    const float pad = dp(pickerPadding), pw = dp(pickerWidth);
    if (beginFlyout(look, "##picker", a, b, pw + 2.0f * pad, {pad, pad}, md::hugeExtent)) {
        // The popup takes its content's width: a fill in it doesn't read the popup's width.
        layout::pushBox(layout::shrinkToFitBox(pw, pw));
        float v[4] = {c->r, c->g, c->b, c->a};
        ImGui::SetNextItemWidth(pw);
        ImGuiColorEditFlags fl = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                 ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueBar |
                                 ImGuiColorEditFlags_NoInputs;
        if (alpha) fl |= ImGuiColorEditFlags_AlphaBar;
        else fl |= ImGuiColorEditFlags_NoAlpha;
        if (ImGui::ColorPicker4("##pick", v, fl)) {
            *c = {v[0], v[1], v[2], alpha ? v[3] : 1.0f};
            changed = true;
        }
        ImGui::Dummy({0, dp(4)});
        std::string buf = toHex(*c);
        md::TextFieldOptions o;
        o.label = "Hex";
        o.outlined = true;
        o.width = pw;
        o.leadingIcon = "tag";
        const md::TextFieldResult r = textField(look, "##hex", buf, o);
        if (r.changed)
            if (auto nc = parseHex(buf)) {
                // #RGBA / #RRGGBBAA parse with an alpha, which only an alpha picker keeps.
                *c = alpha ? *nc : nc->withAlpha(1.0f);
                changed = true;
            }
        layout::popBox();
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(l.size);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace imlui::fluent
