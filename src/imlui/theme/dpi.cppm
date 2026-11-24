// DPI scale: the factor from density-independent pixels (dp) to framebuffer pixels.
//
// The effective scale is the monitor content scale (set by the engine, follows
// the window between monitors) times the user zoom (ui.setZoom, Ctrl +/-).
export module imlui.theme.dpi;

export namespace imlui {

/// Smallest and largest user zoom factor.
inline constexpr float minZoom = 0.5f;
inline constexpr float maxZoom = 3.0f;

/// Effective dp -> pixel factor: contentScale() * zoom().
[[nodiscard]] float dpiScale();

/// Monitor content scale (DPI factor) of the window.
[[nodiscard]] float contentScale();
/// Sets the monitor content scale. Non-positive values are treated as 1.
void setContentScale(float scale);

/// User zoom factor (1 = 100 %).
[[nodiscard]] float zoom();
/// Sets the user zoom, clamped to [minZoom, maxZoom]. NaN resets it to 1.
void setZoom(float z);

/// The zoom `steps` preset steps above (> 0) or below (< 0) `current`, as browsers
/// do it: 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300 %.
[[nodiscard]] float zoomStep(float current, int steps);

/// Converts `v` dp to pixels.
[[nodiscard]] float dp(float v);

} // namespace imlui

module :private;

namespace imlui {

namespace {
constexpr float zoomSteps[] = {0.5f, 0.67f, 0.75f, 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f};
constexpr int zoomStepCount = static_cast<int>(sizeof(zoomSteps) / sizeof(zoomSteps[0]));

float monitorScale = 1.0f;
float userZoom = 1.0f;
} // namespace

float dpiScale() { return monitorScale * userZoom; }

float contentScale() { return monitorScale; }
void setContentScale(float scale) { monitorScale = scale > 0.0f ? scale : 1.0f; }

float zoom() { return userZoom; }

void setZoom(float z) {
    if (!(z == z)) z = 1.0f; // NaN
    userZoom = z < minZoom ? minZoom : (z > maxZoom ? maxZoom : z);
}

float zoomStep(float current, int steps) {
    // `below`: the last preset <= current. Stepping up from between two presets
    // lands on the upper one first, stepping down on the lower one.
    constexpr float eps = 0.005f;
    int below = 0;
    while (below + 1 < zoomStepCount && zoomSteps[below + 1] <= current + eps)
        ++below;
    const bool onStep = current >= zoomSteps[below] - eps && current <= zoomSteps[below] + eps;
    int i = below;
    if (current < zoomSteps[0] - eps)
        i = steps > 0 ? steps - 1 : 0;
    else if (steps > 0)
        i = below + steps;
    else if (steps < 0)
        i = (onStep ? below : below + 1) + steps;
    i = i < 0 ? 0 : (i >= zoomStepCount ? zoomStepCount - 1 : i);
    return zoomSteps[i];
}

float dp(float v) { return v * dpiScale(); }

} // namespace imlui
