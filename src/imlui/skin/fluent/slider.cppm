// Fluent slider: WinUI 3's Slider, for md::slider (same parameters, value handling and return value; only the look
// and the metrics are Fluent's).
//
// A 4dp track (the strong fill) with the value part in accent, a 20dp thumb (the opaque solid control fill with
// Fluent's elevation border) carrying an accent dot of 12dp (14dp on hover, 10dp while dragging), tick marks under
// the track with at most 40 steps (WinUI's TickPlacement BottomRight), and the value in a tooltip above the thumb
// while dragging (WinUI's thumb tooltip). Height: controlHeight. Width: 200dp natural, `width` resolved like
// md::itemWidth. The track runs between the thumb's radii, as md's does.
//
// The thumb follows the mouse only when the press came from the mouse: the slider takes keyboard focus (and shows the
// focus rectangle) like every Fluent control, and a keyboard activation leaves the value alone.
export module imlui.skin.fluent.slider;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.typography;
import imlui.widgets.common;
import imlui.widgets.slider;

export namespace imlui::fluent {

/// A slider (md::slider's parameters and rules: `step` <= 0 or not finite is continuous, an empty or non-finite range
/// leaves `*v` alone, `*v` is written only when the user changed it, stepped values carry no binary noise); returns
/// whether the user changed the value.
bool slider(const Look& look, const char* id, double* v, double vmin, double vmax, double step, float width,
            const char* valueFormat);
/// The size slider() takes with `width` (0: the natural 200dp).
[[nodiscard]] ImVec2 sliderSize(const Look& look, float width);

/// The thumb's diameter, dp (WinUI: 20).
inline constexpr float sliderThumbDp = 20.0f;

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float naturalWidthDp = 200.0f;
constexpr float trackDp = 4.0f;
constexpr float dotDp = 12.0f, dotHoverDp = 14.0f, dotPressedDp = 10.0f;
constexpr float tickDp = 4.0f;

/// The value tooltip above the thumb at (x, top), on the foreground: a flyout with the formatted value.
void drawValueTip(const Look& look, float x, float top, double value, const char* valueFormat) {
    char buf[64] = {};
    const char* format = md::checkedSliderFormat(valueFormat);
    // The format is the Slider's `format` prop, chosen by the script, so it can't be a literal.
    // md::checkedSliderFormat() only lets through a format with exactly one floating conversion (and `%%`), which
    // matches the one double argument, so -Wformat-nonliteral is off for this call only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    if (std::snprintf(buf, sizeof buf, format, value) < 0) buf[0] = char{};
#pragma clang diagnostic pop
    constexpr TextStyle style = TextStyle::BodySmall;
    const ImVec2 ts = md::measure(style, buf);
    const float bw = ts.x + 2.0f * dp(8), bh = ts.y + dp(12);
    const ImVec2 a{std::round(x - bw * 0.5f), std::round(top - dp(8) - bh)}, b{a.x + bw, a.y + bh};
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    drawFlyout(fg, a, b, controlRadius(look), look);
    md::drawText(fg, style, {x - ts.x * 0.5f, md::textTop(style, (a.y + b.y) * 0.5f)}, look.colors.text, buf);
}

} // namespace

ImVec2 sliderSize(const Look& look, float width) {
    return {md::itemWidth(width, dp(naturalWidthDp)), std::max(controlHeight(look), dp(sliderThumbDp))};
}

bool slider(const Look& look, const char* id, double* v, double vmin, double vmax, double step, float width,
            const char* valueFormat) {
    const ImVec2 size = sliderSize(look, width);
    const float w = size.x, h = size.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    invisibleButton(id, size);
    const ItemState item = itemState();
    const State state = stateOf(item);
    // Held with the mouse it drags; a keyboard activation (Space, Enter) only focuses.
    const bool dragging = item.pressed && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    const float r = dp(sliderThumbDp) * 0.5f;
    const float x0 = p.x + r, x1 = p.x + w - r;
    const md::SliderRange range = md::sliderRange(vmin, vmax, step);
    const bool changed = dragging && md::sliderDragTo(v, range, ImGui::GetMousePos().x, x0, x1);
    const float t = md::sliderFraction(*v, range);
    const float tv = md::animate(item.id + 2, t, 30.0f);

    const Colors& c = look.colors;
    const bool disabled = state == State::Disabled;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = std::round(p.y + h * 0.5f), th = dp(trackDp);
    const float hx = x0 + (x1 - x0) * tv;
    const Color value = accentToggleFill(look, disabled ? State::Disabled : State::Rest);
    const Color track = disabled ? c.controlStrongStrokeDisabled : c.controlStrongStroke;
    md::fillRoundRect(dl, {x0, cy - th * 0.5f}, {x1, cy + th * 0.5f}, th * 0.5f, track);
    if (hx > x0) md::fillRoundRect(dl, {x0, cy - th * 0.5f}, {hx, cy + th * 0.5f}, th * 0.5f, value);
    // Only whole steps inside the range (range 1, step 0.4: 0, 0.4, 0.8), as md's ticks.
    const float tw = std::max(1.0f, std::round(dp(1)));
    for (int i = 0, stops = md::sliderStops(range); i <= stops; ++i) {
        const float sx = std::round(x0 + (x1 - x0) * md::sliderStopAt(range, i) - tw * 0.5f);
        md::vLine(dl, sx, cy + r - dp(tickDp), cy + r, track, tw);
    }

    // The thumb: the solid fill with the elevation border, and the accent dot.
    const float stroke = strokeWidth(look);
    md::fillCircle(dl, {hx, cy}, r, solidControlFill(look));
    if (stroke > 0) md::strokeCircle(dl, {hx, cy}, r, c.controlStroke, stroke);
    const float dot = dragging || state == State::Pressed ? dotPressedDp : state == State::Hover ? dotHoverDp : dotDp;
    // Animated in dp, so a zoom or a monitor change resizes the dot at once instead of animating it.
    const float dotR = dp(md::animate(item.id + 3, dot * 0.5f, 20.0f, md::pixelEpsilon));
    md::fillCircle(dl, {hx, cy}, dotR, accentToggleFill(look, state));
    if (item.focused) drawFocusRect(dl, p, {p.x + w, p.y + h}, controlRadius(look), look);

    if (dragging && valueFormat && *valueFormat) drawValueTip(look, hx, cy - r, *v, valueFormat);
    return changed;
}

} // namespace imlui::fluent
