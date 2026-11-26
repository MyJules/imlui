// OKLCH: perceptual lightness / chroma / hue, and conversion to and from sRGB.
export module imlui.color.oklch;

import std;

import imlui.color;

export namespace imlui {

struct Oklch {
    float l = 0; ///< perceived lightness 0..1
    float c = 0; ///< chroma, ~0..0.37
    float h = 0; ///< hue in degrees
};

/// The components are clamped to [0, 1] first (clampColor), so the result is finite for any input.
Oklch toOklch(Color color);

/// Converts back to sRGB, reducing chroma until the color fits the gamut. A non-finite lightness, chroma or hue (or a
/// negative chroma) reads as 0, so the result is always a valid color.
Color fromOklch(Oklch o, float alpha = 1.0f);

/// Material "tone" (CIE L*, 0..100) -> OKLab lightness, exact for greys.
float toneToOkL(float tone);

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr float pi = 3.14159265358979f;

float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float toGamma(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

struct Linear {
    float r, g, b;
};

Linear oklchToLinear(Oklch o) {
    float hr = o.h * pi / 180.0f;
    float a = o.c * std::cos(hr), b = o.c * std::sin(hr);
    float lc = o.l + 0.3963377774f * a + 0.2158037573f * b;
    float mc = o.l - 0.1055613458f * a - 0.0638541728f * b;
    float sc = o.l - 0.0894841775f * a - 1.2914855480f * b;
    float l = lc * lc * lc, m = mc * mc * mc, s = sc * sc * sc;
    return {+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

bool inGamut(Linear c) {
    constexpr float e = 1e-4f;
    return c.r >= -e && c.r <= 1 + e && c.g >= -e && c.g <= 1 + e && c.b >= -e && c.b <= 1 + e;
}

} // namespace

Oklch toOklch(Color color) {
    // Unclamped, a huge component overflows pow() in toLinear, and inf - inf makes the lightness NaN.
    color = clampColor(color);
    float r = toLinear(color.r), g = toLinear(color.g), b = toLinear(color.b);
    float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    float okL = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    float okA = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    float okB = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
    float h = std::atan2(okB, okA) * 180.0f / pi;
    if (h < 0) h += 360.0f;
    return {okL, std::sqrt(okA * okA + okB * okB), h};
}

Color fromOklch(Oklch o, float alpha) {
    o.l = clampUnit(o.l);
    if (!std::isfinite(o.c) || o.c < 0.0f) o.c = 0.0f;
    if (!std::isfinite(o.h)) o.h = 0.0f;
    Linear lin = oklchToLinear(o);
    if (!inGamut(lin)) {
        float lo = 0, hi = o.c;
        for (int i = 0; i < 24; ++i) {
            float mid = (lo + hi) * 0.5f;
            if (inGamut(oklchToLinear({o.l, mid, o.h}))) lo = mid;
            else hi = mid;
        }
        lin = oklchToLinear({o.l, lo, o.h});
    }
    auto g = [](float x) { return clampUnit(toGamma(clampUnit(x))); };
    return {g(lin.r), g(lin.g), g(lin.b), alpha};
}

float toneToOkL(float tone) {
    float t = std::clamp(tone, 0.0f, 100.0f);
    float fy = (t + 16.0f) / 116.0f;
    float y = t > 8.0f ? fy * fy * fy : t / 903.2963f;
    return std::cbrt(y);
}

} // namespace imlui
