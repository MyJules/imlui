// Tests for imlui.color, imlui.color.oklch and imlui.color.tonal_palette.
#include <gtest/gtest.h>

import std;

import imlui.color;
import imlui.color.oklch;
import imlui.color.tonal_palette;

using namespace imlui;

namespace {

constexpr float byteStep = 1.0f / 255.0f;

void expectColorNear(Color actual, Color expected, float tolerance) {
    EXPECT_NEAR(actual.r, expected.r, tolerance);
    EXPECT_NEAR(actual.g, expected.g, tolerance);
    EXPECT_NEAR(actual.b, expected.b, tolerance);
    EXPECT_NEAR(actual.a, expected.a, tolerance);
}

float byte(int v) { return static_cast<float>(v) / 255.0f; }

} // namespace

// ------------------------------------------------------------------ parseHex

TEST(ParseHex, SixDigitsWithHash) {
    auto c = parseHex("#FF8000");
    ASSERT_TRUE(c.has_value());
    expectColorNear(*c, {1.0f, byte(0x80), 0.0f, 1.0f}, 1e-6f);
}

// The '#' is required: a word made of hex digits ("face", "add") is no color.
TEST(ParseHex, RejectsHexWithoutHash) {
    for (const char* bare : {"336699", "face", "add", "abc8", "11223380"})
        EXPECT_FALSE(parseHex(bare).has_value()) << "input: '" << bare << "'";
}

TEST(ParseHex, EightDigitsCarryAlpha) {
    auto c = parseHex("#11223380");
    ASSERT_TRUE(c.has_value());
    expectColorNear(*c, {byte(0x11), byte(0x22), byte(0x33), byte(0x80)}, 1e-6f);
}

TEST(ParseHex, ThreeDigitsExpandEachNibble) {
    auto c = parseHex("#F0A");
    ASSERT_TRUE(c.has_value());
    expectColorNear(*c, {1.0f, 0.0f, byte(0xAA), 1.0f}, 1e-6f);
}

TEST(ParseHex, FourDigitsExpandAlpha) {
    auto c = parseHex("#abc8");
    ASSERT_TRUE(c.has_value());
    expectColorNear(*c, {byte(0xAA), byte(0xBB), byte(0xCC), byte(0x88)}, 1e-6f);
}

TEST(ParseHex, MixedCase) {
    auto upper = parseHex("#ABCDEF");
    auto lower = parseHex("#abcdef");
    ASSERT_TRUE(upper.has_value());
    ASSERT_TRUE(lower.has_value());
    EXPECT_EQ(*upper, *lower);
}

TEST(ParseHex, RejectsInvalidInput) {
    for (const char* bad : {"", "#", "#12", "#12345", "#1234567", "#123456789", "#GG0000", "#12 456", "red", "##123456",
                            "0x123456"})
        EXPECT_FALSE(parseHex(bad).has_value()) << "input: '" << bad << "'";
}

// ------------------------------------------------------------------ toHex

TEST(ToHex, OpaqueHasSixDigits) { EXPECT_EQ(toHex({1.0f, 0.0f, byte(0x80), 1.0f}), "#FF0080"); }

TEST(ToHex, TranslucentHasEightDigits) { EXPECT_EQ(toHex({0.0f, 0.0f, 0.0f, byte(0x40)}), "#00000040"); }

TEST(ToHex, ClampsOutOfRange) { EXPECT_EQ(toHex({-0.5f, 2.0f, 0.5f, 1.0f}), "#00FF80"); }

// NaN fails every comparison, so std::clamp passes it through, and lround(NaN) is unspecified: toHex reads it as 0.
TEST(ToHex, NonFiniteComponentsGiveAValidHex) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(toHex({nan, inf, -inf, 1.0f}), "#00FF00");
    EXPECT_EQ(toHex({0.0f, 0.0f, 0.0f, nan}), "#00000000");
    EXPECT_EQ(toHex(mix({0, 0, 0, 1}, {0, 0, 0, 1}, inf)), "#00000000") << "0 * inf is NaN in every component";
}

TEST(ClampColor, ClampsToUnitAndNanToZero) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(clampColor({nan, inf, -inf, 2.0f}), (Color{0, 1, 0, 1}));
    EXPECT_EQ(clampColor({0.25f, -0.5f, 1.5f, 0.5f}), (Color{0.25f, 0, 1, 0.5f}));
    static_assert(clampUnit(2.0f) == 1.0f && clampUnit(-1.0f) == 0.0f && clampUnit(0.5f) == 0.5f);
}

TEST(ToHex, RoundTripsThroughParseHex) {
    for (const char* hex : {"#000000", "#FFFFFF", "#6750A4", "#006A6A", "#B3261E", "#12345678", "#FEDCBA01"}) {
        auto c = parseHex(hex);
        ASSERT_TRUE(c.has_value()) << hex;
        EXPECT_EQ(toHex(*c), hex);
    }
}

// ------------------------------------------------------------------ mix / overlay

TEST(Mix, InterpolatesAllChannels) {
    Color a{0.0f, 0.2f, 1.0f, 0.0f};
    Color b{1.0f, 0.4f, 0.0f, 1.0f};
    EXPECT_EQ(mix(a, b, 0.0f), a);
    EXPECT_EQ(mix(a, b, 1.0f), b);
    expectColorNear(mix(a, b, 0.25f), {0.25f, 0.25f, 0.75f, 0.25f}, 1e-6f);
}

TEST(Mix, IsConstexpr) {
    constexpr Color c = mix({0, 0, 0, 0}, {1, 1, 1, 1}, 0.5f);
    static_assert(c.r == 0.5f && c.a == 0.5f);
    SUCCEED();
}

TEST(Overlay, KeepsBaseAlpha) {
    Color base{1.0f, 1.0f, 1.0f, 0.5f};
    Color layer{0.0f, 0.0f, 0.0f, 1.0f};
    expectColorNear(overlay(base, layer, 0.12f), {0.88f, 0.88f, 0.88f, 0.5f}, 1e-6f);
}

TEST(Overlay, ZeroOpacityIsBase) {
    Color base{0.1f, 0.2f, 0.3f, 0.4f};
    EXPECT_EQ(overlay(base, {1, 0, 0, 1}, 0.0f), base);
}

TEST(ColorType, WithAlphaReplacesAlphaOnly) {
    Color c = Color{0.1f, 0.2f, 0.3f, 1.0f}.withAlpha(0.25f);
    EXPECT_EQ(c, (Color{0.1f, 0.2f, 0.3f, 0.25f}));
}

// ------------------------------------------------------------------ OKLCH

TEST(Oklch, WhiteAndBlackAreAchromatic) {
    Oklch white = toOklch({1, 1, 1, 1});
    EXPECT_NEAR(white.l, 1.0f, 1e-3f);
    EXPECT_NEAR(white.c, 0.0f, 1e-3f);
    Oklch black = toOklch({0, 0, 0, 1});
    EXPECT_NEAR(black.l, 0.0f, 1e-3f);
    EXPECT_NEAR(black.c, 0.0f, 1e-3f);
}

TEST(Oklch, KnownValueForSrgbRed) {
    // Reference: sRGB red is oklch(0.628 0.2577 29.23).
    Oklch red = toOklch({1, 0, 0, 1});
    EXPECT_NEAR(red.l, 0.628f, 2e-3f);
    EXPECT_NEAR(red.c, 0.2577f, 2e-3f);
    EXPECT_NEAR(red.h, 29.23f, 0.5f);
}

TEST(Oklch, HueIsInZeroTo360) {
    for (const char* hex : {"#0000FF", "#FF00FF", "#00FF00", "#6750A4", "#123456"}) {
        Oklch o = toOklch(*parseHex(hex));
        EXPECT_GE(o.h, 0.0f) << hex;
        EXPECT_LT(o.h, 360.0f) << hex;
    }
}

TEST(Oklch, SrgbRoundTrip) {
    for (int r = 0; r <= 255; r += 51)
        for (int g = 0; g <= 255; g += 51)
            for (int b = 0; b <= 255; b += 51) {
                Color in{byte(r), byte(g), byte(b), 1.0f};
                expectColorNear(fromOklch(toOklch(in)), in, byteStep * 0.5f);
            }
}

TEST(Oklch, FromOklchPassesAlpha) { EXPECT_FLOAT_EQ(fromOklch({0.5f, 0.05f, 120.0f}, 0.3f).a, 0.3f); }

TEST(Oklch, OutOfGamutChromaIsReduced) {
    // Chroma 0.4 at mid lightness is outside sRGB for every hue.
    for (float hue : {0.0f, 90.0f, 180.0f, 270.0f}) {
        Oklch wanted{0.6f, 0.4f, hue};
        Color c = fromOklch(wanted);
        for (float channel : {c.r, c.g, c.b}) {
            EXPECT_GE(channel, 0.0f);
            EXPECT_LE(channel, 1.0f);
        }
        Oklch got = toOklch(c);
        EXPECT_LT(got.c, wanted.c) << "hue " << hue;
        EXPECT_GT(got.c, 0.0f) << "hue " << hue;
        // Lightness is kept while chroma is reduced.
        EXPECT_NEAR(got.l, wanted.l, 0.01f) << "hue " << hue;
    }
}

TEST(Oklch, InGamutColorIsKept) {
    Oklch o = toOklch(*parseHex("#6750A4"));
    Oklch back = toOklch(fromOklch(o));
    EXPECT_NEAR(back.l, o.l, 1e-3f);
    EXPECT_NEAR(back.c, o.c, 1e-3f);
    EXPECT_NEAR(back.h, o.h, 0.5f);
}

// A component beyond ~1.6e16 overflowed pow() in the sRGB-to-linear step (inf), and inf - inf made the lightness NaN,
// which no clamp downstream caught. toOklch clamps its input; fromOklch reads non-finite values as 0.
TEST(Oklch, NonFiniteInputGivesFiniteResults) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto finite = [](Color c) {
        return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
    };
    for (const Color c :
         {Color{inf, 0, 0, 1}, Color{1e30f, 1e30f, 1e30f, 1}, Color{nan, nan, nan, 1}, Color{-inf, 0.5f, inf, 1}}) {
        const Oklch o = toOklch(c);
        EXPECT_TRUE(std::isfinite(o.l) && std::isfinite(o.c) && std::isfinite(o.h)) << o.l << " " << o.c << " " << o.h;
        const Oklch clamped = toOklch(clampColor(c));
        EXPECT_TRUE(o.l == clamped.l && o.c == clamped.c && o.h == clamped.h) << "the clamped color's";
    }
    for (const Oklch o : {Oklch{nan, 0.1f, 30}, Oklch{0.5f, nan, 30}, Oklch{0.5f, 0.1f, nan}, Oklch{0.5f, inf, 30},
                          Oklch{0.5f, 0.1f, inf}, Oklch{inf, 0.1f, 30}, Oklch{0.5f, -1.0f, 30}, Oklch{0.5f, 1e30f, 30}})
        EXPECT_TRUE(finite(fromOklch(o))) << o.l << " " << o.c << " " << o.h;
    EXPECT_EQ(fromOklch({nan, 0, 0}), (Color{0, 0, 0, 1}));
}

// ------------------------------------------------------------------ tones

TEST(ToneToOkL, Endpoints) {
    EXPECT_NEAR(toneToOkL(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(toneToOkL(100.0f), 1.0f, 1e-4f);
}

TEST(ToneToOkL, IsMonotonic) {
    float previous = toneToOkL(0.0f);
    for (int i = 1; i <= 1000; ++i) {
        float tone = static_cast<float>(i) * 0.1f;
        float current = toneToOkL(tone);
        EXPECT_GT(current, previous) << "tone " << tone;
        previous = current;
    }
}

TEST(ToneToOkL, ClampsOutOfRange) {
    EXPECT_FLOAT_EQ(toneToOkL(-10.0f), toneToOkL(0.0f));
    EXPECT_FLOAT_EQ(toneToOkL(150.0f), toneToOkL(100.0f));
}

TEST(TonalPalette, GreyToneMatchesCieLightness) {
    // Tone 50 is CIE L* 50, which is sRGB #777777.
    Color c = TonalPalette{0.0f, 0.0f}.tone(50.0f);
    EXPECT_NEAR(c.r, c.g, 1e-4f);
    EXPECT_NEAR(c.g, c.b, 1e-4f);
    EXPECT_NEAR(c.r, byte(0x77), 2.0f * byteStep);
}

TEST(TonalPalette, ToneUsesPaletteHueAndChroma) {
    TonalPalette p{200.0f, 0.08f};
    Oklch o = toOklch(p.tone(60.0f));
    EXPECT_NEAR(o.l, toneToOkL(60.0f), 2e-3f);
    EXPECT_NEAR(o.c, 0.08f, 2e-3f);
    EXPECT_NEAR(o.h, 200.0f, 1.0f);
}
