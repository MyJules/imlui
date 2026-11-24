// Shared property readers for element handlers.
module;

#include <string>
#include <string_view>

export module imlui.ui.props;

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

} // namespace imlui
