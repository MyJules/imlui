// resolveSize(): the one rule for size values (`width`, `height`, `size` props and the widths the Material widgets
// take), so every element fills, fills minus and falls back to its own size the same way.
module;

#include <algorithm>

export module imlui.ui.item_size;

export namespace imlui {

/// Resolves a size in pixels (as Frame::size() / sizeProp() return it) against `avail` pixels of available space:
///
/// - `> 0`: that fixed size;
/// - `0` (unset): `natural`, the element's own size;
/// - `-1` (any value from -1 up to 0): fill, `avail`;
/// - `< -1`: fill minus `|requested|`, `avail + requested`.
///
/// A fill result is at least 1 pixel.
[[nodiscard]] float resolveSize(float requested, float avail, float natural);

} // namespace imlui

module :private;

namespace imlui {

float resolveSize(float requested, float avail, float natural) {
    if (requested > 0.0f) return requested;
    if (requested == 0.0f) return natural;
    return std::max(1.0f, requested < -1.0f ? avail + requested : avail);
}

} // namespace imlui
