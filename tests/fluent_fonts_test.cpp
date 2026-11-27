// The Fluent design's system fonts: under design "fluent" the type scale draws with Segoe UI Variable and Segoe UI
// Semibold and the icons with Segoe Fluent Icons, read from the OS font folder where they are installed (Windows);
// without them (another OS, or system fonts turned off, as the other tests have them) it keeps Roboto and Material
// Icons. A theme's own fonts win, and Material never uses the system's.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.lua.errors;
import imlui.theme.font_library;
import imlui.theme.icons;
import imlui.theme.system_fonts;
import imlui.theme.theme;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

class FluentFontsTest : public ::testing::Test {
protected:
    ~FluentFontsTest() override {
        setTheme(Theme{});
        applyPendingTheme();
        setSystemFontsEnabled(false);
    }

    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true, .systemFonts = true}};
};

/// Whether this machine has Fluent's fonts (Windows 11).
bool haveSegoe() { return systemFont(fluentRegularFont).has_value() && systemFont(fluentIconFiles[0]).has_value(); }

} // namespace

TEST_F(FluentFontsTest, SystemFontNamesAreFilesOfTheFontFolder) {
    EXPECT_TRUE(isSystemFont("system:/fonts/x.ttf"));
    EXPECT_FALSE(isSystemFont("app:/fonts/x.ttf"));
    EXPECT_FALSE(systemFont("").has_value());
    EXPECT_FALSE(systemFont("../win.ini").has_value());
    EXPECT_FALSE(systemFont("sub/x.ttf").has_value());
    EXPECT_FALSE(systemFont("no-such-font-here.ttf").has_value());
    EXPECT_FALSE(readSystemFont("app:/fonts/Roboto-Regular.ttf").has_value());
    if (!haveSegoe()) GTEST_SKIP() << "no Segoe UI Variable / Segoe Fluent Icons on this system";
    EXPECT_EQ(systemFont(fluentRegularFont), std::string{systemFontPrefix} + std::string{fluentRegularFont});
    const auto bytes = readSystemFont(*systemFont(fluentRegularFont));
    ASSERT_TRUE(bytes.has_value());
    EXPECT_GT(bytes->size(), 0u);
}

TEST_F(FluentFontsTest, MaterialKeepsTheThemesFonts) {
    Theme t;
    t.fonts.regular = "app:/fonts/Mine.ttf";
    EXPECT_EQ(designFonts(t), t.fonts);
    EXPECT_EQ(designFonts(Theme{}), FontSet{});
    applyTheme("theme {}");
    EXPECT_EQ(fluentIconFont(), nullptr);
    EXPECT_FALSE(iconGlyph("home").fluent);
}

TEST_F(FluentFontsTest, FluentDrawsWithTheSegoeFontsWhereInstalled) {
    if (!haveSegoe()) GTEST_SKIP() << "no Segoe UI Variable / Segoe Fluent Icons on this system";
    applyTheme("theme {}");
    ImFont* roboto = regularFace();
    ImFont* robotoMedium = mediumFace();
    ASSERT_NE(roboto, nullptr);

    Theme fluentTheme;
    fluentTheme.design = "fluent";
    const FontSet set = designFonts(fluentTheme);
    EXPECT_EQ(set.regular, "system:/fonts/SegUIVar.ttf");
    EXPECT_FALSE(set.medium.empty());

    applyTheme(R"(theme { design = "fluent" })");
    EXPECT_NE(regularFace(), nullptr);
    EXPECT_NE(regularFace(), roboto);
    EXPECT_NE(mediumFace(), robotoMedium);
    ASSERT_NE(fluentIconFont(), nullptr);
    // A mapped name draws Segoe's glyph; one without a Fluent counterpart stays Material's.
    const IconGlyph home = iconGlyph("home");
    EXPECT_TRUE(home.fluent);
    EXPECT_EQ(home.font, fluentIconFont());
    EXPECT_EQ(home.code, fluentIconCode("home"));
    const IconGlyph broken = iconGlyph("broken_image");
    EXPECT_FALSE(broken.fluent);
    EXPECT_EQ(broken.font, iconFont());
    EXPECT_FALSE(broken.text.empty());
    // A frame draws with them without errors.
    run(R"(root = Column { Button { "OK", icon = "home" }, Text { "Text" }, Icon { "settings" } })");
    m_h.renderFrames("root", 2);
    EXPECT_TRUE(luaui::errors().empty());

    // The theme's own face wins; Medium still comes from the system.
    applyTheme(R"(theme { design = "fluent", fonts = { regular = "imlui:/fonts/Roboto-Regular.ttf" } })");
    EXPECT_EQ(regularFace(), roboto);
    EXPECT_NE(mediumFace(), robotoMedium);

    // Back to Material: Roboto and Material Icons again.
    applyTheme("theme {}");
    EXPECT_EQ(regularFace(), roboto);
    EXPECT_EQ(mediumFace(), robotoMedium);
    EXPECT_EQ(fluentIconFont(), nullptr);
    EXPECT_FALSE(iconGlyph("home").fluent);
}

TEST_F(FluentFontsTest, WithoutSystemFontsFluentKeepsRobotoAndMaterialIcons) {
    setSystemFontsEnabled(false);
    applyTheme("theme {}");
    ImFont* roboto = regularFace();
    Theme fluentTheme;
    fluentTheme.design = "fluent";
    EXPECT_EQ(designFonts(fluentTheme), FontSet{});
    applyTheme(R"(theme { design = "fluent" })");
    EXPECT_EQ(regularFace(), roboto);
    EXPECT_EQ(fluentIconFont(), nullptr);
    EXPECT_FALSE(iconGlyph("home").fluent);
    EXPECT_FALSE(iconGlyph("home").text.empty());
    EXPECT_TRUE(luaui::errors().empty());
}

// The name table is sorted (the lookup bisects it) and maps the names the controls draw.
TEST(FluentIcons, MapsTheCommonMaterialNames) {
    for (const char* name : {"add", "check", "close", "expand_more", "home", "search", "settings", "menu"})
        EXPECT_NE(fluentIconCode(name), 0u) << name;
    EXPECT_EQ(fluentIconCode("broken_image"), 0u);
    EXPECT_EQ(fluentIconCode(""), 0u);
    EXPECT_EQ(fluentIconCode("zzz"), 0u);
}
