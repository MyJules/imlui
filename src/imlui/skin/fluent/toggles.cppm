// Fluent toggles: WinUI 3's CheckBox, RadioButton and ToggleSwitch, for md::checkbox, md::radio and
// md::switchToggle (same parameters and return values; only the look and the metrics are Fluent's).
//
//   checkbox -> a 20dp box with the control radius: unchecked an alt fill with a strong 1dp outline, checked an accent
//               fill with a check on accent
//   radio    -> a 20dp circle: unchecked like the checkbox, checked an accent circle with a dot on accent (12dp, 14dp
//               on hover, 10dp pressed)
//   switch   -> a 40 x 20dp pill: off an alt fill with a strong outline and a secondary-text knob, on an accent fill
//               with a knob on accent; the knob is 12dp, 14dp on hover and stretches to 17dp while pressed.
//               `withIcons` is ignored: Fluent's knob carries no icon.
//
// Height: controlHeight (at least the mark). Width: the mark, then gap + label when there is one. The whole item is
// the hit area and the focus rectangle's frame.
export module imlui.skin.fluent.toggles;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.widgets.common;

export namespace imlui::fluent {

/// A checkbox (md::checkbox's parameters): toggles `*v` on a click; returns whether it was clicked.
bool checkbox(const Look& look, const char* label, bool* v);
/// The size checkbox() takes with `label`.
[[nodiscard]] ImVec2 checkboxSize(const Look& look, const char* label);
/// A radio button (md::radio's parameters); returns whether it was clicked.
bool radio(const Look& look, const char* label, bool selected);
/// The size radio() takes with `label`.
[[nodiscard]] ImVec2 radioSize(const Look& look, const char* label);
/// A toggle switch (md::switchToggle's parameters): toggles `*v` on a click; returns whether it was clicked.
/// `withIcons` is accepted and ignored (Fluent's knob has no icon).
bool switchToggle(const Look& look, const char* label, bool* v, bool withIcons);
/// The size switchToggle() takes with `label`.
[[nodiscard]] ImVec2 switchSize(const Look& look, const char* label);

/// The checkbox's and the radio button's mark, dp (WinUI: 20 x 20).
inline constexpr float markDp = 20.0f;
/// The switch's track, dp (WinUI: 40 x 20).
inline constexpr float switchTrackWidthDp = 40.0f;
inline constexpr float switchTrackHeightDp = 20.0f;

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float checkGlyphDp = 16.0f; ///< the check icon (its stroke spans about 12dp, WinUI's glyph)
constexpr float knobDp = 12.0f, knobHoverDp = 14.0f, knobPressedWidthDp = 17.0f;
constexpr float dotDp = 12.0f, dotHoverDp = 14.0f, dotPressedDp = 10.0f;

/// A toggle's measurements: the mark (box, circle or track) and the label after it.
struct MarkLayout {
    std::string_view text;
    float markW = 0, markH = 0;
    float w = 0, h = 0;
};

MarkLayout markLayout(const Look& look, const char* label, float markWDp, float markHDp) {
    MarkLayout l;
    l.text = md::visibleLabel(label);
    l.markW = dp(markWDp);
    l.markH = dp(markHDp);
    l.h = std::max(controlHeight(look), l.markH);
    l.w = l.markW + (l.text.empty() ? 0.0f : gap(look) + md::measure(bodyStyle, l.text).x);
    return l;
}

Color labelColor(const Look& look, State state) {
    return state == State::Disabled ? look.colors.textDisabled : look.colors.text;
}

Color faded(Color c, float t) { return c.withAlpha(c.a * t); }

/// The mark's top-left corner: left-aligned, vertically centered on a whole pixel.
ImVec2 markOrigin(ImVec2 p, const MarkLayout& l) { return {std::round(p.x), std::round(p.y + (l.h - l.markH) * 0.5f)}; }

void drawLabel(ImDrawList* dl, const Look& look, const MarkLayout& l, ImVec2 p, State state) {
    if (l.text.empty()) return;
    md::drawTextV(dl, bodyStyle, p.x + l.markW + gap(look), p.y + l.h * 0.5f, labelColor(look, state), l.text);
}

} // namespace

ImVec2 checkboxSize(const Look& look, const char* label) {
    const MarkLayout l = markLayout(look, label, markDp, markDp);
    return {l.w, l.h};
}

ImVec2 radioSize(const Look& look, const char* label) {
    const MarkLayout l = markLayout(look, label, markDp, markDp);
    return {l.w, l.h};
}

ImVec2 switchSize(const Look& look, const char* label) {
    const MarkLayout l = markLayout(look, label, switchTrackWidthDp, switchTrackHeightDp);
    return {l.w, l.h};
}

bool checkbox(const Look& look, const char* label, bool* v) {
    const MarkLayout l = markLayout(look, label, markDp, markDp);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(label, {l.w, l.h});
    if (clicked) *v = !*v;
    const ItemState item = itemState();
    const State state = stateOf(item);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = markOrigin(p, l), b{a.x + l.markW, a.y + l.markH};
    const float radius = controlRadius(look);
    const float t = md::animate(item.id + 1, *v ? 1.0f : 0.0f, 20.0f);
    if (t < 0.99f) {
        const Color stroke = faded(strongStroke(look, state), 1 - t);
        drawControl(dl, a, b, radius, {faded(altFill(look, state), 1 - t), stroke, stroke, {}}, strokeWidth(look));
    }
    if (t > 0.01f) {
        md::fillRoundRect(dl, a, b, radius, faded(accentToggleFill(look, state), t));
        md::drawIcon(dl, "check", {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, checkGlyphDp * (0.6f + 0.4f * t),
                     faded(accentToggleGlyph(look, state), t));
    }
    drawLabel(dl, look, l, p, state);
    if (item.focused) drawFocusRect(dl, p, {p.x + l.w, p.y + l.h}, radius, look);
    return clicked;
}

bool radio(const Look& look, const char* label, bool selected) {
    const MarkLayout l = markLayout(look, label, markDp, markDp);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(label, {l.w, l.h});
    const ItemState item = itemState();
    const State state = stateOf(item);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = markOrigin(p, l);
    const float r = l.markW * 0.5f;
    const ImVec2 c{a.x + r, a.y + r};
    const float t = md::animate(item.id + 1, selected ? 1.0f : 0.0f, 18.0f);
    if (t < 0.99f) {
        md::fillCircle(dl, c, r, faded(altFill(look, state), 1 - t));
        md::strokeCircle(dl, c, r, faded(strongStroke(look, state), 1 - t), strokeWidth(look));
    }
    const float dot = state == State::Pressed ? dotPressedDp : state == State::Hover ? dotHoverDp : dotDp;
    const float dotR = md::animate(item.id + 2, selected ? dp(dot) * 0.5f : 0.0f, 18.0f);
    if (t > 0.01f) md::fillCircle(dl, c, r, faded(accentToggleFill(look, state), t));
    if (dotR > 0.5f) md::fillCircle(dl, c, dotR, faded(accentToggleGlyph(look, state), t));
    drawLabel(dl, look, l, p, state);
    if (item.focused) drawFocusRect(dl, p, {p.x + l.w, p.y + l.h}, controlRadius(look), look);
    return clicked;
}

bool switchToggle(const Look& look, const char* label, bool* v, [[maybe_unused]] bool withIcons) {
    const MarkLayout l = markLayout(look, label, switchTrackWidthDp, switchTrackHeightDp);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = invisibleButton(label, {l.w, l.h});
    if (clicked) *v = !*v;
    const ItemState item = itemState();
    const State state = stateOf(item);
    const Colors& col = look.colors;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = markOrigin(p, l), b{a.x + l.markW, a.y + l.markH};
    const float pill = l.markH * 0.5f;
    const float t = md::animate(item.id + 1, *v ? 1.0f : 0.0f, 16.0f);
    if (t < 0.99f) {
        const Color stroke = faded(strongStroke(look, state), 1 - t);
        drawControl(dl, a, b, pill, {faded(altFill(look, state), 1 - t), stroke, stroke, {}}, strokeWidth(look));
    }
    if (t > 0.01f) md::fillRoundRect(dl, a, b, pill, faded(accentToggleFill(look, state), t));

    // The knob: centered on the track's end caps, growing on hover and stretching toward the middle while pressed.
    const float kh =
        md::animate(item.id + 2, dp(state == State::Pressed || state == State::Hover ? knobHoverDp : knobDp), 20.0f);
    const float kw = md::animate(item.id + 3, state == State::Pressed ? dp(knobPressedWidthDp) : kh, 20.0f);
    const float cy = a.y + pill;
    const float inset = (l.markH - kh) * 0.5f;
    const float cx =
        std::clamp(a.x + pill + (l.markW - 2 * pill) * t, a.x + inset + kw * 0.5f, b.x - inset - kw * 0.5f);
    const Color offKnob = state == State::Disabled ? col.textDisabled : col.textSecondary;
    md::fillRoundRect(dl, {cx - kw * 0.5f, cy - kh * 0.5f}, {cx + kw * 0.5f, cy + kh * 0.5f}, kh * 0.5f,
                      mix(offKnob, accentToggleGlyph(look, state), t));
    drawLabel(dl, look, l, p, state);
    if (item.focused) drawFocusRect(dl, p, {p.x + l.w, p.y + l.h}, controlRadius(look), look);
    return clicked;
}

} // namespace imlui::fluent
