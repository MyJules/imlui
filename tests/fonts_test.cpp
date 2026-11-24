// Tests for fonts declared from Lua (`ui.theme { fonts = { ... } }`): replacing the type scale's faces, named fonts
// selected per element (`font` prop, ui.textSize), fallbacks merged into the text fonts, errors for missing or
// broken fonts, and hot reload reusing atlas fonts instead of piling them up. Roboto Medium, mounted under `app:/`,
// stands in for an app's own typeface. Also: font names relative to the declaring script, the painter's font
// argument, checkFont() on damaged fonts made from the real ones, and font bytes kept alive while their atlas font
// is in use.
#include <gtest/gtest.h>

import imgui;
import imlui.lua.errors;
import imlui.resources;
import imlui.resources.directory_mount;
import imlui.theme.font_check;
import imlui.theme.font_library;
import imlui.theme.theme;
import imlui.theme.typography;
import imlui.test.headless_frame;
import imlui.test.last_item_probe;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

/// The bytes of resource `name` in `h`, as a string to change and write back.
std::string fileOf(test::HeadlessFrame& h, std::string_view name) {
    const auto bytes = h.resources().read(name);
    EXPECT_TRUE(bytes) << name;
    return bytes ? std::string{bytes->text()} : std::string{};
}

std::span<const std::byte> bytesOf(const std::string& s) { return std::as_bytes(std::span{s.data(), s.size()}); }

std::uint32_t readU32(const std::string& s, std::size_t at) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i)
        v = (v << 8) | static_cast<unsigned char>(s[at + i]);
    return v;
}

void writeU16(std::string& s, std::size_t at, std::uint32_t v) {
    s[at] = static_cast<char>((v >> 8) & 0xFF);
    s[at + 1] = static_cast<char>(v & 0xFF);
}

void writeU32(std::string& s, std::size_t at, std::uint32_t v) {
    writeU16(s, at, v >> 16);
    writeU16(s, at + 2, v & 0xFFFF);
}

/// The number of tables in the single font `s`.
std::size_t tableCount(const std::string& s) { return readU32(s, 4) >> 16; }

/// The position of table `tag`'s directory record in the single font `s`.
std::size_t recordOf(const std::string& s, std::string_view tag) {
    for (std::size_t i = 0; i < tableCount(s); ++i)
        if (std::string_view{s}.substr(12 + 16 * i, 4) == tag) return 12 + 16 * i;
    ADD_FAILURE() << "no table " << tag;
    return 0;
}

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
                                  mono = "app:/nope.ttf", bad = "../outside.ttf", worse = 42 } })");
    // Malformed entries while the table is read, then load failures: fallbacks, regular, medium, named by name.
    const std::vector<std::string> expected = {
        "theme.fonts.worse: expected a resource name such as \"app:/fonts/Inter.ttf\"",
        "theme.fonts.regular: font 'app:/fonts/Missing.ttf' not found",
        "theme.fonts.medium: 'app:/broken.ttf' is not a TrueType/OpenType font",
        "theme.fonts.bad: invalid font resource name '../outside.ttf' (expected e.g. 'app:/fonts/Inter.ttf')",
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

// ------------------------------------------------------------------ relative names

TEST_F(FontsTest, RelativeFontNamesResolveAgainstTheDeclaringScript) {
    m_dir.write("pages/local/Mono.ttf", fileOf(m_h, "app:/fonts/Roboto-Medium.ttf"));
    m_dir.write("pages/theme.lua", R"(
        ui.theme { fonts = { regular = "../fonts/Roboto-Medium.ttf", mono = "local/Mono.ttf",
                             fallback = "app:/fonts/MaterialIconsRound-Regular.otf" } }
        return Column {})");
    m_dir.write("pages/tail.lua", R"(return theme { fonts = { mono = "local/Mono.ttf" } })");
    m_dir.write("pages/state.lua", R"(return themeState { fonts = { mono = "local/Mono.ttf" } })");

    apply(R"(ui.__load("app:/pages/theme.lua"))");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(theme().fonts.regular, "app:/fonts/Roboto-Medium.ttf");
    EXPECT_EQ(theme().fonts.named.at("mono"), "app:/pages/local/Mono.ttf");
    EXPECT_EQ(theme().fonts.fallback, std::vector<std::string>{"app:/fonts/MaterialIconsRound-Regular.otf"});
    EXPECT_NE(regularFace(), m_builtinRegular);
    ASSERT_NE(namedFont("mono"), nullptr);

    // A tail call (`return theme { ... }`) still knows its script.
    apply(R"(ui.__load("app:/pages/tail.lua"))");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(theme().fonts.named.at("mono"), "app:/pages/local/Mono.ttf");

    // A themeState keeps the script it was created in for every re-application.
    apply(R"(themeStateFromPage = ui.__load("app:/pages/state.lua"))");
    apply(R"(themeStateFromPage.dark = false)");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_FALSE(theme().dark);
    EXPECT_EQ(theme().fonts.named.at("mono"), "app:/pages/local/Mono.ttf");

    // Code that isn't a script resolves against the main script (the last ui.__load).
    apply(R"(ui.theme { fonts = { mono = "local/Mono.ttf" } })");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(theme().fonts.named.at("mono"), "app:/pages/local/Mono.ttf");
}

// ------------------------------------------------------------------ the painter's font

TEST_F(FontsTest, PainterTextTakesANamedFont) {
    apply(R"(ui.theme { fonts = { mono = "app:/fonts/Roboto-Medium.ttf" } })");
    ImFont* mono = namedFont("mono");
    ASSERT_NE(mono, nullptr);
    // Text's horizontal extent in the last frame: the vertices with glyph UVs (shapes use the white pixel).
    const auto paintedWidth = [&](std::string_view root) {
        m_h.renderFrame(root);
        const ImVec2 white = ImGui::GetIO().Fonts->TexUvWhitePixel;
        float left = 3.4e38f, right = -3.4e38f;
        for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
            for (const auto& v : list->VtxBuffer)
                if (v.uv.x != white.x || v.uv.y != white.y) {
                    left = v.pos.x < left ? v.pos.x : left;
                    right = v.pos.x > right ? v.pos.x : right;
                }
        return right - left;
    };
    const std::string paint = std::string{"p:text(0, 0, \""} + sample + "\", nil, \"bodyLarge\"";
    ASSERT_EQ(m_h.run("plain = Canvas { width = 400, height = 40, onPaint = function(_, p) " + paint + ") end }\n" +
                      "mono = Canvas { width = 400, height = 40, onPaint = function(_, p) " + paint +
                      ", \"mono\") end }\n" + "unknown = Canvas { width = 400, height = 40, onPaint = function(_, p) " +
                      paint + ", \"nope\") end }"),
              "");
    const float plain = paintedWidth("plain");
    // Glyph quads include the glyphs' side bearings, so compare the two faces against each other.
    EXPECT_GT(paintedWidth("mono"), plain + 1.0f);
    EXPECT_NEAR(paintedWidth("mono") - plain, widthIn(mono, sample) - widthIn(m_builtinRegular, sample), 1.0f);
    EXPECT_NEAR(paintedWidth("unknown"), plain, 0.5f);       // undeclared: the style's face
    EXPECT_EQ(font(TextStyle::BodyLarge), m_builtinRegular); // the override ends with the call
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ damaged fonts

TEST(FontCheck, AcceptsTheRealFonts) {
    test::HeadlessFrame h;
    for (const char* name : {"imlui:/fonts/Roboto-Regular.ttf", "imlui:/fonts/Roboto-Medium.ttf",
                             "imlui:/fonts/MaterialIconsRound-Regular.otf"}) {
        const std::string font = fileOf(h, name);
        EXPECT_EQ(checkFont(bytesOf(font)), std::nullopt) << name;
    }

    // A collection holding Roboto (its table offsets count from the start of the collection).
    std::string roboto = fileOf(h, "imlui:/fonts/Roboto-Regular.ttf");
    const std::size_t tables = tableCount(roboto);
    for (std::size_t i = 0; i < tables; ++i)
        writeU32(roboto, 12 + 16 * i + 8, readU32(roboto, 12 + 16 * i + 8) + 16);
    std::string collection = "ttcf";
    collection += std::string{"\0\1\0\0\0\0\0\1\0\0\0\x10", 12};
    EXPECT_EQ(checkFont(bytesOf(collection + roboto)), std::nullopt);
    writeU32(collection, 8, 0);
    EXPECT_EQ(checkFont(bytesOf(collection + roboto)), "the font collection is empty");
}

TEST(FontCheck, RejectsDamagedFonts) {
    test::HeadlessFrame h;
    const std::string roboto = fileOf(h, "imlui:/fonts/Roboto-Regular.ttf");
    const std::string icons = fileOf(h, "imlui:/fonts/MaterialIconsRound-Regular.otf");
    const auto problem = [](const std::string& font) { return checkFont(bytesOf(font)).value_or("(accepted)"); };

    EXPECT_EQ(problem("hello, this is text"), notAFont);
    EXPECT_EQ(problem(roboto.substr(0, 11)), notAFont);
    EXPECT_EQ(problem(roboto.substr(0, 100)), "the table directory is truncated");
    EXPECT_NE(problem(roboto.substr(0, roboto.size() / 2)).find("lies outside the file"), std::string::npos);

    std::string font = roboto;
    font.replace(recordOf(font, "cmap"), 4, "xmap"); // stbtt_InitFont fails, and ImGui asserts on that
    EXPECT_EQ(problem(font), "required table 'cmap' is missing");

    font = roboto;
    writeU16(font, 4, 0xFFFF); // numTables
    EXPECT_EQ(problem(font), "the table directory is truncated");

    font = roboto;
    writeU32(font, recordOf(font, "loca") + 12, 2);
    EXPECT_EQ(problem(font), "table 'loca' is truncated");

    font = roboto;
    writeU16(font, readU32(font, recordOf(font, "hhea") + 8) + 34, 0xFFFF); // numberOfHMetrics
    EXPECT_EQ(problem(font), "table 'hmtx' is truncated");

    font = roboto;
    writeU16(font, readU32(font, recordOf(font, "head") + 8) + 50, 7); // indexToLocFormat
    EXPECT_EQ(problem(font), "table 'head' is broken");

    font = roboto;
    const std::size_t hhea = readU32(font, recordOf(font, "hhea") + 8);
    font.replace(hhea + 6, 2, font.substr(hhea + 4, 2)); // descender = ascender: ImGui would scale by 1 / 0
    EXPECT_EQ(problem(font), "table 'hhea' gives the font no height");
    // A negative height (found by the `font` fuzz target): the rasterizer's edges come out upside down, and it asserts.
    writeU16(font, hhea + 4, 0x8101); // ascender -32511, descender 0
    writeU16(font, hhea + 6, 0);
    EXPECT_EQ(problem(font), "table 'hhea' gives the font no height");
    // A height of 1 unit: ImGui would make every glyph thousands of times the font size (TrueType boxes from the
    // glyph headers, CFF ones from the charstrings).
    writeU16(font, hhea + 4, 1);
    EXPECT_NE(problem(font).find("is far larger than the font"), std::string::npos) << problem(font);
    font = icons;
    const std::size_t iconsHhea = readU32(font, recordOf(font, "hhea") + 8);
    writeU16(font, iconsHhea + 4, 1);
    writeU16(font, iconsHhea + 6, 0);
    EXPECT_NE(problem(font).find("is far larger than the font"), std::string::npos) << problem(font);

    // A TrueType outline (glyph 0, .notdef) whose contour count sends stb_truetype past its end.
    {
        const std::size_t head = readU32(roboto, recordOf(roboto, "head") + 8);
        ASSERT_EQ(static_cast<unsigned char>(roboto[head + 51]), 1); // long loca offsets
        const std::size_t loca = readU32(roboto, recordOf(roboto, "loca") + 8);
        const std::size_t glyf = readU32(roboto, recordOf(roboto, "glyf") + 8);
        ASSERT_LT(readU32(roboto, loca), readU32(roboto, loca + 4));
        font = roboto;
        writeU16(font, glyf + readU32(roboto, loca), 0x7FFF);
        EXPECT_EQ(problem(font), "the outline of glyph 0 is broken");
    }

    // CFF outlines (the icon font): stb_truetype asserts on an INDEX offset size outside 1-4.
    const std::size_t cff = readU32(icons, recordOf(icons, "CFF ") + 8);
    font = icons;
    const std::size_t nameIndex = cff + static_cast<unsigned char>(font[cff + 2]);
    font[nameIndex + 2] = 7;
    EXPECT_EQ(problem(font), "CFF Name INDEX is broken");
    font = icons;
    writeU32(font, recordOf(font, "CFF ") + 12, 3);
    EXPECT_EQ(problem(font), "CFF table is broken");

    // A charstring stb_truetype can't run (found by the `font` fuzz target): its last operator is a hintmask whose
    // mask bytes would lie past the end of the glyph's program, so stbtt_GetGlyphBox, which ImGui calls for every
    // glyph it loads, asserts. Glyph 1's program is overwritten in place with stems, then that hintmask.
    {
        const auto byte = [&](std::size_t at) -> std::size_t { return static_cast<unsigned char>(icons[at]); };
        const auto number = [&](std::size_t at, std::size_t size) {
            std::size_t v = 0;
            for (std::size_t i = 0; i < size; ++i)
                v = v << 8 | byte(at + i);
            return v;
        };
        // An INDEX at `at`: the range of element `i`, and where the INDEX ends.
        const auto element = [&](std::size_t at, std::size_t i, std::size_t* end = nullptr) {
            const std::size_t count = number(at, 2), offSize = byte(at + 2);
            const std::size_t data = at + 3 + (count + 1) * offSize - 1;
            if (end) *end = data + number(at + 3 + count * offSize, offSize);
            return std::pair{data + number(at + 3 + i * offSize, offSize),
                             data + number(at + 3 + (i + 1) * offSize, offSize)};
        };
        std::size_t at = cff + byte(cff + 2);
        static_cast<void>(element(at, 0, &at)); // Name INDEX
        const auto [topBegin, topEnd] = element(at, 0);
        // The Top DICT's CharStrings offset (operator 17): integer operands only in this font.
        std::size_t charStrings = 0, operand = 0;
        for (std::size_t i = topBegin; i < topEnd;) {
            const std::size_t b0 = byte(i);
            if (b0 == 28)
                operand = number(i + 1, 2), i += 3;
            else if (b0 == 29)
                operand = number(i + 1, 4), i += 5;
            else if (b0 >= 32 && b0 <= 246)
                operand = b0 - 139, i += 1;
            else if (b0 >= 247 && b0 <= 250)
                operand = (b0 - 247) * 256 + byte(i + 1) + 108, i += 2;
            else if (b0 >= 251 && b0 <= 254)
                i += 2;
            else {
                if (b0 == 17) charStrings = operand;
                i += b0 == 12 ? 2 : 1;
            }
        }
        ASSERT_NE(charStrings, 0u);
        const auto [begin, end] = element(cff + charStrings, 1);
        ASSERT_GE(end - begin, 3u);
        font = icons;
        // 0 0 hstem ... (each one mask bit), any spare bytes as leading zeros, then hintmask as the last byte.
        const std::size_t stems = (end - begin - 1) / 3, spare = (end - begin - 1) % 3;
        std::size_t i = begin;
        for (std::size_t k = 0; k < spare; ++k)
            font[i++] = static_cast<char>(139);
        for (std::size_t k = 0; k < stems; ++k) {
            font[i++] = static_cast<char>(139);
            font[i++] = static_cast<char>(139);
            font[i++] = 1; // hstem
        }
        font[i] = 0x13; // hintmask
        EXPECT_EQ(problem(font), "the CFF charstring of glyph 1 is broken");
    }

    // Every truncation is caught (the last table ends the file, padding aside).
    for (std::size_t size = 0; size + 4 < roboto.size(); size += 997)
        EXPECT_TRUE(checkFont(bytesOf(roboto.substr(0, size)))) << size;
    for (std::size_t size = 0; size + 4 < icons.size(); size += 997)
        EXPECT_TRUE(checkFont(bytesOf(icons.substr(0, size)))) << size;
}

TEST(FontCheck, SurvivesGarbledBytes) {
    // Random bytes in the parts it reads, for the sanitizer builds: it must never read out of bounds.
    test::HeadlessFrame h;
    const std::string roboto = fileOf(h, "imlui:/fonts/Roboto-Regular.ttf");
    const std::string icons = fileOf(h, "imlui:/fonts/MaterialIconsRound-Regular.otf");
    const std::size_t cff = readU32(icons, recordOf(icons, "CFF ") + 8);
    std::uint32_t seed = 12345;
    const auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for (int round = 0; round < 300; ++round) {
        std::string font = round % 2 ? roboto : icons;
        const std::size_t start = round % 2 ? 0 : cff;
        for (int i = 0; i < 8; ++i)
            font[start + next() % 400] = static_cast<char>(next() & 0xFF);
        static_cast<void>(checkFont(bytesOf(font)));
    }
}

namespace {

/// A big-endian byte string under construction.
struct Bytes {
    std::string s;
    Bytes& u16(std::uint32_t v) {
        s += static_cast<char>((v >> 8) & 0xFF);
        s += static_cast<char>(v & 0xFF);
        return *this;
    }
    Bytes& u32(std::uint32_t v) { return u16(v >> 16).u16(v & 0xFFFF); }
    Bytes& u8(std::uint32_t v) {
        s += static_cast<char>(v & 0xFF);
        return *this;
    }
};

/// A format 4 segment: code points first..last map to (code point + delta) mod 65536, or, with `rangeOffset`, to the
/// glyphIdArray entry that many bytes past the segment's idRangeOffset field.
struct Segment {
    std::uint32_t first, last, delta, rangeOffset;
};

/// The parts of a format 4 subtable, with the search fields the spec prescribes unless overridden.
struct Format4 {
    std::vector<Segment> segments;
    std::vector<std::uint32_t> glyphIds;
    std::optional<std::uint32_t> searchRange, entrySelector, rangeShift;

    [[nodiscard]] std::string bytes() const {
        const auto count = static_cast<std::uint32_t>(segments.size());
        std::uint32_t power = 1, log = 0;
        while (power * 2 <= count) {
            power *= 2;
            ++log;
        }
        Bytes b;
        const auto length = static_cast<std::uint32_t>(16 + 8 * segments.size() + 2 * glyphIds.size());
        b.u16(4).u16(length).u16(0).u16(2 * count);
        b.u16(searchRange.value_or(2 * power))
            .u16(entrySelector.value_or(log))
            .u16(rangeShift.value_or(2 * count - 2 * power));
        for (const auto& seg : segments)
            b.u16(seg.last);
        b.u16(0);
        for (const auto& seg : segments)
            b.u16(seg.first);
        for (const auto& seg : segments)
            b.u16(seg.delta & 0xFFFF);
        for (const auto& seg : segments)
            b.u16(seg.rangeOffset);
        for (const auto id : glyphIds)
            b.u16(id);
        return b.s;
    }
};

/// A minimal TrueType font with `glyphs` glyphs and `subtable` as its (Windows Unicode) character map. Glyph i's
/// outline is outlines[i] (padded to an even size), empty past the end of `outlines`.
std::string minimalFont(const std::string& subtable, std::uint32_t glyphs,
                        const std::vector<std::string>& outlines = {}) {
    std::vector<std::pair<std::string, std::string>> tables;
    tables.emplace_back("cmap", Bytes{}.u16(0).u16(1).u16(3).u16(1).u32(12).s + subtable);
    std::string head(54, '\0'); // indexToLocFormat (at 50) 0: short offsets
    tables.emplace_back("head", head);
    std::string hhea(36, '\0');
    writeU16(hhea, 4, 800);     // ascender
    writeU16(hhea, 6, 0xFF38);  // descender: -200
    writeU16(hhea, 34, glyphs); // numberOfHMetrics
    tables.emplace_back("hhea", hhea);
    tables.emplace_back("hmtx", std::string(4 * std::size_t{glyphs}, '\0'));
    tables.emplace_back("maxp", Bytes{}.u32(0x00005000).u16(glyphs).s);
    Bytes loca;
    std::string glyf;
    for (std::size_t i = 0; i <= glyphs; ++i) {
        loca.u16(static_cast<std::uint32_t>(glyf.size() / 2));
        if (i < outlines.size() && i < glyphs) glyf += outlines[i] + std::string(outlines[i].size() % 2, '\0');
    }
    tables.emplace_back("loca", loca.s);
    tables.emplace_back("glyf", glyf.empty() ? std::string(4, '\0') : glyf);
    Bytes font;
    font.u32(0x00010000).u16(static_cast<std::uint32_t>(tables.size())).u16(0).u16(0).u16(0);
    std::size_t offset = 12 + 16 * tables.size();
    std::string data;
    for (const auto& [tag, bytes] : tables) {
        font.s += tag;
        font.u32(0).u32(static_cast<std::uint32_t>(offset + data.size())).u32(static_cast<std::uint32_t>(bytes.size()));
        data += bytes;
        data.resize((data.size() + 3) & ~std::size_t{3}, '\0');
    }
    return font.s + data;
}

/// 0-1 -> glyphs 1-2 (by delta), A-C -> glyphs 1-3 (from glyphIdArray), and the closing 0xFFFF segment -> glyph 0.
Format4 validFormat4() {
    Format4 f;
    f.segments = {{'0', '1', 1u - '0', 0}, {'A', 'C', 0, 4}, {0xFFFF, 0xFFFF, 1, 0}};
    f.glyphIds = {1, 2, 3};
    return f;
}

} // namespace

TEST(FontCheck, CharacterMapsMapOnlyToGlyphsTheFontHas) {
    constexpr std::uint32_t glyphs = 4;
    const auto problem = [](const Format4& cmap, std::uint32_t count = glyphs) {
        return checkFont(bytesOf(minimalFont(cmap.bytes(), count))).value_or("(accepted)");
    };
    const std::string broken = "the character map is broken";
    const std::string missing = "the character map maps to glyphs the font doesn't have";
    EXPECT_EQ(problem(validFormat4()), "(accepted)");
    EXPECT_EQ(problem(validFormat4(), 0), "the font has no glyphs");

    // Format 4, idRangeOffset: the glyphIdArray entries for A-C must lie inside the subtable and name real glyphs.
    Format4 f = validFormat4();
    f.glyphIds[2] = glyphs;
    EXPECT_EQ(problem(f), missing);
    f = validFormat4();
    f.segments[1].rangeOffset = 6; // C's entry is past the end
    EXPECT_EQ(problem(f), broken);
    f.segments[1].rangeOffset = 0x7000;
    EXPECT_EQ(problem(f), broken);
    f = validFormat4();
    f.segments[1].first = 'D'; // first > last: never matched, so its offset doesn't matter
    f.segments[1].rangeOffset = 0x7000;
    EXPECT_EQ(problem(f), "(accepted)");

    // Format 4, idDelta: 0-1 map to glyphs 3-4, and 4 is past the last glyph; a wrap past 0xFFFF reaches it too.
    f = validFormat4();
    f.segments[0].delta = 3u - '0';
    EXPECT_EQ(problem(f), missing);
    f = validFormat4();
    f.segments[0].delta = 0x10000u - '1'; // '0' -> 0xFFFF, '1' -> 0
    EXPECT_EQ(problem(f), missing);

    // Format 4 search fields: stb_truetype's binary search trusts them, so they must not reach past the segments.
    f = validFormat4();
    f.rangeShift = 6; // starts at segment 3 of 3
    EXPECT_EQ(problem(f), broken);
    f = validFormat4();
    f.searchRange = 8;
    f.entrySelector = 2; // segment 1 + 2 + 1
    EXPECT_EQ(problem(f), broken);
    f = validFormat4();
    f.searchRange = 0;
    f.entrySelector = 0xFFFF; // steps that add nothing, but stb_truetype takes every one on every lookup
    EXPECT_EQ(problem(f), broken);
    f = validFormat4();
    f.segments.clear();
    f.glyphIds.clear();
    f.rangeShift = 0;
    EXPECT_EQ(problem(f), broken);

    // Formats 6, 12 and 13.
    const auto check = [](const std::string& subtable) {
        return checkFont(bytesOf(minimalFont(subtable, glyphs))).value_or("(accepted)");
    };
    const auto format6 = [](std::uint32_t glyph) {
        return Bytes{}.u16(6).u16(14).u16(0).u16('A').u16(2).u16(1).u16(glyph).s;
    };
    EXPECT_EQ(check(format6(3)), "(accepted)");
    EXPECT_EQ(check(format6(glyphs)), missing);
    const auto format12 = [](std::uint32_t format, std::uint32_t first, std::uint32_t last, std::uint32_t glyph) {
        return Bytes{}.u16(format).u16(0).u32(28).u32(0).u32(1).u32(first).u32(last).u32(glyph).s;
    };
    EXPECT_EQ(check(format12(12, 'A', 'C', 1)), "(accepted)");     // glyphs 1-3
    EXPECT_EQ(check(format12(12, 'A', 'D', 1)), missing);          // glyphs 1-4
    EXPECT_EQ(check(format12(12, 'A', 'A', 0xFFFFFFFF)), missing); // no wrap to a negative index
    EXPECT_EQ(check(format12(12, 'D', 'A', 9)), "(accepted)");     // first > last: never matched
    EXPECT_EQ(check(format12(13, 'A', 0x10FFFF, 3)), "(accepted)");
    EXPECT_EQ(check(format12(13, 'A', 'A', glyphs)), missing);
}

namespace {

/// A glyph's box: xMin, yMin, xMax, yMax.
using Box = std::array<std::int32_t, 4>;

/// A glyph's header: its number of contours (negative: a composite glyph) and its box.
Bytes glyphHeader(std::int32_t contours, Box box = {0, 0, 100, 100}) {
    Bytes b;
    b.u16(static_cast<std::uint32_t>(contours) & 0xFFFF);
    for (const auto v : box)
        b.u16(static_cast<std::uint32_t>(v) & 0xFFFF);
    return b;
}

/// A simple glyph of one contour through `points` (x, y, on the curve), with word coordinates.
std::string contourGlyph(const std::vector<std::tuple<std::int32_t, std::int32_t, bool>>& points, Box box) {
    Bytes b = glyphHeader(1, box);
    b.u16(static_cast<std::uint32_t>(points.size() - 1)).u16(0);
    for (const auto& [x, y, on] : points)
        b.u8(on ? 0x01 : 0x00);
    std::int32_t last = 0;
    for (const auto& point : points) {
        b.u16(static_cast<std::uint32_t>(std::get<0>(point) - last) & 0xFFFF);
        last = std::get<0>(point);
    }
    last = 0;
    for (const auto& point : points) {
        b.u16(static_cast<std::uint32_t>(std::get<1>(point) - last) & 0xFFFF);
        last = std::get<1>(point);
    }
    return b.s;
}

/// A simple glyph whose contours end at `endPts`, its points (as many as the last end point says) on the curve
/// unless `on` says otherwise, each 10 units right of and above the one before (word coordinates).
std::string simpleGlyph(const std::vector<std::uint32_t>& endPts, const std::vector<bool>& on = {}) {
    Bytes b = glyphHeader(static_cast<std::int32_t>(endPts.size()));
    for (const auto end : endPts)
        b.u16(end);
    b.u16(0); // no instructions
    const std::size_t points = std::size_t{endPts.back()} + 1;
    for (std::size_t i = 0; i < points; ++i)
        b.u8(i < on.size() && !on[i] ? 0x00 : 0x01);
    for (std::size_t i = 0; i < 2 * points; ++i)
        b.u16(10);
    return b.s;
}

/// A composite glyph of `parts`, each placed by a word offset (dx, 0) and given extra `flags`, and `scale` (the
/// words of its scale, if the flags ask for one).
std::string compositeGlyph(const std::vector<std::uint32_t>& parts, std::uint32_t flags = 0,
                           const std::vector<std::uint32_t>& scale = {}, std::uint32_t dx = 0) {
    Bytes b = glyphHeader(-1);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::uint32_t more = i + 1 < parts.size() ? 0x20 : 0;
        b.u16(0x0003 | flags | more).u16(parts[i]).u16(dx).u16(0); // ARG_1_AND_2_ARE_WORDS, ARGS_ARE_XY_VALUES
        for (const auto word : scale)
            b.u16(word);
    }
    return b.s;
}

} // namespace

TEST(FontCheck, GlyphOutlinesAreReadLikeStbTruetype) {
    // stb_truetype decodes a glyph's outline when ImGui rasterizes it, trusting every count and offset, and fetches
    // the parts of a composite glyph by recursion.
    const std::string cmap = Bytes{}.u16(6).u16(12).u16(0).u16('A').u16(1).u16(1).s; // 'A' -> glyph 1
    const auto problem = [&](const std::vector<std::string>& outlines) {
        const auto glyphs = static_cast<std::uint32_t>(std::max<std::size_t>(outlines.size(), 2));
        return checkFont(bytesOf(minimalFont(cmap, glyphs, outlines))).value_or("(accepted)");
    };
    const std::string accepted = "(accepted)", broken = "the outline of glyph 1 is broken";
    const std::string triangle = simpleGlyph({2});
    EXPECT_EQ(problem({"", triangle}), accepted);

    // Simple glyphs: the flags (with repeat counts) and the coordinates they announce must lie inside the glyph.
    EXPECT_EQ(problem({"", glyphHeader(1).u16(2).u16(0).u8(0x3F).u8(2).u8(1).u8(2).u8(3).u8(1).u8(2).u8(3).s}),
              accepted); // one flag repeated: byte coordinates, positive
    EXPECT_EQ(problem({"", triangle.substr(0, triangle.size() - 2)}), broken);              // a y coordinate short
    EXPECT_EQ(problem({"", glyphHeader(1).u16(0).u16(1).u8(0).u8(0x08).s}), broken);        // the repeat count missing
    EXPECT_EQ(problem({"", glyphHeader(1).u16(0).u16(100).u8(1).u16(0).u16(0).s}), broken); // instructions
    EXPECT_EQ(problem({"", glyphHeader(100).u16(0).u16(0).s}), broken);                     // end points
    EXPECT_EQ(problem({"", glyphHeader(1).u16(0xFFFF).u16(0).u8(1).u16(0).u16(0).s}), broken); // 65536 points
    // A contour that starts off the curve also reads the next point: in a glyph that is drawn, the last contour
    // can't be a lone point off the curve. Fonts have such glyphs with an empty box as placeholders (Calibri,
    // Consolas): ImGui never draws those, nor decodes them, unless a drawn composite glyph uses one.
    const std::string lone = simpleGlyph({2, 3}, {true, true, true, false});
    const std::string placeholder = glyphHeader(1, {0, 0, 0, 0}).s + simpleGlyph({0}, {false}).substr(10);
    EXPECT_EQ(problem({"", lone}), broken);
    EXPECT_EQ(problem({"", placeholder}), accepted);
    EXPECT_EQ(problem({"", placeholder, compositeGlyph({1})}), "the outline of glyph 2 is broken");
    EXPECT_EQ(problem({"", simpleGlyph({0, 3}, {false, true, true, true})}), accepted);
    // End points out of order: stb_truetype takes the points from the last one and copes with the rest.
    EXPECT_EQ(problem({"", simpleGlyph({5, 2})}), accepted);

    // Composite glyphs: every part, with its offset and scale, inside the glyph; parts past the font are empty.
    EXPECT_EQ(problem({"", triangle, compositeGlyph({1, 1})}), accepted);
    EXPECT_EQ(problem({"", triangle, compositeGlyph({7})}), accepted);
    EXPECT_EQ(problem({"", triangle, compositeGlyph({1}, 0x08, {0x2000})}), accepted);                    // a scale
    EXPECT_EQ(problem({"", triangle, compositeGlyph({1}, 0x40, {0x2000, 0x4000})}), accepted);            // x and y
    EXPECT_EQ(problem({"", triangle, compositeGlyph({1}, 0x80, {0x4000, 0, 0x1000, 0x4000})}), accepted); // 2x2
    EXPECT_EQ(problem({"", triangle, compositeGlyph({1}, 0x80, {0x4000, 0})}),
              "the outline of glyph 2 is broken"); // the matrix cut short
    EXPECT_EQ(problem({"", triangle, glyphHeader(-1).u16(0x23).u16(1).u16(0).u16(0).s}),
              "the outline of glyph 2 is broken"); // another part announced, none there
    EXPECT_EQ(problem({"", triangle, glyphHeader(-1).u16(0x0000).u16(1).u8(0).u8(0).s}),
              "the outline of glyph 2 places a part by its points"); // stb_truetype asserts

    // Parts that contain the glyph itself: stb_truetype would recurse until the stack overflows.
    EXPECT_EQ(problem({"", compositeGlyph({1})}), "the outline of glyph 1 contains itself");
    EXPECT_EQ(problem({"", compositeGlyph({2}), compositeGlyph({1})}), "the outline of glyph 1 contains itself");
    EXPECT_EQ(problem({compositeGlyph({0})}), "the outline of glyph 0 contains itself");

    // Composite glyphs inside composite glyphs: at most 16 levels.
    const auto chain = [&](std::uint32_t last) {
        std::vector<std::string> outlines{"", triangle};
        for (std::uint32_t k = 2; k <= last; ++k)
            outlines.push_back(compositeGlyph({k - 1}));
        return outlines;
    };
    EXPECT_EQ(problem(chain(17)), accepted);
    EXPECT_EQ(problem(chain(18)), "the outline of glyph 17 is nested too deeply");

    // Each level using the one below three times: the vertices (and stb_truetype's copying) grow by 3 per level.
    std::vector<std::string> tree{"", triangle};
    for (std::uint32_t k = 2; k <= 14; ++k)
        tree.push_back(compositeGlyph({k - 1, k - 1, k - 1}));
    EXPECT_NE(problem(tree).find("is too complex"), std::string::npos) << problem(tree);

    // A part moved where its coordinates don't fit stb_truetype's 16-bit vertices (a float-to-int conversion out
    // of range): the point at x = 30000 moved right by 2000 fits, by 3000 doesn't, nor scaled by 2.
    const std::string far = glyphHeader(1).u16(0).u16(0).u8(0x01).u16(30000).u16(0).s;
    EXPECT_EQ(problem({"", far, compositeGlyph({1}, 0, {}, 2000)}), accepted);
    EXPECT_EQ(problem({"", far, compositeGlyph({1}, 0, {}, 3000)}), "the outline of glyph 2 moves a part out of range");
    EXPECT_EQ(problem({"", far, compositeGlyph({1}, 0x08, {0x7FFF})}),
              "the outline of glyph 2 moves a part out of range");

    // The box ImGui sizes a glyph's bitmap by: not inverted, nor more than 16 times the font's height (1000 here).
    EXPECT_EQ(problem({"", glyphHeader(1, {100, 0, 0, 100}).s + triangle.substr(10)}),
              "the outline of glyph 1 has an inverted box");
    EXPECT_EQ(problem({"", glyphHeader(1, {0, 0, 100, -100}).s + triangle.substr(10)}),
              "the outline of glyph 1 has an inverted box");
    EXPECT_EQ(problem({"", glyphHeader(1, {-8000, 0, 8000, 100}).s + triangle.substr(10)}), accepted);
    EXPECT_EQ(problem({"", glyphHeader(1, {-8000, 0, 8001, 100}).s + triangle.substr(10)}),
              "the outline of glyph 1 is far larger than the font");

    // A glyph whose box ends at or below the baseline (found by the `font` fuzz target): its bitmap can start at the
    // baseline, and stb_truetype's rasterizer asserts on an edge wholly above a bitmap's first row there. Its
    // outline may stray from its box (real fonts' do) but not above the baseline, curves included.
    const Box below{0, -300, 300, 0};
    EXPECT_EQ(problem({"", contourGlyph({{0, -100, true}, {100, -300, true}, {200, 0, true}}, below)}), accepted);
    EXPECT_EQ(problem({"", contourGlyph({{0, -100, true}, {100, -300, true}, {200, 10, true}}, below)}),
              "the outline of glyph 1 rises above its box");
    EXPECT_EQ(problem({"", contourGlyph({{0, -40, true}, {50, 0, false}, {100, -40, true}}, below)}), accepted);
    EXPECT_EQ(problem({"", contourGlyph({{0, 0, true}, {50, 40, false}, {100, 0, true}}, below)}),
              "the outline of glyph 1 rises above its box"); // the curve peaks at 20
    const std::string riser = contourGlyph({{0, -100, true}, {100, -300, true}, {200, 10, true}}, {0, 0, 300, 300});
    EXPECT_EQ(problem({"", riser}), accepted); // its box doesn't end below the baseline
    EXPECT_EQ(problem({"", riser, glyphHeader(-1, below).s + compositeGlyph({1}).substr(10)}),
              "the outline of glyph 2 rises above its box");
}

TEST_F(FontsTest, DamagedFontsAreReportedInsteadOfAborting) {
    std::string garbled = fileOf(m_h, "app:/fonts/Roboto-Medium.ttf");
    garbled.replace(recordOf(garbled, "cmap"), 4, "xmap");
    m_dir.write("garbled.ttf", garbled);
    m_dir.write("truncated.ttf", fileOf(m_h, "app:/fonts/Roboto-Medium.ttf").substr(0, 5000));
    std::string cff = fileOf(m_h, "app:/fonts/MaterialIconsRound-Regular.otf");
    const std::size_t at = readU32(cff, recordOf(cff, "CFF ") + 8);
    cff[at + static_cast<unsigned char>(cff[at + 2]) + 2] = 9;
    m_dir.write("cff.otf", cff);

    apply(R"(ui.theme { fonts = { regular = "garbled.ttf", medium = "truncated.ttf", icons = "cff.otf" } })");
    const std::vector<std::string> expected = {
        "theme.fonts.regular: font 'app:/garbled.ttf' is damaged: required table 'cmap' is missing",
        "theme.fonts.medium: font 'app:/truncated.ttf' is damaged: table 'GDEF' lies outside the file",
        "theme.fonts.icons: font 'app:/cff.otf' is damaged: CFF Name INDEX is broken",
    };
    EXPECT_EQ(luaui::errors(), expected);
    EXPECT_EQ(regularFace(), m_builtinRegular);
    EXPECT_EQ(mediumFace(), m_builtinMedium);
    EXPECT_EQ(namedFont("icons"), nullptr);
    m_h.renderFrame("nothing");
}

// ------------------------------------------------------------------ font bytes and hot reload

TEST_F(FontsTest, AFontInUseKeepsItsBytesAlive) {
    m_dir.write("Custom.ttf", fileOf(m_h, "app:/fonts/Roboto-Medium.ttf"));
    const std::string theme = R"(ui.theme { fonts = { custom = "app:/Custom.ttf" } })";
    apply(theme);
    ASSERT_NE(namedFont("custom"), nullptr);
    std::weak_ptr<const void> first;
    {
        const auto bytes = m_h.resources().read("app:/Custom.ttf");
        ASSERT_TRUE(bytes && bytes->owner());
        first = bytes->owner();
    }

    // The file changes: the mount's cache moves on to the new bytes, the font still uses the old ones.
    m_dir.write("Custom.ttf", fileOf(m_h, "app:/fonts/Roboto-Regular.ttf"));
    m_dir.touch("Custom.ttf", 10);
    ASSERT_TRUE(m_h.resources().read("app:/Custom.ttf"));
    EXPECT_FALSE(first.expired()) << "the atlas font still points into them";
    m_h.renderFrame("nothing");

    // The theme is applied again: the new font replaces the old one, whose bytes go with it.
    apply(theme);
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_TRUE(first.expired());
    m_h.renderFrame("nothing");
}
