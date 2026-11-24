// Color: non-linear sRGB color type, blending and hex parsing/formatting.
export module imlui.color;

import std;

import imgui;

export namespace imlui {

/// Non-linear sRGB color, components in [0, 1].
struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    [[nodiscard]] constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
    [[nodiscard]] ImVec4 vec4() const;
    [[nodiscard]] ImU32 u32() const;
    static Color from(ImVec4 v);
    friend bool operator==(const Color&, const Color&) = default;
};

/// Linear interpolation in sRGB space (what Material "state layers" do).
constexpr Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

/// Paints `layer` over `base` at `opacity` (Material state layer).
constexpr Color overlay(Color base, Color layer, float opacity) {
    Color c = mix(base, layer, opacity);
    c.a = base.a;
    return c;
}

/// Parses "#RGB", "#RGBA", "#RRGGBB" or "#RRGGBBAA" (leading '#' optional).
std::optional<Color> parseHex(std::string_view s);

/// Formats as "#RRGGBB", or "#RRGGBBAA" when not opaque.
std::string toHex(Color c);

} // namespace imlui

module :private;

namespace imlui {

ImVec4 Color::vec4() const { return {r, g, b, a}; }
ImU32 Color::u32() const { return ImGui::ColorConvertFloat4ToU32(vec4()); }
Color Color::from(ImVec4 v) { return {v.x, v.y, v.z, v.w}; }

std::optional<Color> parseHex(std::string_view s) {
    if (!s.empty() && s.front() == '#') s.remove_prefix(1);
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::uint32_t v[8]{};
    if (s.size() != 3 && s.size() != 4 && s.size() != 6 && s.size() != 8) return std::nullopt;
    for (std::size_t i = 0; i < s.size(); ++i) {
        int n = nib(s[i]);
        if (n < 0) return std::nullopt;
        v[i] = static_cast<std::uint32_t>(n);
    }
    auto f = [](std::uint32_t x) { return static_cast<float>(x) / 255.0f; };
    if (s.size() <= 4) {
        Color c{f(v[0] * 17), f(v[1] * 17), f(v[2] * 17), 1};
        if (s.size() == 4) c.a = f(v[3] * 17);
        return c;
    }
    Color c{f(v[0] * 16 + v[1]), f(v[2] * 16 + v[3]), f(v[4] * 16 + v[5]), 1};
    if (s.size() == 8) c.a = f(v[6] * 16 + v[7]);
    return c;
}

std::string toHex(Color c) {
    auto b = [](float x) { return static_cast<int>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
    char buf[16];
    if (c.a >= 0.999f) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", b(c.r), b(c.g), b(c.b));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", b(c.r), b(c.g), b(c.b), b(c.a));
    return buf;
}

} // namespace imlui
