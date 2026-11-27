// Tests for imlui.lua.theme_table: building a Theme from a Lua table.
#include <gtest/gtest.h>

import std;
import lua;
import imgui;
import imlui.color;
import imlui.theme.scheme;
import imlui.theme.skin_tokens;
import imlui.theme.theme;
import imlui.lua.errors;
import imlui.lua.theme_table;

using namespace imlui;

namespace {

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

class ThemeTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_context = ImGui::CreateContext();
        m_state = lua::newstate();
        lua::openlibs(m_state);
        luaui::clearErrors();
    }

    void TearDown() override {
        lua::close(m_state);
        ImGui::DestroyContext(m_context);
        luaui::clearErrors();
    }

    /// Runs `code`, which returns a theme table, and parses it.
    Theme parse(std::string_view code) {
        EXPECT_EQ(lua::loadbuffer(m_state, code, "=theme"), lua::ok) << lua::tostring(m_state, -1);
        EXPECT_EQ(lua::pcall(m_state, 0, 1, 0), lua::ok) << lua::tostring(m_state, -1);
        int top = lua::gettop(m_state);
        Theme t = luaui::themeFromTable(m_state, -1);
        EXPECT_EQ(lua::gettop(m_state), top) << "themeFromTable left the stack unbalanced";
        lua::pop(m_state);
        return t;
    }

    /// True if some recorded error contains `part`.
    static bool hasError(std::string_view part) {
        for (const auto& e : luaui::errors())
            if (contains(e, part)) return true;
        return false;
    }

    ImGuiContext* m_context = nullptr;
    lua::State* m_state = nullptr;
};

} // namespace

TEST_F(ThemeTableTest, EmptyTableGivesDefaults) {
    Theme t = parse("return {}");
    Theme defaults;
    EXPECT_EQ(t.preset, defaults.preset);
    EXPECT_EQ(t.seed, defaults.seed);
    EXPECT_EQ(t.dark, defaults.dark);
    EXPECT_FLOAT_EQ(t.spacing, defaults.spacing);
    EXPECT_FLOAT_EQ(t.fontScale, defaults.fontScale);
    EXPECT_FLOAT_EQ(t.shape.medium, defaults.shape.medium);
    for (const auto& role : roles())
        EXPECT_EQ(t.scheme.*role.member, defaults.scheme.*role.member) << role.name;
    EXPECT_TRUE(t.imguiColors.empty());
    EXPECT_TRUE(t.style.empty());
    EXPECT_TRUE(t.tokens.empty());
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, ScalarFields) {
    Theme t =
        parse(R"(return { preset = "imgui-classic", seed = "#006A6A", dark = false, spacing = 6, fontScale = 1.25 })");
    EXPECT_EQ(t.preset, ThemePreset::ImguiClassic);
    EXPECT_EQ(t.seed, *parseHex("#006A6A"));
    EXPECT_FALSE(t.dark);
    EXPECT_FLOAT_EQ(t.spacing, 6.0f);
    EXPECT_FLOAT_EQ(t.fontScale, 1.25f);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, SchemeIsGeneratedFromSeedAndMode) {
    Theme t = parse(R"(return { seed = "#B3261E", dark = false })");
    Scheme expected = generateScheme(*parseHex("#B3261E"), false);
    for (const auto& role : roles())
        EXPECT_EQ(t.scheme.*role.member, expected.*role.member) << role.name;
}

TEST_F(ThemeTableTest, ShapeOverridesOnlyGivenCorners) {
    Theme t = parse("return { shape = { extraSmall = 1, small = 2, large = 20, extraLarge = 40 } }");
    Theme::Shape defaults;
    EXPECT_FLOAT_EQ(t.shape.extraSmall, 1.0f);
    EXPECT_FLOAT_EQ(t.shape.small, 2.0f);
    EXPECT_FLOAT_EQ(t.shape.medium, defaults.medium);
    EXPECT_FLOAT_EQ(t.shape.large, 20.0f);
    EXPECT_FLOAT_EQ(t.shape.extraLarge, 40.0f);
}

TEST_F(ThemeTableTest, ColorOverridesReplaceRoles) {
    Theme t = parse(R"(
        return { seed = "#6750A4", dark = true,
                 colors = { primary = "#FF0000", surface = { 0.1, 0.2, 0.3 }, onSurface = "#00FF0080" } })");
    Scheme generated = generateScheme(*parseHex("#6750A4"), true);
    EXPECT_EQ(t.scheme.primary, (Color{1, 0, 0, 1}));
    EXPECT_EQ(t.scheme.surface, (Color{0.1f, 0.2f, 0.3f, 1.0f}));
    EXPECT_EQ(t.scheme.onSurface, *parseHex("#00FF0080"));
    // Roles that are not overridden keep the generated colors.
    EXPECT_EQ(t.scheme.onPrimary, generated.onPrimary);
    EXPECT_EQ(t.scheme.secondary, generated.secondary);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, ImGuiColorOverrides) {
    Theme t = parse(R"(return { imgui = { WindowBg = "#102030", Text = { 1, 1, 1, 0.5 } } })");
    ASSERT_EQ(t.imguiColors.size(), 2u);
    bool sawWindowBg = false, sawText = false;
    for (const auto& [index, color] : t.imguiColors) {
        if (index == ImGuiCol_WindowBg) {
            sawWindowBg = true;
            EXPECT_EQ(color, *parseHex("#102030"));
        } else if (index == ImGuiCol_Text) {
            sawText = true;
            EXPECT_EQ(color, (Color{1, 1, 1, 0.5f}));
        }
    }
    EXPECT_TRUE(sawWindowBg);
    EXPECT_TRUE(sawText);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, StyleOverrides) {
    Theme t = parse("return { style = { FrameRounding = 6, ItemSpacing = { 8, 4 } } }");
    ASSERT_EQ(t.style.size(), 2u);
    bool sawRounding = false, sawSpacing = false;
    for (const auto& o : t.style) {
        if (o.name == "FrameRounding") {
            sawRounding = true;
            EXPECT_FLOAT_EQ(o.value.x, 6.0f);
            EXPECT_FLOAT_EQ(o.value.y, 6.0f);
        } else if (o.name == "ItemSpacing") {
            sawSpacing = true;
            EXPECT_FLOAT_EQ(o.value.x, 8.0f);
            EXPECT_FLOAT_EQ(o.value.y, 4.0f);
        }
    }
    EXPECT_TRUE(sawRounding);
    EXPECT_TRUE(sawSpacing);
    EXPECT_TRUE(luaui::errors().empty());
}

// Values ImGui asserts on, or that aren't numbers at all, are reported and left out instead of reaching ImGuiStyle.
TEST_F(ThemeTableTest, BadStyleValuesAreReported) {
    Theme t = parse(R"(return { style = {
        Alpha = 1.5, DisabledAlpha = -0.1, WindowMinSize = { 0, 0 }, ItemSpacing = { 8 }, FramePadding = "4",
        FrameRounding = 0/0, GrabMinSize = 1/0, IndentSpacing = 1e9, WindowPadding = { 4, 0/0 },
        FrameBorderSize = 1, CellPadding = { 2, 3 }, ScrollbarSize = { 12 },
    } })");
    ASSERT_EQ(t.style.size(), 2u);
    for (const char* error :
         {"theme.style.Alpha: must be in 0..1", "theme.style.DisabledAlpha: must be in 0..1",
          "theme.style.WindowMinSize: must be at least 1", "theme.style.ItemSpacing: expected a number or {x, y}",
          "theme.style.FramePadding: expected a number or {x, y}", "theme.style.FrameRounding: not a finite number",
          "theme.style.GrabMinSize: not a finite number", "theme.style.IndentSpacing: larger than 10000",
          "theme.style.WindowPadding: not a finite number", "theme.style.ScrollbarSize: expected a number or {x, y}"})
        EXPECT_TRUE(hasError(error)) << error;
    EXPECT_EQ(luaui::errors().size(), 10u);
}

// A number beyond the float range is narrowed like every other Lua number (toFloat), not by a cast (undefined
// behavior): it is reported as too large, and a NaN still as not finite.
TEST_F(ThemeTableTest, StyleValuesBeyondTheFloatRangeAreTooLarge) {
    Theme t = parse("return { style = { IndentSpacing = 1e300, ItemSpacing = { 4, -1e300 }, FrameRounding = -(0/0) } }");
    EXPECT_TRUE(t.style.empty());
    for (const char* error : {"theme.style.IndentSpacing: larger than 10000", "theme.style.ItemSpacing: larger than 10000",
                              "theme.style.FrameRounding: not a finite number"})
        EXPECT_TRUE(hasError(error)) << error;
    EXPECT_EQ(luaui::errors().size(), 3u);
}

TEST_F(ThemeTableTest, BadScalesAndSizesAreReported) {
    Theme t = parse(R"(return { fontScale = 100, spacing = -1, shape = { small = 0/0, large = "x", medium = 20 } })");
    const Theme defaults;
    EXPECT_FLOAT_EQ(t.fontScale, defaults.fontScale);
    EXPECT_FLOAT_EQ(t.spacing, defaults.spacing);
    EXPECT_FLOAT_EQ(t.shape.small, defaults.shape.small);
    EXPECT_FLOAT_EQ(t.shape.large, defaults.shape.large);
    EXPECT_FLOAT_EQ(t.shape.medium, 20.0f);
    EXPECT_TRUE(hasError("theme.fontScale: expected a number in 0.25..4"));
    EXPECT_TRUE(hasError("theme.spacing: expected a number in 0..10000"));
    EXPECT_TRUE(hasError("theme.shape.small: expected a number in 0..10000"));
    EXPECT_TRUE(hasError("theme.shape.large: expected a number in 0..10000"));
    luaui::clearErrors();
    t = parse("return { fontScale = 0/0 }");
    EXPECT_FLOAT_EQ(t.fontScale, defaults.fontScale);
    EXPECT_TRUE(hasError("theme.fontScale"));
}

// A Theme built in C++ goes through the same limits when it's applied: ImGui's style checks pass with it.
TEST_F(ThemeTableTest, SetThemeBringsValuesIntoRange) {
    Theme t;
    t.fontScale = 1000.0f;
    t.spacing = std::numeric_limits<float>::quiet_NaN();
    t.style = {{"Alpha", {1.5f, 1.5f}}, {"WindowMinSize", {8.0f, 0.0f}}, {"FrameRounding", {6.0f, 6.0f}}};
    setTheme(t);
    ASSERT_TRUE(applyPendingTheme());
    EXPECT_FLOAT_EQ(theme().fontScale, maxFontScale);
    EXPECT_FLOAT_EQ(theme().spacing, Theme{}.spacing);
    ASSERT_EQ(theme().style.size(), 1u);
    EXPECT_EQ(theme().style[0].name, "FrameRounding");
    const ImGuiStyle& st = ImGui::GetStyle();
    EXPECT_GE(st.Alpha, 0.0f);
    EXPECT_LE(st.Alpha, 1.0f);
    EXPECT_GE(st.WindowMinSize.y, 1.0f);
    setTheme(Theme{});
    applyPendingTheme();
}

TEST_F(ThemeTableTest, UnknownRoleIsReported) {
    Theme t = parse(R"(return { colors = { notARole = "#FFFFFF", primary = "#00FF00" } })");
    EXPECT_TRUE(hasError("unknown role 'notARole'"));
    EXPECT_EQ(luaui::errors().size(), 1u);
    EXPECT_EQ(t.scheme.primary, (Color{0, 1, 0, 1})); // valid entries still apply
}

TEST_F(ThemeTableTest, InvalidRoleColorIsReported) {
    Theme t = parse(R"(return { colors = { primary = "#XYZ", secondary = 42 } })");
    EXPECT_TRUE(hasError("theme.colors.primary: invalid color"));
    EXPECT_TRUE(hasError("theme.colors.secondary: invalid color"));
    Theme defaults;
    EXPECT_EQ(t.scheme.primary, defaults.scheme.primary);
}

// Hex needs its '#': a value made of hex digits is a role name, and an unknown one is reported as such.
TEST_F(ThemeTableTest, BareHexIsAnUnknownRole) {
    Theme t = parse(R"(return { seed = "face", colors = { primary = "00FF00", secondary = "pirmary/0.5" } })");
    EXPECT_TRUE(hasError("theme: invalid seed color 'face': unknown role 'face' (a hex color needs its '#': '#face')"));
    EXPECT_TRUE(hasError("theme.colors.primary: invalid color: unknown role '00FF00' (a hex color needs its '#'"));
    EXPECT_TRUE(hasError("theme.colors.secondary: invalid color: unknown role 'pirmary'"));
    Theme defaults;
    EXPECT_EQ(t.seed, defaults.seed);
    EXPECT_EQ(t.scheme.primary, defaults.scheme.primary);
}

TEST_F(ThemeTableTest, UnknownImGuiColorIsReported) {
    Theme t = parse(R"(return { imgui = { NotAColor = "#FFFFFF" } })");
    EXPECT_TRUE(hasError("unknown ImGuiCol 'NotAColor'"));
    EXPECT_TRUE(t.imguiColors.empty());
}

TEST_F(ThemeTableTest, UnknownStyleFieldIsReported) {
    Theme t = parse("return { style = { NotAField = 3, FrameRounding = 2 } }");
    EXPECT_TRUE(hasError("unknown ImGuiStyle field 'NotAField'"));
    ASSERT_EQ(t.style.size(), 1u);
    EXPECT_EQ(t.style[0].name, "FrameRounding");
}

// A preset is a ThemePreset name: another one (a typo, a number) is reported with the names and keeps the default.
TEST_F(ThemeTableTest, UnknownPresetIsReported) {
    Theme t = parse(R"(return { preset = "imgui-drak", seed = "#006A6A" })");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_TRUE(hasError("theme.preset: unknown preset 'imgui-drak' (one of: 'material', 'imgui-dark', "
                         "'imgui-light', 'imgui-classic')"))
        << luaui::errors()[0];
    EXPECT_EQ(t.preset, ThemePreset::Material);
    EXPECT_EQ(t.seed, *parseHex("#006A6A")); // the rest still applies
    luaui::clearErrors();
    t = parse("return { preset = 3 }");
    EXPECT_TRUE(hasError("theme.preset: unknown preset '3'"));
    EXPECT_EQ(t.preset, ThemePreset::Material);
}

TEST_F(ThemeTableTest, InvalidSeedIsReported) {
    Theme t = parse(R"(return { seed = "purple" })");
    EXPECT_TRUE(hasError("invalid seed color 'purple'"));
    EXPECT_EQ(t.seed, Theme{}.seed);
}

TEST_F(ThemeTableTest, AcceptsRelativeAndAbsoluteIndex) {
    lua::pushnil(m_state); // something below the table
    ASSERT_EQ(lua::loadbuffer(m_state, "return { spacing = 3 }", "=theme"), lua::ok);
    ASSERT_EQ(lua::pcall(m_state, 0, 1, 0), lua::ok);
    EXPECT_FLOAT_EQ(luaui::themeFromTable(m_state, -1).spacing, 3.0f);
    EXPECT_FLOAT_EQ(luaui::themeFromTable(m_state, 2).spacing, 3.0f);
    EXPECT_EQ(lua::gettop(m_state), 2);
}

TEST_F(ThemeTableTest, PushSchemeWritesEveryRoleAsHex) {
    Scheme s = generateScheme(*parseHex("#006A6A"), false);
    luaui::pushScheme(m_state, s);
    ASSERT_EQ(lua::gettop(m_state), 1);
    for (const auto& role : roles()) {
        std::string name{role.name};
        ASSERT_EQ(lua::getfield(m_state, -1, name.c_str()), lua::Type::String) << name;
        EXPECT_EQ(lua::tostring(m_state, -1), toHex(s.*role.member)) << name;
        lua::pop(m_state);
    }
}

// ui.scheme()'s table is built once per theme and copied for each call: a script changing the table it got changes
// nothing the next call returns, and a new theme gives its own colors.
TEST_F(ThemeTableTest, CurrentSchemeIsACopyOfACachedTable) {
    const auto primary = [&] {
        luaui::pushCurrentScheme(m_state);
        EXPECT_EQ(lua::gettop(m_state), 1);
        EXPECT_EQ(lua::getfield(m_state, -1, "primary"), lua::Type::String);
        std::string hex{lua::tostring(m_state, -1)};
        lua::pop(m_state, 2);
        return hex;
    };
    const std::string before = primary();
    EXPECT_EQ(before, toHex(theme().scheme.primary));
    luaui::pushCurrentScheme(m_state);
    lua::pushstring(m_state, "#000000");
    lua::setfield(m_state, -2, "primary");
    lua::pushnil(m_state);
    lua::setfield(m_state, -2, "secondary");
    lua::pop(m_state);
    EXPECT_EQ(primary(), before);
    luaui::pushCurrentScheme(m_state);
    EXPECT_EQ(lua::getfield(m_state, -1, "secondary"), lua::Type::String);
    lua::pop(m_state, 2);
    // Every role, as pushScheme() writes them.
    luaui::pushCurrentScheme(m_state);
    for (const auto& role : roles()) {
        lua::pushstring(m_state, role.name);
        ASSERT_EQ(lua::rawget(m_state, -2), lua::Type::String) << role.name;
        EXPECT_EQ(lua::tostring(m_state, -1), toHex(theme().scheme.*role.member)) << role.name;
        lua::pop(m_state);
    }
    lua::pop(m_state);

    const Theme previous = theme();
    Theme other;
    other.seed = *parseHex("#B3261E");
    other.scheme = generateScheme(other.seed, other.dark);
    setTheme(other);
    ASSERT_TRUE(applyPendingTheme());
    EXPECT_EQ(primary(), toHex(other.scheme.primary));
    EXPECT_NE(primary(), before);
    setTheme(previous);
    applyPendingTheme();
}

TEST_F(ThemeTableTest, PushedSchemeParsesBackAsColors) {
    // A scheme pushed to Lua can be fed back as `colors` overrides.
    Scheme s = generateScheme(*parseHex("#FF6D00"), true);
    lua::newtable(m_state);
    luaui::pushScheme(m_state, s);
    lua::setfield(m_state, -2, "colors");
    Theme t = luaui::themeFromTable(m_state, -1);
    EXPECT_TRUE(luaui::errors().empty());
    for (const auto& role : roles())
        EXPECT_EQ(toHex(t.scheme.*role.member), toHex(s.*role.member)) << role.name;
}

TEST_F(ThemeTableTest, ColorOverridesAcceptRolesOfTheNewScheme) {
    // Roles in `colors` refer to the scheme generated from this table's seed, not the current theme.
    Theme t = parse(R"(
        return { seed = "#B3261E", dark = false,
                 colors = { surface = "surfaceContainerLow", primary = "onSurface/0.5", secondary = "#00FF00" } })");
    Scheme generated = generateScheme(*parseHex("#B3261E"), false);
    EXPECT_EQ(t.scheme.surface, generated.surfaceContainerLow);
    EXPECT_EQ(t.scheme.primary, generated.onSurface.withAlpha(0.5f));
    EXPECT_EQ(t.scheme.secondary, (Color{0, 1, 0, 1}));
    EXPECT_NE(generated.surfaceContainerLow, Theme{}.scheme.surfaceContainerLow);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, RolesReferToGeneratedColorsNotOverrides) {
    // `primary` is overridden, but `secondary = "primary"` still means the generated primary.
    Theme t = parse(R"(return { seed = "#006A6A", colors = { primary = "#FF0000", secondary = "primary" } })");
    Scheme generated = generateScheme(*parseHex("#006A6A"), Theme{}.dark);
    EXPECT_EQ(t.scheme.primary, (Color{1, 0, 0, 1}));
    EXPECT_EQ(t.scheme.secondary, generated.primary);
}

TEST_F(ThemeTableTest, ImGuiColorsAcceptRoles) {
    Theme t = parse(R"(return { seed = "#B3261E", dark = true, imgui = { WindowBg = "surfaceContainer/0.25" } })");
    Scheme generated = generateScheme(*parseHex("#B3261E"), true);
    ASSERT_EQ(t.imguiColors.size(), 1u);
    EXPECT_EQ(t.imguiColors[0].first, ImGuiCol_WindowBg);
    EXPECT_EQ(t.imguiColors[0].second, generated.surfaceContainer.withAlpha(0.25f));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, SeedAcceptsRoleNameAndTable) {
    // The seed has no scheme of its own yet; its roles come from the current theme.
    Theme fromRole = parse(R"(return { seed = "tertiary" })");
    EXPECT_EQ(fromRole.seed, theme().scheme.tertiary);
    Theme fromTable = parse("return { seed = { 0, 0.5, 1 } }");
    EXPECT_EQ(fromTable.seed, (Color{0, 0.5f, 1, 1}));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, InvalidImGuiColorIsReported) {
    Theme t = parse(R"(return { imgui = { WindowBg = "notARole", Text = 42 } })");
    EXPECT_TRUE(hasError("theme.imgui.WindowBg: invalid color"));
    EXPECT_TRUE(hasError("theme.imgui.Text: invalid color"));
    EXPECT_TRUE(t.imguiColors.empty());
}

// lua::tostring converts a number key in place, which would break the lua::next loop ("invalid key to
// 'next'", raised unprotected): keys that aren't strings are reported and skipped.
TEST_F(ThemeTableTest, NonStringKeysAreReportedAndSkipped) {
    Theme t = parse(R"(
        return { colors = { "#FF0000", primary = "#00FF00" },
                 imgui = { "#FF0000", [2.5] = "#00FF00", Text = "#0000FF" },
                 style = { 4, FrameRounding = 6 } })");
    EXPECT_TRUE(hasError("theme.colors: keys must be role names"));
    EXPECT_TRUE(hasError("theme.imgui: keys must be ImGuiCol names"));
    EXPECT_TRUE(hasError("theme.style: keys must be ImGuiStyle field names"));
    EXPECT_EQ(t.scheme.primary, (Color{0, 1, 0, 1}));
    ASSERT_EQ(t.imguiColors.size(), 1u);
    EXPECT_EQ(t.imguiColors[0].second, (Color{0, 0, 1, 1}));
    ASSERT_EQ(t.style.size(), 1u);
    EXPECT_EQ(t.style[0].name, "FrameRounding");
}

// tokens = { ... }: each a number in its range, a boolean or a ButtonFill name, kept in order, applied over a design's
// defaults by name.
TEST_F(ThemeTableTest, TokenOverrides) {
    Theme t = parse(R"(return { tokens = { controlHeight = 40, inputUnderline = false, buttonFill = "outline" } })");
    EXPECT_TRUE(luaui::errors().empty());
    ASSERT_EQ(t.tokens.size(), 3u);
    const SkinTokens k = applyTokenOverrides(fluentTokens(), t.tokens);
    EXPECT_FLOAT_EQ(k.controlHeight, 40.0f);
    EXPECT_FALSE(k.inputUnderline);
    EXPECT_EQ(k.buttonFill, ButtonFill::Outline);
    EXPECT_FLOAT_EQ(k.controlRadius, fluentTokens().controlRadius); // not overridden
    EXPECT_EQ(fluentTokensFor(t), k);
}

TEST_F(ThemeTableTest, BadTokensAreReportedAndKeepTheirDefaults) {
    Theme t = parse(R"(return { tokens = {
        nope = 1, controlHeight = 1000, controlRadius = 0/0, density = "2", inputUnderline = 1,
        buttonFill = "tonal", gap = {}, strokeWidth = 2, [1] = 4,
    } })");
    ASSERT_EQ(t.tokens.size(), 1u);
    EXPECT_EQ(t.tokens[0].name, "strokeWidth");
    for (const char* error : {"theme.tokens: unknown token 'nope' (known: controlHeight, compactHeight",
                              "theme.tokens.controlHeight: expected a number in 8..200",
                              "theme.tokens.controlRadius: expected a number in 0..100",
                              "theme.tokens.density: expected a number in 0.5..2",
                              "theme.tokens.inputUnderline: expected a boolean",
                              R"(theme.tokens.buttonFill: expected "filled" or "outline")",
                              "theme.tokens.gap: expected a number in 0..100", "theme.tokens: keys must be token names"})
        EXPECT_TRUE(hasError(error)) << error;
    EXPECT_EQ(luaui::errors().size(), 8u);
    const SkinTokens k = fluentTokensFor(t);
    EXPECT_FLOAT_EQ(k.controlHeight, fluentTokens().controlHeight);
    EXPECT_FLOAT_EQ(k.strokeWidth, 2.0f);

    luaui::clearErrors();
    t = parse("return { tokens = 5 }");
    EXPECT_TRUE(t.tokens.empty());
    EXPECT_TRUE(hasError("theme.tokens: expected a table"));
}

// A Theme built in C++ is held to the same limits: setTheme drops a token override with a problem.
TEST_F(ThemeTableTest, SetThemeDropsBadTokenOverrides) {
    Theme t;
    t.tokens = {{"controlHeight", 1.0f}, {"nope", 1.0f}, {"inputUnderline", 2.0f}, {"gap", 4.0f},
                {"buttonFill", std::string{"outline"}}, {"density", std::numeric_limits<float>::infinity()}};
    setTheme(t);
    ASSERT_TRUE(applyPendingTheme());
    ASSERT_EQ(theme().tokens.size(), 2u);
    EXPECT_EQ(theme().tokens[0].name, "gap");
    EXPECT_EQ(theme().tokens[1].name, "buttonFill");
    setTheme(Theme{});
    applyPendingTheme();
}

TEST(SkinTokens, TheTableNamesEveryTokenOnce) {
    std::set<std::string_view> names;
    for (const TokenInfo& info : tokenInfos()) {
        EXPECT_TRUE(names.insert(info.name).second) << info.name;
        EXPECT_EQ(findToken(info.name), &info);
        EXPECT_FALSE(info.doc.empty()) << info.name;
        // Each design's default is in the token's range.
        if (info.kind == TokenKind::Number) {
            for (const SkinTokens& d : {fluentTokens(), materialTokens()}) {
                const float v = d.*std::get<float SkinTokens::*>(info.member);
                EXPECT_GE(v, info.min) << info.name;
                EXPECT_LE(v, info.max) << info.name;
            }
        }
    }
    EXPECT_EQ(findToken("nope"), nullptr);
    EXPECT_EQ(tokenProblem("controlHeight", 32.0f), "");
    EXPECT_EQ(tokenProblem("controlHeight", true), "expected a number in 8..200");
    EXPECT_EQ(tokenProblem("nope", 1.0f), "unknown token");
}
