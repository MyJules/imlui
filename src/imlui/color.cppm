// Colors: sRGB color type, hex parsing/formatting, and OKLCH-based tonal
// palettes used to generate Material 3 style color schemes.
module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

export module imlui.color;

import imgui;

export namespace imlui {

/// Non-linear sRGB color, components in [0, 1].
struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    [[nodiscard]] constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
    [[nodiscard]] ImVec4 vec4() const { return {r, g, b, a}; }
    [[nodiscard]] ImU32 u32() const { return ImGui::ColorConvertFloat4ToU32(vec4()); }
    static Color from(ImVec4 v) { return {v.x, v.y, v.z, v.w}; }
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

// ------------------------------------------------------------------ OKLCH

struct Oklch {
    float L = 0; // perceived lightness 0..1
    float C = 0; // chroma, ~0..0.37
    float h = 0; // hue in degrees
};

namespace detail {
inline float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float toGamma(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }
inline constexpr float kPi = 3.14159265358979f;

struct Lin { float r, g, b; };

inline Lin oklchToLinear(Oklch o) {
    float hr = o.h * kPi / 180.0f;
    float a = o.C * std::cos(hr), b = o.C * std::sin(hr);
    float l_ = o.L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = o.L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = o.L - 0.0894841775f * a - 1.2914855480f * b;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return {+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}
inline bool inGamut(Lin c) {
    constexpr float e = 1e-4f;
    return c.r >= -e && c.r <= 1 + e && c.g >= -e && c.g <= 1 + e && c.b >= -e && c.b <= 1 + e;
}
} // namespace detail

Oklch toOklch(Color c) {
    using namespace detail;
    float r = toLinear(c.r), g = toLinear(c.g), b = toLinear(c.b);
    float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    float L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    float A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    float B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
    float h = std::atan2(B, A) * 180.0f / kPi;
    if (h < 0) h += 360.0f;
    return {L, std::sqrt(A * A + B * B), h};
}

/// Converts back to sRGB, reducing chroma until the color fits the gamut.
Color fromOklch(Oklch o, float alpha = 1.0f) {
    using namespace detail;
    o.L = std::clamp(o.L, 0.0f, 1.0f);
    Lin lin = oklchToLinear(o);
    if (!inGamut(lin)) {
        float lo = 0, hi = o.C;
        for (int i = 0; i < 24; ++i) {
            float mid = (lo + hi) * 0.5f;
            if (inGamut(oklchToLinear({o.L, mid, o.h}))) lo = mid; else hi = mid;
        }
        lin = oklchToLinear({o.L, lo, o.h});
    }
    auto g = [](float x) { return std::clamp(toGamma(std::clamp(x, 0.0f, 1.0f)), 0.0f, 1.0f); };
    return {g(lin.r), g(lin.g), g(lin.b), alpha};
}

/// Material "tone" (CIE L*, 0..100) -> OKLab lightness, exact for greys.
float toneToOkL(float tone) {
    float t = std::clamp(tone, 0.0f, 100.0f);
    float fy = (t + 16.0f) / 116.0f;
    float y = t > 8.0f ? fy * fy * fy : t / 903.2963f;
    return std::cbrt(y);
}

/// A hue + chroma pair from which any tone can be taken (Material TonalPalette).
struct TonalPalette {
    float hue = 0;
    float chroma = 0;
    [[nodiscard]] Color tone(float t) const { return fromOklch({toneToOkL(t), chroma, hue}); }
};

} // namespace imlui
