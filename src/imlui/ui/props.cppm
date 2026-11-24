// Shared property readers for element handlers.
export module imlui.ui.props;

import std;

import imlui.theme.dpi;
import imlui.lua.node;

export namespace imlui {

/// Reads a size property in dp and returns pixels: > 0 a fixed size, -1 "fill",
/// < -1 "fill minus |v| dp", 0 auto. Fixed and fill-minus sizes are scaled by dp();
/// -1 stays -1. For width keys, `fillWidth = true` means "fill" (-1).
[[nodiscard]] float sizeProp(const luaui::Node& node, const char* key);

/// "Label###id": the visible text may change every frame without changing the ImGui ID.
[[nodiscard]] std::string labelId(std::string_view text, const char* id);

/// "start" -> 0, "center" -> 0.5, "end" / "right" / "bottom" -> 1.
[[nodiscard]] float alignFactor(std::string_view align);

/// A 1-based index prop as an int that is safe to do arithmetic on: values below 1, NaN and -infinity become 0
/// (none), values past the int range (and +infinity) become the largest int.
[[nodiscard]] int indexValue(double index);
[[nodiscard]] int indexValue(int index);

/// A progress fraction for the widgets: clamped to [0, 1], 0 when not finite (`done / total` with total == 0).
[[nodiscard]] float progressFraction(double value);

} // namespace imlui

module :private;

namespace imlui {

float sizeProp(const luaui::Node& node, const char* key) {
    if (key[0] == 'w' && node.boolean("fillWidth", false)) return -1.0f;
    float v = node.numf(key, 0.0f);
    return (v > 0 || v < -1.0f) ? dp(v) : v;
}

std::string labelId(std::string_view text, const char* id) {
    std::string s{text};
    s += "###";
    s += id;
    return s;
}

float alignFactor(std::string_view align) {
    if (align == "center") return 0.5f;
    if (align == "end" || align == "right" || align == "bottom") return 1.0f;
    return 0.0f;
}

int indexValue(double index) {
    if (!(index >= 1.0)) return 0; // also NaN
    if (index >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    return static_cast<int>(index);
}

int indexValue(int index) { return std::max(index, 0); }

float progressFraction(double value) {
    if (!std::isfinite(value)) return 0.0f;
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

} // namespace imlui
