// Tests for fonts declared from Lua (`ui.theme { fonts = { ... } }`): replacing the type scale's faces, named fonts
// selected per element (`font` prop, ui.textSize), fallbacks merged into the text fonts, errors for missing or
// broken fonts, and hot reload reusing atlas fonts instead of piling them up. Roboto Medium, mounted under `app:/`,
// stands in for an app's own typeface.
#include <gtest/gtest.h>

import imgui;
import imlui.lua.errors;
import imlui.resources;
import imlui.resources.directory_mount;
import imlui.theme.font_library;
import imlui.theme.theme;
import imlui.theme.typography;
import imlui.test.headless_frame;
import imlui.test.last_item_probe;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

/// Medium's l, i and r are wider than Regular's (by about 3px over this text at bodyLarge), so the two faces are
/// told apart by width.
constexpr const char* sample = "llllllllll rrrrrrrrrr";

class FontsTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_h.resources().mount("app:/fonts/", std::make_shared<DirectoryMount>(IMLUI_FONT_DIR));
        m_h.resources().mount("app:/", std::make_shared<DirectoryMount>(m_dir.path()));
        m_h.registerType<test::LastItemProbe>("Probe");
        m_builtinRegular = regularFace();
        m_builtinMedium = mediumFace();
    }
    void TearDown() override {
        // The theme is global: leave the default one (and its fonts) behind for the next test.
        setTheme(Theme{});
        applyPendingTheme();
    }

    /// Runs `code`, expecting no Lua error, then applies the pending theme as the engine does between frames.
    void apply(std::string_view code) {
        ASSERT_EQ(m_h.run(code), "");
        applyPendingTheme();
    }

    /// Width of `text` in pixels drawn in `face` at bodyLarge.
    static float widthIn(ImFont* face, std::string_view text) {
        return face
            ->CalcTextSizeA(fontPixels(TextStyle::BodyLarge), 3.4e38f, 0.0f, text.data(), text.data() + text.size())
            .x;
    }

    /// Width of the last item rendered inside the Probe `root` (whole pixels: compare within 1px).
    float probedWidth(std::string_view root) {
        m_h.renderFrame(root);
        return m_h.instance<test::LastItemProbe>(root)->size().x;
    }

    static int atlasFonts() { return ImGui::GetIO().Fonts->Fonts.Size; }

    test::TempDir m_dir;
    test::HeadlessFrame m_h{{.builtins = true}};
    ImFont* m_builtinRegular = nullptr;
    ImFont* m_builtinMedium = nullptr;
};

} // namespace

TEST_F(FontsTest, BuiltinFacesAreLoadedThroughTheLibrary) {
    ASSERT_NE(m_builtinRegular, nullptr);
    ASSERT_NE(m_builtinMedium, nullptr);
    EXPECT_NE(m_builtinRegular, m_builtinMedium);
    EXPECT_EQ(font(TextStyle::BodyLarge), m_builtinRegular);
    EXPECT_EQ(font(TextStyle::LabelLarge), m_builtinMedium);
    EXPECT_EQ(namedFont("regular"), m_builtinRegular);
    EXPECT_EQ(namedFont("medium"), m_builtinMedium);
    EXPECT_EQ(namedFont("mono"), nullptr);
}

TEST_F(FontsTest, ThemeReplacesTheTypeScaleFaces) {
    apply(R"(ui.theme { fonts = { regular = "app:/fonts/Roboto-Medium.ttf" } })");
    EXPECT_TRUE(luaui::errors().empty());
    ImFont* custom = regularFace();
    EXPECT_NE(custom, m_builtinRegular);
    EXPECT_NE(custom, m_builtinMedium); // its own atlas font, though from the same file as the built-in Medium
    EXPECT_EQ(font(TextStyle::BodyLarge), custom);
    EXPECT_EQ(font(TextStyle::DisplayLarge), custom);
    EXPECT_EQ(font(TextStyle::LabelLarge), m_builtinMedium); // medium unset: still Roboto Medium
    EXPECT_EQ(ImGui::GetIO().FontDefault, custom);

    // Text in the default style draws in the new face.
    ASSERT_EQ(m_h.run(std::string{"root = Probe { Text { \""} + sample + "\" } }"), "");
    EXPECT_NEAR(probedWidth("root"), widthIn(custom, sample), 1.0f);

    // The default theme brings Roboto back.
    apply("ui.theme {}");
    EXPECT_EQ(regularFace(), m_builtinRegular);
}

TEST_F(FontsTest, NamedFontsAreSelectedPerElement) {
    apply(R"(ui.theme { fonts = { mono = "app:/fonts/Roboto-Medium.ttf" } })");
    EXPECT_TRUE(luaui::errors().empty());
    ImFont* mono = namedFont("mono");
    ASSERT_NE(mono, nullptr);
    EXPECT_EQ(regularFace(), m_builtinRegular);
    ASSERT_GT(widthIn(mono, sample), widthIn(m_builtinRegular, sample) + 1.0f);

    ASSERT_EQ(m_h.run(std::string{"plain = Probe { Text { \""} + sample + "\" } }\n" + "mono = Probe { Text { \"" +
                      sample + "\", font = \"mono\" } }\n" + "unknown = Probe { Text { \"" + sample +
                      "\", font = \"nope\" } }\n" + "plainButton = Probe { Button { \"" + sample + "\" } }\n" +
                      "regularButton = Probe { Button { \"" + sample + "\", font = \"regular\" } }"),
              "");
    EXPECT_NEAR(probedWidth("plain"), widthIn(m_builtinRegular, sample), 1.0f);
    EXPECT_NEAR(probedWidth("mono"), widthIn(mono, sample), 1.0f);
    EXPECT_NEAR(probedWidth("unknown"), widthIn(m_builtinRegular, sample), 1.0f); // undeclared: the style's face
    // Button labels are labelLarge, a Medium style: "regular" draws them in the Regular face, narrower.
    EXPECT_LT(probedWidth("regularButton") + 1.0f, probedWidth("plainButton"));
    EXPECT_EQ(font(TextStyle::BodyLarge), m_builtinRegular); // the override ends with the element

    // ui.textSize measures in a named font too (dp; the harness scale is 1).
    ASSERT_EQ(m_h.run(std::string{"w1 = ui.textSize(\""} + sample + "\")\n" + "w2 = ui.textSize(\"" + sample +
                      "\", \"bodyLarge\", \"mono\")\n" + "assert(w2 > w1 + 1, w1 .. ' ' .. w2)"),
              "");
}

TEST_F(FontsTest, FallbacksAreMergedIntoTheTextFonts) {
    // The icon font stands in for a font with glyphs Roboto lacks: U+E88A is the "home" icon.
    constexpr ImWchar home = 0xE88A;
    EXPECT_FALSE(m_builtinRegular->IsGlyphInFont(home));
    apply(R"(ui.theme { fonts = { fallback = { "app:/fonts/MaterialIconsRound-Regular.otf" },
                                  mono = "app:/fonts/Roboto-Medium.ttf" } })");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_NE(regularFace(), m_builtinRegular); // Roboto with the fallback merged is another atlas font
    EXPECT_TRUE(regularFace()->IsGlyphInFont(home));
    EXPECT_TRUE(mediumFace()->IsGlyphInFont(home));
    EXPECT_TRUE(namedFont("mono")->IsGlyphInFont(home));
    EXPECT_TRUE(regularFace()->IsGlyphInFont('A'));

    // One fallback as a plain string works too; a missing one is reported once and left out.
    apply(R"(ui.theme { fonts = { fallback = "app:/fonts/Missing.otf" } })");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_EQ(luaui::errors()[0], "theme.fonts.fallback: font 'app:/fonts/Missing.otf' not found");
    EXPECT_FALSE(regularFace()->IsGlyphInFont(home));
}

TEST_F(FontsTest, MissingOrBrokenFontsAreReportedAndFallBackToRoboto) {
    m_dir.write("broken.ttf", "this is not a font file at all");
    apply(R"(ui.theme { fonts = { regular = "app:/fonts/Missing.ttf", medium = "app:/broken.ttf",
                                  mono = "app:/nope.ttf", bad = "no prefix.ttf", worse = 42 } })");
    // Malformed entries while the table is read, then load failures: fallbacks, regular, medium, named by name.
    const std::vector<std::string> expected = {
        "theme.fonts.worse: expected a resource name such as \"app:/fonts/Inter.ttf\"",
        "theme.fonts.regular: font 'app:/fonts/Missing.ttf' not found",
        "theme.fonts.medium: 'app:/broken.ttf' is not a TrueType/OpenType font",
        "theme.fonts.bad: invalid font resource name 'no prefix.ttf' (expected e.g. 'app:/fonts/Inter.ttf')",
        "theme.fonts.mono: font 'app:/nope.ttf' not found",
    };
    EXPECT_EQ(luaui::errors(), expected);
    EXPECT_EQ(regularFace(), m_builtinRegular);
    EXPECT_EQ(mediumFace(), m_builtinMedium);
    EXPECT_EQ(namedFont("mono"), nullptr);
    EXPECT_TRUE(declaresFont("mono")); // declared, so an element using it isn't logged again

    // Text with the failed font still draws, in the style's face.
    ASSERT_EQ(m_h.run(std::string{"root = Probe { Text { \""} + sample + "\", font = \"mono\" } }"), "");
    EXPECT_NEAR(probedWidth("root"), widthIn(m_builtinRegular, sample), 1.0f);

    luaui::clearErrors();
    ASSERT_EQ(m_h.run(R"(ui.theme { fonts = 3 })"), "");
    EXPECT_EQ(luaui::errors(), std::vector<std::string>{"theme.fonts: expected a table"});
}

TEST_F(FontsTest, ReapplyingReusesAtlasFonts) {
    const std::string theme = R"(ui.theme { fonts = { regular = "app:/fonts/Roboto-Medium.ttf",
        mono = "app:/fonts/Roboto-Medium.ttf", fallback = { "app:/fonts/MaterialIconsRound-Regular.otf" } } })";
    apply(theme);
    ImFont* regular = regularFace();
    ImFont* mono = namedFont("mono");
    const int fonts = atlasFonts();
    const std::size_t cached = libraryFontCount();
    // The same face and fallbacks are one atlas font, whatever role uses it.
    EXPECT_EQ(regular, mono);

    // A hot reload runs the script again: the theme is declared anew, the fonts are reused.
    for (int i = 0; i < 3; ++i) {
        apply(theme);
        apply("ui.theme {}");
    }
    apply(theme);
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(regularFace(), regular);
    EXPECT_EQ(namedFont("mono"), mono);
    EXPECT_EQ(atlasFonts(), fonts);
    EXPECT_EQ(libraryFontCount(), cached);
}

TEST_F(FontsTest, AChangedFontFileReplacesItsAtlasFont) {
    const auto bytes = m_h.resources().read("app:/fonts/Roboto-Medium.ttf");
    ASSERT_TRUE(bytes);
    m_dir.write("Custom.ttf", std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    const std::string theme = R"(ui.theme { fonts = { custom = "app:/Custom.ttf" } })";
    apply(theme);
    ImFont* before = namedFont("custom");
    ASSERT_NE(before, nullptr);
    const int fonts = atlasFonts();

    m_dir.touch("Custom.ttf", 10); // the directory mount reads it again into a new buffer
    apply(theme);
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_NE(namedFont("custom"), nullptr);
    EXPECT_NE(namedFont("custom"), before);
    EXPECT_EQ(atlasFonts(), fonts); // the old font was removed when the new one came into use
    EXPECT_EQ(ImGui::GetIO().Fonts->Fonts.contains(before), false);
    m_h.renderFrame("nothing"); // a frame with the new atlas font set renders
}
