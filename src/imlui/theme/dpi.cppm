// DPI scale: the factor from density-independent pixels (dp) to framebuffer pixels.
export module imlui.theme.dpi;

export namespace imlui {

/// Current dp -> pixel factor (monitor content scale).
[[nodiscard]] float dpiScale();

/// Sets the dp -> pixel factor. Call before loading fonts / applying the theme.
void setDpiScale(float scale);

/// Converts `v` dp to pixels.
[[nodiscard]] float dp(float v);

} // namespace imlui

module :private;

namespace imlui {

namespace {
float currentScale = 1.0f;
} // namespace

float dpiScale() { return currentScale; }
void setDpiScale(float scale) { currentScale = scale; }
float dp(float v) { return v * currentScale; }

} // namespace imlui
