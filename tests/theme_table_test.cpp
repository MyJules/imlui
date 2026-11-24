// Tests for imlui.lua.theme_table: building a Theme from a Lua table.
#include <gtest/gtest.h>

import lua;
import imgui;
import imlui.color;
import imlui.theme.scheme;
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
    for (const auto& role : roles()) EXPECT_EQ(t.scheme.*role.member, defaults.scheme.*role.member) << role.name;
    EXPECT_TRUE(t.imguiColors.empty());
    EXPECT_TRUE(t.style.empty());
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, ScalarFields) {
    Theme t = parse(R"(return { preset = "imgui-classic", seed = "#006A6A", dark = false, spacing = 6, fontScale = 1.25 })");
    EXPECT_EQ(t.preset, "imgui-classic");
    EXPECT_EQ(t.seed, *parseHex("#006A6A"));
    EXPECT_FALSE(t.dark);
    EXPECT_FLOAT_EQ(t.spacing, 6.0f);
    EXPECT_FLOAT_EQ(t.fontScale, 1.25f);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ThemeTableTest, SchemeIsGeneratedFromSeedAndMode) {
    Theme t = parse(R"(return { seed = "#B3261E", dark = false })");
    Scheme expected = generateScheme(*parseHex("#B3261E"), false);
    for (const auto& role : roles()) EXPECT_EQ(t.scheme.*role.member, expected.*role.member) << role.name;
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

TEST_F(ThemeTableTest, PushedSchemeParsesBackAsColors) {
    // A scheme pushed to Lua can be fed back as `colors` overrides.
    Scheme s = generateScheme(*parseHex("#FF6D00"), true);
    lua::newtable(m_state);
    luaui::pushScheme(m_state, s);
    lua::setfield(m_state, -2, "colors");
    Theme t = luaui::themeFromTable(m_state, -1);
    EXPECT_TRUE(luaui::errors().empty());
    for (const auto& role : roles()) EXPECT_EQ(toHex(t.scheme.*role.member), toHex(s.*role.member)) << role.name;
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
