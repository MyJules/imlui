// Tests for imlui.theme.theme lookups (resolveColor, isStyleField, imguiColorIndex)
// and imlui.theme.typography name lookup.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.test.headless_frame;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.theme.typography;

using namespace imlui;

namespace {

class ThemeTest : public ::testing::Test {
protected:
    void SetUp() override { m_context = ImGui::CreateContext(); }
    void TearDown() override { ImGui::DestroyContext(m_context); }

    ImGuiContext* m_context = nullptr;
};

} // namespace

// ------------------------------------------------------------------ resolveColor

TEST_F(ThemeTest, ResolveColorHex) {
    EXPECT_EQ(resolveColor("#FF0000"), (Color{1, 0, 0, 1}));
    EXPECT_EQ(resolveColor("#00FF0080"), parseHex("#00FF0080"));
}

TEST_F(ThemeTest, ResolveColorRoleName) {
    for (const auto& role : roles()) {
        auto c = resolveColor(role.name);
        ASSERT_TRUE(c.has_value()) << role.name;
        EXPECT_EQ(*c, theme().scheme.*role.member) << role.name;
    }
}

TEST_F(ThemeTest, ResolveColorRoleWithAlpha) {
    auto c = resolveColor("onSurface/0.6");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(*c, theme().scheme.onSurface.withAlpha(0.6f));
}

TEST_F(ThemeTest, ResolveColorRejectsABadAlpha) {
    // The alpha must be a whole number in 0..1: a typo or an out-of-range value is no color (reported by the caller
    // like an unknown role) instead of a silent 0 or 2.
    for (const char* spec : {"onSurface/O.6", "primary/2", "primary/-0.1", "primary/", "primary/0.5x", "primary/ 0.5",
                             "primary/nan", "primary/inf"})
        EXPECT_FALSE(resolveColor(spec).has_value()) << spec;
    EXPECT_EQ(resolveColor("primary/0"), theme().scheme.primary.withAlpha(0.0f));
    EXPECT_EQ(resolveColor("primary/1"), theme().scheme.primary.withAlpha(1.0f));
    EXPECT_EQ(resolveColor("primary/.25"), theme().scheme.primary.withAlpha(0.25f));
}

TEST_F(ThemeTest, ResolveColorHexWithoutHash) {
    // Not a role name, so it falls back to hex parsing.
    EXPECT_EQ(resolveColor("0000FF"), (Color{0, 0, 1, 1}));
}

TEST_F(ThemeTest, ResolveColorRejectsUnknown) {
    EXPECT_FALSE(resolveColor("").has_value());
    EXPECT_FALSE(resolveColor("notARole").has_value());
    EXPECT_FALSE(resolveColor("notARole/0.5").has_value());
    EXPECT_FALSE(resolveColor("#nothex").has_value());
}

// ------------------------------------------------------------------ isStyleField

TEST_F(ThemeTest, IsStyleFieldKnowsImGuiStyleFields) {
    for (const char* name : {"Alpha", "WindowPadding", "FrameRounding", "ItemSpacing", "ScrollbarSize",
                             "ButtonTextAlign", "SeparatorTextPadding"})
        EXPECT_TRUE(isStyleField(name)) << name;
}

TEST_F(ThemeTest, IsStyleFieldRejectsUnknown) {
    for (const char* name : {"", "frameRounding", "NotAField", "Colors", "FontScale"})
        EXPECT_FALSE(isStyleField(name)) << name;
}

// ------------------------------------------------------------------ style overrides at any zoom

// Every style override the validation accepts survives the DPI scale. ImGuiStyle::ScaleAllSizes truncates, so at
// zoom 0.5 an accepted WindowMinSize of 1 became 0, which NewFrame asserts on (an abort).
TEST(ThemeStyleOverrides, EveryAcceptedValueRendersAtTheZoomLimits) {
    test::HeadlessFrame h{test::HeadlessFrame::Options{.builtins = true}};
    ASSERT_EQ(h.run(R"(
        root = Column { Text { "a" }, Button { "b" }, TextField {}, Slider {}, Card { Text { "c" } },
                        Row { Checkbox { "d" }, Switch {} }, ScrollView { height = 50, Text { "e" } },
                        TabBar { Tab { "t", Text { "f" } } }, Separator { "g" } })"),
              "");
    int applied = 0;
    for (const float zoom : {minZoom, maxZoom}) {
        setZoom(zoom);
        for (const std::string_view name : styleFieldNames()) {
            for (const float v : {-maxStyleValue, -1.0f, 0.0f, 0.5f, 1.0f, 1.5f, maxStyleValue}) {
                if (!styleValueProblem(name, {v, v}).empty()) continue;
                Theme t;
                t.style.push_back({std::string{name}, {v, v}});
                setTheme(std::move(t));
                applyPendingTheme();
                h.renderFrame("root");
                const ImGuiStyle& st = ImGui::GetStyle();
                ASSERT_GE(st.WindowMinSize.x, 1.0f) << name << " = " << v << " at zoom " << zoom;
                ASSERT_GE(st.WindowMinSize.y, 1.0f) << name << " = " << v << " at zoom " << zoom;
                ++applied;
            }
        }
    }
    EXPECT_GT(applied, 2 * 31 * 3); // every field, with several values each
    setZoom(1.0f);
    setTheme(Theme{});
    applyPendingTheme();
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ imguiColorIndex

TEST_F(ThemeTest, ImGuiColorIndexMatchesEveryName) {
    for (int i = 0; i < ImGuiCol_COUNT; ++i) EXPECT_EQ(imguiColorIndex(ImGui::GetStyleColorName(i)), i);
}

TEST_F(ThemeTest, ImGuiColorIndexKnownNames) {
    EXPECT_EQ(imguiColorIndex("Text"), ImGuiCol_Text);
    EXPECT_EQ(imguiColorIndex("WindowBg"), ImGuiCol_WindowBg);
}

TEST_F(ThemeTest, ImGuiColorIndexUnknownIsMinusOne) {
    EXPECT_EQ(imguiColorIndex(""), -1);
    EXPECT_EQ(imguiColorIndex("windowbg"), -1);
    EXPECT_EQ(imguiColorIndex("NotAColor"), -1);
}

// ------------------------------------------------------------------ typography

TEST(Typography, TextStyleFromNameFindsEveryStyle) {
    for (int i = 0; i < static_cast<int>(TextStyle::Count); ++i) {
        auto s = static_cast<TextStyle>(i);
        const TextStyleInfo& info = textStyleInfo(s);
        auto found = textStyleFromName(info.name);
        ASSERT_TRUE(found.has_value()) << info.name;
        EXPECT_EQ(*found, s) << info.name;
    }
}

TEST(Typography, KnownNames) {
    EXPECT_EQ(textStyleFromName("bodyLarge"), TextStyle::BodyLarge);
    EXPECT_EQ(textStyleFromName("headlineSmall"), TextStyle::HeadlineSmall);
    EXPECT_EQ(textStyleFromName("labelSmall"), TextStyle::LabelSmall);
}

TEST(Typography, UnknownNameIsEmpty) {
    EXPECT_FALSE(textStyleFromName("").has_value());
    EXPECT_FALSE(textStyleFromName("BodyLarge").has_value());
    EXPECT_FALSE(textStyleFromName("huge").has_value());
}

TEST(Typography, Material3Sizes) {
    EXPECT_FLOAT_EQ(textStyleInfo(TextStyle::DisplayLarge).size, 57.0f);
    EXPECT_FLOAT_EQ(textStyleInfo(TextStyle::BodyMedium).size, 14.0f);
    EXPECT_EQ(textStyleInfo(TextStyle::TitleMedium).weight, Weight::Medium);
    EXPECT_EQ(textStyleInfo(TextStyle::BodyLarge).weight, Weight::Regular);
}
