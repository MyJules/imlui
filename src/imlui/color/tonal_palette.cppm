// Tonal palette: a hue + chroma pair from which any Material tone can be taken.
export module imlui.color.tonal_palette;

import imlui.color;
import imlui.color.oklch;

export namespace imlui {

/// A hue + chroma pair from which any tone can be taken (Material TonalPalette).
struct TonalPalette {
    float hue = 0;
    float chroma = 0;
    /// Color at Material tone `t` (0 = black .. 100 = white).
    [[nodiscard]] Color tone(float t) const;
};

} // namespace imlui

module :private;

namespace imlui {

Color TonalPalette::tone(float t) const { return fromOklch({toneToOkL(t), chroma, hue}); }

} // namespace imlui
