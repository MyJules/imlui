// Material slider with value label while dragging.
export module imlui.widgets.slider;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.log;
import imlui.widgets.common;
import imlui.widgets.value_format;

export namespace imlui::md {

/// Material slider. `step` <= 0 means continuous. `valueFormat` is checked with isValidFloatFormat(); an invalid
/// one is reported once and replaced by defaultValueFormat. Returns true when the user changed the value; `*v`
/// is written only then, so a value the user didn't touch keeps its exact double (0.1 stays 0.1). A stepped value
/// is `vmin + k * step` without binary noise (0.1 * 3 gives 0.3, not 0.30000000000000004), and no more than the
/// last whole step in the range (range 1, step 0.4: at most 0.8). A range that isn't finite (an infinite or NaN
/// bound, or bounds too far apart for a double) is empty, like vmin >= vmax: the value is left alone. A step that
/// isn't finite is continuous. `width` follows resolveSize (itemWidth): 0 is the natural 200dp, -1 fills (the
/// Slider element passes -1 when unset).
bool slider(const char* id, double* v, double vmin, double vmax, double step = 0.0, float width = 0.0f,
            const char* valueFormat = "%.2f");
/// The size of slider() with `width` (resolved like itemWidth(): 0 is the natural 200dp).
[[nodiscard]] ImVec2 sliderSize(float width);

/// slider()'s format check, shared with the other skins' sliders: `format` when isValidFloatFormat(), else
/// defaultValueFormat, an invalid format being reported once (whichever slider meets it first).
[[nodiscard]] const char* checkedSliderFormat(const char* format);
/// slider()'s snapping: `vmin + k * step` for the step nearest to `v` and at most `vmax`, rounded to 15 significant
/// digits. `step` > 0, the range finite and not empty.
[[nodiscard]] double snapToStep(double v, double vmin, double vmax, double step);

/// A slider's range and step as slider() reads them; the value logic below is every skin's slider's.
struct SliderRange {
    double vmin = 0, vmax = 0;
    double range = 0;    ///< vmax - vmin
    double step = 0;     ///< > 0, or 0: continuous (also for a step that isn't finite)
    bool ranged = false; ///< the range is finite and not empty; else the value is left alone
};
[[nodiscard]] SliderRange sliderRange(double vmin, double vmax, double step);
/// Moves `*v` to the value under `mouseX` on a track from `x0` to `x1`: snapped to the step, clamped to the range,
/// written only when it differs. Returns whether it did; nothing happens without a range.
bool sliderDragTo(double* v, const SliderRange& r, float mouseX, float x0, float x1);
/// Where `v` lies on the track, 0..1 (0 without a range).
[[nodiscard]] float sliderFraction(double v, const SliderRange& r);
/// The last tick: ticks 0..sliderStops() mark the whole steps inside the range (range 1, step 0.4: 0, 0.4, 0.8);
/// -1 when there are none (continuous, no range, or more than 40 steps).
[[nodiscard]] int sliderStops(const SliderRange& r);
/// Where tick `i` lies on the track, 0..1.
[[nodiscard]] float sliderStopAt(const SliderRange& r, int i);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

/// Formats already reported as invalid, so each is logged once rather than every frame.
std::unordered_set<std::string> reportedFormats;

} // namespace

const char* checkedSliderFormat(const char* format) {
    if (isValidFloatFormat(format)) return format;
    if (reportedFormats.emplace(format).second)
        log::warn("Slider: invalid format \"{}\" (expected one %f, %e or %g conversion); using \"{}\"", format,
                  defaultValueFormat);
    return defaultValueFormat;
}

/// `vmin + k * step` for the step nearest to `v` and at most `vmax`, rounded to 15 significant digits: that drops
/// the noise of the binary multiplication (0.1 * 3 = 0.30000000000000004) and keeps every value a step can mean.
double snapToStep(double v, double vmin, double vmax, double step) {
    // The last whole step in the range; the small slack keeps it when the division comes out just below a whole
    // number (1 / 0.1 = 9.999...), as for the ticks.
    const double last = std::floor((vmax - vmin) / step + 1e-6);
    const double snapped = vmin + std::clamp(std::round((v - vmin) / step), 0.0, last) * step;
    char buf[32] = {};
    if (std::snprintf(buf, sizeof buf, "%.15g", snapped) < 0) return snapped;
    return std::strtod(buf, nullptr);
}

SliderRange sliderRange(double vmin, double vmax, double step) {
    SliderRange r;
    r.vmin = vmin;
    r.vmax = vmax;
    // inf - inf is NaN and inf - x is inf: a finite, positive range means finite bounds too.
    r.range = vmax - vmin;
    r.ranged = std::isfinite(r.range) && r.range > 0;
    r.step = std::isfinite(step) && step > 0 ? step : 0.0;
    return r;
}

bool sliderDragTo(double* v, const SliderRange& r, float mouseX, float x0, float x1) {
    if (!r.ranged) return false;
    const float mt = std::clamp((mouseX - x0) / std::max(1.0f, x1 - x0), 0.0f, 1.0f);
    double nv = r.vmin + static_cast<double>(mt) * r.range;
    if (r.step > 0) nv = snapToStep(nv, r.vmin, r.vmax, r.step);
    nv = std::clamp(nv, r.vmin, r.vmax); // the last step's slack
    if (nv == *v) return false;
    *v = nv;
    return true;
}

float sliderFraction(double v, const SliderRange& r) {
    return r.ranged ? static_cast<float>(std::clamp((v - r.vmin) / r.range, 0.0, 1.0)) : 0.0f;
}

int sliderStops(const SliderRange& r) {
    if (!(r.step > 0 && r.ranged && r.range / r.step <= 40)) return -1;
    // The small slack keeps the last tick when the division comes out just below a whole number (1 / 0.1 = 9.999...).
    return static_cast<int>(std::floor(r.range / r.step + 1e-6));
}

float sliderStopAt(const SliderRange& r, int i) {
    return static_cast<float>(std::min(1.0, static_cast<double>(i) * r.step / r.range));
}

ImVec2 sliderSize(float width) { return {itemWidth(width, dp(200)), dp(44)}; }

bool slider(const char* id, double* v, double vmin, double vmax, double step, float width, const char* valueFormat) {
    const Scheme& s = scheme();
    const ImVec2 size = sliderSize(width);
    const float w = size.x, h = size.y;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool dis = itemDisabled();
    ImGuiID iid = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();

    const float r = dp(10);
    const float x0 = p.x + r, x1 = p.x + w - r;
    const SliderRange range = sliderRange(vmin, vmax, step);
    const bool changed = act && sliderDragTo(v, range, ImGui::GetMousePos().x, x0, x1);
    const float t = sliderFraction(*v, range);
    float tv = animate(iid + 2, t, 30.0f);

    Color active = dis ? s.onSurface.withAlpha(0.38f) : s.primary;
    Color inactive = dis ? s.onSurface.withAlpha(0.12f) : s.secondaryContainer;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f, th = dp(4);
    const float hx = x0 + (x1 - x0) * tv;
    fillRoundRect(dl, {x0, cy - th * 0.5f}, {x1, cy + th * 0.5f}, th * 0.5f, inactive);
    if (hx > x0) fillRoundRect(dl, {x0, cy - th * 0.5f}, {hx, cy + th * 0.5f}, th * 0.5f, active);
    for (int i = 0, stops = sliderStops(range); i <= stops; ++i) {
        const float sx = x0 + (x1 - x0) * sliderStopAt(range, i);
        fillCircle(dl, {sx, cy}, dp(1),
                   sx <= hx ? s.onPrimary.withAlpha(0.7f) : s.onSecondaryContainer.withAlpha(0.7f));
    }
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) fillCircle(dl, {hx, cy}, dp(20), s.primary.withAlpha(layer * 1.5f));
    fillCircle(dl, {hx, cy}, r * (act ? 1.1f : 1.0f), active);

    if (act && valueFormat && *valueFormat) {
        char buf[64] = {};
        const char* format = checkedSliderFormat(valueFormat);
        // The format is the Slider's `format` prop, chosen by the script, so it can't be a literal.
        // checkedSliderFormat() only lets through a format with exactly one floating conversion (and `%%`), which
        // matches the one double argument, so -Wformat-nonliteral is off for this call only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
        if (std::snprintf(buf, sizeof buf, format, *v) < 0) buf[0] = char{};
#pragma clang diagnostic pop
        ImVec2 ts = measure(TextStyle::LabelMedium, buf);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float bw = std::max(ts.x + dp(16), dp(28)), bh = dp(28);
        ImVec2 ba{hx - bw * 0.5f, cy - r - dp(8) - bh};
        fillRoundRect(fg, ba, {ba.x + bw, ba.y + bh}, bh * 0.5f, s.inverseSurface);
        drawText(fg, TextStyle::LabelMedium, {hx - ts.x * 0.5f, textTop(TextStyle::LabelMedium, ba.y + bh * 0.5f)},
                 s.inverseOnSurface, buf);
    }
    return changed;
}

} // namespace imlui::md
