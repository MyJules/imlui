// Tests for imlui.theme.theme lookups (resolveColor, isStyleField, imguiColorIndex)
// and imlui.theme.typography name lookup.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.test.headless_frame;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.theme.typography;
import imlui.types.lua_convert;
import imlui.ui.enum_names;

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

// A negative ItemSpacing.x is accepted but reaches ImGui as 0: menus and popups store it as an unsigned 16-bit width
// (ImGuiMenuColumns), where a negative one is undefined (UBSan: "-5000 is outside the range of ... unsigned short").
// The vertical spacing keeps its sign.
TEST(ThemeStyleOverrides, ItemSpacingXIsClampedToWhatMenusStore) {
    test::HeadlessFrame h{test::HeadlessFrame::Options{.builtins = true}};
    for (const float zoom : {minZoom, 1.0f, maxZoom}) {
        setZoom(zoom);
        Theme t;
        t.style.push_back({"ItemSpacing", {-maxStyleValue, -4.0f}});
        setTheme(std::move(t));
        applyPendingTheme();
        EXPECT_EQ(ImGui::GetStyle().ItemSpacing.x, 0.0f) << zoom;
        EXPECT_LT(ImGui::GetStyle().ItemSpacing.y, 0.0f) << zoom;
        Theme wide;
        wide.style.push_back({"ItemSpacing", {maxStyleValue, maxStyleValue}});
        setTheme(std::move(wide));
        applyPendingTheme();
        EXPECT_LE(ImGui::GetStyle().ItemSpacing.x, 65535.0f) << zoom;
    }
    setZoom(1.0f);
    setTheme(Theme{});
    applyPendingTheme();
}

// ImGuiStyle::ScaleAllSizes truncates: below a scale of 1 a 1px border became 0 (ImGui drew none: under Fluent, which
// sets the window, popup, frame and child borders from its strokeWidth token, every raw ImGui border vanished at zoom
// 90%), and at 1.5 it became 1 where the skins draw round(dp(1)) = 2. A border or line that is set is at least a
// pixel at any scale, rounded like the skins' strokes; one that is 0 stays 0.
TEST(ThemeStyleOverrides, LineWidthsStayVisibleAtAnyScale) {
    test::HeadlessFrame h;
    Theme material;
    Theme fluentTheme;
    fluentTheme.design = std::string{fluent::designName};
    for (const float zoom : {0.5f, 0.9f, 1.5f, 2.0f}) {
        setZoom(zoom);
        const float stroke = std::max(1.0f, std::round(dp(1.0f))); // a skin's 1dp stroke, as strokeWidth() draws it
        setTheme(material);
        applyPendingTheme();
        const ImGuiStyle& st = ImGui::GetStyle();
        EXPECT_EQ(st.ChildBorderSize, stroke) << "material, zoom " << zoom;
        EXPECT_EQ(st.TabBarBorderSize, stroke) << "material, zoom " << zoom;
        EXPECT_EQ(st.SeparatorTextBorderSize, stroke) << "material, zoom " << zoom;
        EXPECT_EQ(st.WindowBorderSize, 0.0f) << "material, zoom " << zoom; // Material sets none
        EXPECT_EQ(st.FrameBorderSize, 0.0f) << "material, zoom " << zoom;
        EXPECT_GE(st.InputTextCursorSize, 1.0f) << "material, zoom " << zoom; // ImGui's default, 1
        setTheme(fluentTheme);
        applyPendingTheme();
        for (const float width : {st.WindowBorderSize, st.PopupBorderSize, st.FrameBorderSize, st.ChildBorderSize})
            EXPECT_EQ(width, stroke) << "fluent, zoom " << zoom;
        // A style override is scaled the same way: 0.5 is still a line.
        Theme thin;
        thin.style.push_back({"FrameBorderSize", {0.5f, 0.0f}});
        setTheme(std::move(thin));
        applyPendingTheme();
        EXPECT_EQ(st.FrameBorderSize, std::max(1.0f, std::round(0.5f * zoom))) << "override, zoom " << zoom;
    }
    setZoom(1.0f);
    setTheme(Theme{});
    applyPendingTheme();
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

// The Lua names of TextStyle (imlui.ui.enum_names) are the type scale's names, in its order.
TEST(Typography, LuaNamesAreTheTypeScaleNames) {
    for (int i = 0; i < static_cast<int>(TextStyle::Count); ++i) {
        const auto s = static_cast<TextStyle>(i);
        const auto& [value, name] = EnumNames<TextStyle>::values[static_cast<std::size_t>(i)];
        EXPECT_EQ(value, s) << i;
        EXPECT_EQ(name, textStyleInfo(s).name) << i;
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

// linePixels() is what ImGui::GetFontSize() gives inside pushFont(): the text fields and spin boxes read their line
// height from it instead of pushing and popping a font twice per frame, and must lay out to the same pixel.
TEST(Typography, LinePixelsIsThePushedFontSize) {
    test::HeadlessFrame h{test::HeadlessFrame::Options{.builtins = true}};
    int checked = 0;
    for (const float monitor : {1.0f, 1.25f, 1.5f, 1.75f}) {
        setContentScale(monitor);
        for (const float zoom : {minZoom, 0.67f, 0.9f, 1.0f, 1.1f, 1.33f, 2.5f, maxZoom}) {
            setZoom(zoom);
            for (const float fontScale : {minFontScale, 0.8f, 1.0f, 1.15f, 1.3f, 2.2f, maxFontScale}) {
                Theme t;
                t.fontScale = fontScale;
                setTheme(std::move(t));
                applyPendingTheme();
                ImGui::NewFrame();
                for (int i = 0; i < static_cast<int>(TextStyle::Count); ++i) {
                    const auto s = static_cast<TextStyle>(i);
                    pushFont(s);
                    EXPECT_EQ(linePixels(s), ImGui::GetFontSize())
                        << textStyleInfo(s).name << ", monitor " << monitor << ", zoom " << zoom << ", fontScale "
                        << fontScale;
                    ImGui::PopFont();
                    EXPECT_LE(fontPixels(s), maxTextPixels);
                    ++checked;
                }
                ImGui::Render();
            }
        }
    }
    EXPECT_EQ(checked, 4 * 8 * 7 * static_cast<int>(TextStyle::Count));
    setContentScale(1.0f);
    setZoom(1.0f);
    setTheme(Theme{});
    applyPendingTheme();
}

// The largest text: the theme's largest fontScale at the largest zoom on a 4x monitor, displayLarge (57dp) is
// thousands of pixels before the cap, and ten distinct glyphs that large didn't fit ImGui's 8192-pixel font atlas
// ("Out of texture memory", an abort). Text is drawn at most maxTextPixels tall, as ImGui draws a pushed font.
TEST(Typography, TheLargestTextFitsTheFontAtlas) {
    test::HeadlessFrame h{test::HeadlessFrame::Options{.builtins = true}};
    ASSERT_EQ(h.run(R"(
        root = Column { Text { "ABCDEFGHIJ", style = "displayLarge" }, Text { "KLMNOPQRST", style = "headlineLarge" },
                        Button { "UVWXYZ" }, TextField { value = "abcdefghij" } })"),
              "");
    setContentScale(4.0f);
    setZoom(maxZoom);
    Theme t;
    t.fontScale = maxFontScale;
    setTheme(std::move(t));
    applyPendingTheme();
    EXPECT_EQ(fontPixels(TextStyle::DisplayLarge), maxTextPixels);
    EXPECT_EQ(linePixels(TextStyle::BodyLarge), maxTextPixels);
    h.renderFrames("root", 2);
    EXPECT_TRUE(luaui::errors().empty());
    setContentScale(1.0f);
    setZoom(1.0f);
    setTheme(Theme{});
    applyPendingTheme();
}
