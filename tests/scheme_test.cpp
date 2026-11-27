// Tests for imlui.theme.scheme: Material 3 scheme generation and role lookup.
#include <gtest/gtest.h>

import std;

import imlui.color;
import imlui.theme.scheme;

using namespace imlui;

namespace {

/// Fifth root by Newton's method (keeps the test free of <cmath>).
double fifthRoot(double x) {
    if (x <= 0) return 0;
    double y = x > 1 ? x : 1;
    for (int i = 0; i < 100; ++i) y -= (y - x / (y * y * y * y)) / 5.0;
    return y;
}

/// sRGB transfer function: x^2.4 is x^2 * (x^2)^(1/5).
double toLinear(double c) {
    if (c <= 0.04045) return c / 12.92;
    double x = (c + 0.055) / 1.055;
    return x * x * fifthRoot(x * x);
}

/// WCAG 2 relative luminance.
double luminance(Color c) {
    return 0.2126 * toLinear(static_cast<double>(c.r)) + 0.7152 * toLinear(static_cast<double>(c.g)) +
           0.0722 * toLinear(static_cast<double>(c.b));
}

/// WCAG 2 contrast ratio, 1..21.
double contrast(Color a, Color b) {
    double la = luminance(a), lb = luminance(b);
    if (la < lb) {
        double t = la;
        la = lb;
        lb = t;
    }
    return (la + 0.05) / (lb + 0.05);
}

struct RolePair {
    const char* role;
    const char* onRole;
};

constexpr RolePair contrastPairs[] = {
    {"primary", "onPrimary"},
    {"primaryContainer", "onPrimaryContainer"},
    {"secondary", "onSecondary"},
    {"secondaryContainer", "onSecondaryContainer"},
    {"tertiary", "onTertiary"},
    {"tertiaryContainer", "onTertiaryContainer"},
    {"error", "onError"},
    {"errorContainer", "onErrorContainer"},
    {"surface", "onSurface"},
    {"surfaceVariant", "onSurfaceVariant"},
    {"inverseSurface", "inverseOnSurface"},
};

constexpr const char* seeds[] = {"#6750A4", "#006A6A", "#B3261E", "#FFD600", "#00C853", "#2962FF",
                                 "#FF6D00", "#808080", "#000000", "#FFFFFF"};

} // namespace

TEST(SchemeContrast, TestHelperMatchesWcag) {
    EXPECT_NEAR(contrast({0, 0, 0, 1}, {1, 1, 1, 1}), 21.0, 1e-6);
    EXPECT_NEAR(contrast({1, 1, 1, 1}, {1, 1, 1, 1}), 1.0, 1e-6);
    // #777777 on white is the classic 4.48:1.
    Color grey = *parseHex("#777777");
    EXPECT_NEAR(contrast(grey, {1, 1, 1, 1}), 4.48, 0.01);
}

class SchemeForSeed : public ::testing::TestWithParam<const char*> {};

TEST_P(SchemeForSeed, OnRolesHaveAaContrast) {
    Color seed = *parseHex(GetParam());
    for (bool dark : {false, true}) {
        Scheme s = generateScheme(seed, dark);
        for (const auto& pair : contrastPairs) {
            const Color* base = findRole(s, pair.role);
            const Color* on = findRole(s, pair.onRole);
            ASSERT_NE(base, nullptr) << pair.role;
            ASSERT_NE(on, nullptr) << pair.onRole;
            EXPECT_GE(contrast(*base, *on), 4.5)
                << pair.role << "/" << pair.onRole << (dark ? " (dark)" : " (light)") << ": " << toHex(*base)
                << " vs " << toHex(*on);
        }
    }
}

TEST_P(SchemeForSeed, DarkSurfacesAreDarkerThanLight) {
    Color seed = *parseHex(GetParam());
    Scheme light = generateScheme(seed, false);
    Scheme dark = generateScheme(seed, true);
    for (const char* role : {"surface", "surfaceDim", "surfaceBright", "surfaceContainerLowest", "surfaceContainerLow",
                             "surfaceContainer", "surfaceContainerHigh", "surfaceContainerHighest", "surfaceVariant"}) {
        EXPECT_LT(luminance(*findRole(dark, role)), luminance(*findRole(light, role))) << role;
    }
    EXPECT_GT(luminance(dark.onSurface), luminance(light.onSurface));
}

TEST_P(SchemeForSeed, SurfaceContainersAreOrderedByElevation) {
    Color seed = *parseHex(GetParam());
    Scheme light = generateScheme(seed, false);
    EXPECT_GT(luminance(light.surfaceContainerLowest), luminance(light.surfaceContainerLow));
    EXPECT_GT(luminance(light.surfaceContainerLow), luminance(light.surfaceContainer));
    EXPECT_GT(luminance(light.surfaceContainer), luminance(light.surfaceContainerHigh));
    EXPECT_GT(luminance(light.surfaceContainerHigh), luminance(light.surfaceContainerHighest));
    Scheme dark = generateScheme(seed, true);
    EXPECT_LT(luminance(dark.surfaceContainerLowest), luminance(dark.surfaceContainerLow));
    EXPECT_LT(luminance(dark.surfaceContainerLow), luminance(dark.surfaceContainer));
    EXPECT_LT(luminance(dark.surfaceContainer), luminance(dark.surfaceContainerHigh));
    EXPECT_LT(luminance(dark.surfaceContainerHigh), luminance(dark.surfaceContainerHighest));
}

TEST_P(SchemeForSeed, ColorsAreOpaqueAndInRange) {
    Color seed = *parseHex(GetParam());
    for (bool dark : {false, true}) {
        Scheme s = generateScheme(seed, dark);
        for (const auto& role : roles()) {
            Color c = s.*role.member;
            for (float channel : {c.r, c.g, c.b}) {
                EXPECT_GE(channel, 0.0f) << role.name;
                EXPECT_LE(channel, 1.0f) << role.name;
            }
            EXPECT_FLOAT_EQ(c.a, 1.0f) << role.name;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Seeds, SchemeForSeed, ::testing::ValuesIn(seeds));

// An infinite or NaN seed component (from a script: ui.theme { seed = { math.huge, 0, 0 } }) used to make every role
// NaN, which reached ImGui's float-to-byte conversion. It reads as the clamped color.
TEST(GenerateScheme, NonFiniteSeedGivesTheClampedSeedsScheme) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const bool dark : {false, true}) {
        const Scheme s = generateScheme({inf, 0, 0, 1}, dark);
        const Scheme red = generateScheme({1, 0, 0, 1}, dark);
        for (const auto& role : roles()) {
            const Color c = s.*role.member;
            for (const float channel : {c.r, c.g, c.b, c.a})
                EXPECT_TRUE(std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f) << role.name;
            EXPECT_EQ(c, red.*role.member) << role.name;
        }
        const Scheme grey = generateScheme({nan, nan, nan, 1}, dark);
        EXPECT_EQ(grey.primary, generateScheme({0, 0, 0, 1}, dark).primary) << "NaN reads as 0";
    }
}

TEST(GenerateScheme, LightAndDarkDiffer) {
    Color seed = *parseHex("#6750A4");
    Scheme light = generateScheme(seed, false);
    Scheme dark = generateScheme(seed, true);
    EXPECT_NE(light.primary, dark.primary);
    EXPECT_NE(light.surface, dark.surface);
    // inversePrimary of one mode is the primary tone of the other.
    EXPECT_EQ(light.inversePrimary, dark.primary);
    EXPECT_EQ(dark.inversePrimary, light.primary);
}

TEST(GenerateScheme, IsDeterministic) {
    Color seed = *parseHex("#006A6A");
    Scheme a = generateScheme(seed, true);
    Scheme b = generateScheme(seed, true);
    for (const auto& role : roles()) EXPECT_EQ(a.*role.member, b.*role.member) << role.name;
}

TEST(GenerateScheme, ScrimAndShadowAreBlack) {
    Scheme s = generateScheme(*parseHex("#6750A4"), false);
    EXPECT_EQ(s.scrim, (Color{0, 0, 0, 1}));
    EXPECT_EQ(s.shadow, (Color{0, 0, 0, 1}));
}

TEST(Roles, CoverEverySchemeMember) {
    // Scheme is nothing but Colors, and roles() lists each one once.
    EXPECT_EQ(roles().size(), sizeof(Scheme) / sizeof(Color));
    for (std::size_t i = 0; i < roles().size(); ++i)
        for (std::size_t j = i + 1; j < roles().size(); ++j) {
            EXPECT_NE(roles()[i].name, roles()[j].name);
            EXPECT_NE(roles()[i].member, roles()[j].member);
        }
}

TEST(FindRole, FindsEveryRole) {
    Scheme s = generateScheme(*parseHex("#6750A4"), false);
    const Scheme& constScheme = s;
    for (const auto& role : roles()) {
        Color* found = findRole(s, role.name);
        ASSERT_NE(found, nullptr) << role.name;
        EXPECT_EQ(found, &(s.*role.member)) << role.name;
        EXPECT_EQ(findRole(constScheme, role.name), &(s.*role.member)) << role.name;
    }
}

TEST(FindRole, UnknownNameIsNull) {
    Scheme s{};
    EXPECT_EQ(findRole(s, "notARole"), nullptr);
    EXPECT_EQ(findRole(s, ""), nullptr);
    EXPECT_EQ(findRole(s, "Primary"), nullptr); // case-sensitive
}

// findRole() searches the names sorted: a prefix, a longer name or a name between two roles isn't one.
TEST(FindRole, OnlyWholeNamesMatch) {
    Scheme s{};
    for (const char* name : {"a", "zzz", "primaryContaine", "primaryContainers", "surfaceContainerHighestX", "on",
                             "onSurfaceVariant2", "outlineVariant "})
        EXPECT_EQ(findRole(s, name), nullptr) << name;
    EXPECT_EQ(findRole(s, "scrim"), &s.scrim);
    EXPECT_EQ(findRole(s, "errorContainer"), &s.errorContainer);
}

// generateScheme() keeps its last few results (fewer than `seeds`): whatever the order seeds come in, each scheme
// is the one a seed gives the first time.
TEST(GenerateScheme, CachedResultsMatchFreshOnes) {
    std::vector<Scheme> first;
    for (const char* hex : seeds)
        for (const bool dark : {false, true})
            first.push_back(generateScheme(*parseHex(hex), dark));
    for (int round = 0; round < 3; ++round) {
        std::size_t i = 0;
        for (const char* hex : seeds)
            for (const bool dark : {false, true}) {
                const Scheme again = generateScheme(*parseHex(hex), dark);
                for (const auto& role : roles())
                    EXPECT_EQ(again.*role.member, first[i].*role.member) << hex << " " << dark << " " << role.name;
                ++i;
            }
    }
    EXPECT_NE(generateScheme(*parseHex("#6750A4"), true).primary, generateScheme(*parseHex("#6750A4"), false).primary);
}

TEST(FindRole, WritesThroughToScheme) {
    Scheme s{};
    *findRole(s, "tertiaryContainer") = {0.25f, 0.5f, 0.75f, 1.0f};
    EXPECT_EQ(s.tertiaryContainer, (Color{0.25f, 0.5f, 0.75f, 1.0f}));
}

// A grey has no hue: the one OKLCH gives it is rounding noise, and used to pick a colored primary (a different one
// for each grey). A grey seed gives a grey scheme; only the error roles keep their red.
TEST(SchemeGreySeed, GivesAGreyScheme) {
    for (const char* hex : {"#000000", "#404040", "#808080", "#C0C0C0", "#FFFFFF"}) {
        for (bool dark : {false, true}) {
            const Scheme s = generateScheme(*parseHex(hex), dark);
            for (const auto& role : roles()) {
                if (role.name.starts_with("error") || role.name.starts_with("onError")) continue;
                const Color c = s.*role.member;
                EXPECT_NEAR(c.r, c.g, 1e-3f) << hex << " " << role.name;
                EXPECT_NEAR(c.g, c.b, 1e-3f) << hex << " " << role.name;
            }
            EXPECT_GT(s.error.r, s.error.b + 0.2f) << hex;
        }
    }
    // A color one step off grey still has a hue of its own.
    const Scheme bluish = generateScheme(*parseHex("#808081"), false);
    EXPECT_GT(bluish.primary.b, bluish.primary.r + 0.1f);
}
