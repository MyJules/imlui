// The Fluent foundation: design "fluent" resolves FluentSkin, its Button follows the tokens (`theme { tokens = ... }`)
// and maps every Material variant, its colors derive from the scheme roles (dark, light, overrides), the ImGui style
// follows the tokens under it, the Material skin's chrome hooks give the values the elements always used, and the
// design switches at runtime both ways.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.skin;
import imlui.skin.fluent.button;
import imlui.skin.fluent.common;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.scheme;
import imlui.theme.skin_tokens;
import imlui.theme.theme;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.button;
import imlui.widgets.common;
import imlui.test.headless_frame;
import imlui.widgets.top_app_bar;

using namespace imlui;

namespace {

/// Records where it renders: a 1px item.
class Spot final : public Element {
public:
    static void declare(TypeBuilder<Spot>&) {}
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{1.0f, 1.0f}; }
    void render(Frame&) override {
        m_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy({1.0f, 1.0f});
    }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }

private:
    ImVec2 m_pos{};
};

/// Every color of fluent::Colors.
constexpr Color fluent::Colors::* allColors[] = {
    &fluent::Colors::accent,
    &fluent::Colors::accentHover,
    &fluent::Colors::accentPressed,
    &fluent::Colors::accentDisabled,
    &fluent::Colors::onAccent,
    &fluent::Colors::onAccentDisabled,
    &fluent::Colors::accentStroke,
    &fluent::Colors::accentBottomStroke,
    &fluent::Colors::accentText,
    &fluent::Colors::control,
    &fluent::Colors::controlHover,
    &fluent::Colors::controlPressed,
    &fluent::Colors::controlDisabled,
    &fluent::Colors::controlInputActive,
    &fluent::Colors::controlStroke,
    &fluent::Colors::controlBottomStroke,
    &fluent::Colors::controlStrongStroke,
    &fluent::Colors::controlStrongStrokeDisabled,
    &fluent::Colors::controlAlt,
    &fluent::Colors::controlAltHover,
    &fluent::Colors::controlAltPressed,
    &fluent::Colors::subtle,
    &fluent::Colors::subtleHover,
    &fluent::Colors::subtlePressed,
    &fluent::Colors::text,
    &fluent::Colors::textSecondary,
    &fluent::Colors::textTertiary,
    &fluent::Colors::textDisabled,
    &fluent::Colors::background,
    &fluent::Colors::layer,
    &fluent::Colors::card,
    &fluent::Colors::cardStroke,
    &fluent::Colors::flyout,
    &fluent::Colors::flyoutStroke,
    &fluent::Colors::smoke,
    &fluent::Colors::divider,
    &fluent::Colors::focusOuter,
    &fluent::Colors::focusInner,
};

bool inUnitRange(Color c) {
    for (const float v : {c.r, c.g, c.b, c.a})
        if (!(v >= 0.0f && v <= 1.0f)) return false;
    return true;
}

class FluentSkinTest : public ::testing::Test {
protected:
    FluentSkinTest() { m_h.registerType<Spot>("Spot"); }
    ~FluentSkinTest() override {
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override { applyTheme("theme {}"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    /// Applies the theme the script set, between frames, as the Engine does.
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
    }
    /// The x distance from Spot `a`'s item to Spot `b`'s, less `a`'s 1px: what lies between them in a Row.
    float across(std::string_view a, std::string_view b) {
        return m_h.instance<Spot>(b)->pos().x - m_h.instance<Spot>(a)->pos().x - 1.0f;
    }
    /// The same in y, in a Column.
    float down(std::string_view a, std::string_view b) {
        return m_h.instance<Spot>(b)->pos().y - m_h.instance<Spot>(a)->pos().y - 1.0f;
    }
    /// A button's width and height as laid out: `button` between Spots in a Row and in a Column.
    ImVec2 laidOut(std::string_view button) {
        run(std::format(R"(
            a, b, c, d = Spot {{}}, Spot {{}}, Spot {{}}, Spot {{}}
            root = Column {{ spacing = 0, Row {{ spacing = 0, a, {0}, b }}, c, {0}, d }})",
                        button));
        m_h.renderFrames("root", 2);
        return {across("a", "b"), down("c", "d")};
    }
    [[nodiscard]] static const FluentSkin& fluentSkin() {
        const auto* f = dynamic_cast<const FluentSkin*>(&skin());
        EXPECT_NE(f, nullptr);
        return *f;
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

TEST_F(FluentSkinTest, DesignFluentResolvesTheFluentSkin) {
    EXPECT_TRUE(hasSkin("fluent"));
    EXPECT_EQ(skin().name(), "material");
    EXPECT_EQ(dynamic_cast<const FluentSkin*>(&skin()), nullptr);
    applyTheme(R"(theme { design = "fluent" })");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(theme().design, "fluent");
    EXPECT_EQ(skin().name(), "fluent");
    EXPECT_NE(dynamic_cast<const FluentSkin*>(&skin()), nullptr);
    EXPECT_EQ(skin().tokens(), fluentTokens());
}

// The Material skin's tokens describe its own metrics and ignore the overrides.
TEST_F(FluentSkinTest, MaterialTokensAreInformational) {
    applyTheme(R"(theme { tokens = { controlHeight = 50 } })");
    EXPECT_EQ(skin().tokens(), materialTokens());
}

TEST_F(FluentSkinTest, ButtonSizesFollowTheTokens) {
    applyTheme(R"(theme { design = "fluent" })");
    const ImVec2 ok = laidOut(R"(Button { "OK" })");
    EXPECT_FLOAT_EQ(ok.y, dp(32));
    // paddingX on both sides of the label, at least as wide as high.
    EXPECT_FLOAT_EQ(ok.x, std::max(md::measure(fluent::bodyStyle, "OK").x + 2.0f * dp(12), dp(32)));

    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40 } })");
    EXPECT_FLOAT_EQ(skin().tokens().controlHeight, 40.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(Button { "OK" })").y, dp(40));

    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 30, density = 2, paddingX = 0 } })");
    const ImVec2 dense = laidOut(R"(Button { "OK" })");
    EXPECT_FLOAT_EQ(dense.y, dp(60));
    EXPECT_FLOAT_EQ(dense.x, dp(60)); // no padding: the label is narrower than the height
    EXPECT_TRUE(luaui::errors().empty());

    // A fixed size wins; an icon adds its size and the gap.
    applyTheme(R"(theme { design = "fluent" })");
    EXPECT_FLOAT_EQ(laidOut(R"(Button { "OK", width = 150, height = 20 })").x, 150.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(Button { "OK", width = 150, height = 20 })").y, 20.0f);
    const float withIcon = laidOut(R"(Button { "OK", icon = "add" })").x;
    EXPECT_FLOAT_EQ(withIcon, ok.x + dp(16) + dp(8));
}

// filled/tonal -> accent, outlined/elevated -> standard, text -> subtle, fab -> accent and larger.
TEST_F(FluentSkinTest, VariantsMapToFluentFills) {
    EXPECT_EQ(fluent::variantFill(md::ButtonVariant::Filled), fluent::Fill::Accent);
    EXPECT_EQ(fluent::variantFill(md::ButtonVariant::Tonal), fluent::Fill::Accent);
    EXPECT_EQ(fluent::variantFill(md::ButtonVariant::Outlined), fluent::Fill::Standard);
    EXPECT_EQ(fluent::variantFill(md::ButtonVariant::Elevated), fluent::Fill::Standard);
    EXPECT_EQ(fluent::variantFill(md::ButtonVariant::Text), fluent::Fill::Subtle);
    EXPECT_EQ(fluent::variantFill(md::ButtonVariant::Fab), fluent::Fill::Accent);

    applyTheme(R"(theme { design = "fluent" })");
    const fluent::Look look = fluentSkin().look();
    const fluent::Colors& c = look.colors;
    using fluent::Fill, fluent::State;
    EXPECT_EQ(fluent::controlColors(look, Fill::Accent, State::Rest).fill, c.accent);
    EXPECT_EQ(fluent::controlColors(look, Fill::Accent, State::Rest).text, c.onAccent);
    EXPECT_EQ(fluent::controlColors(look, Fill::Accent, State::Hover).fill, c.accentHover);
    EXPECT_EQ(fluent::controlColors(look, Fill::Accent, State::Pressed).fill, c.accentPressed);
    EXPECT_EQ(fluent::controlColors(look, Fill::Accent, State::Disabled).fill, c.accentDisabled);
    EXPECT_EQ(fluent::controlColors(look, Fill::Standard, State::Rest).fill, c.control);
    EXPECT_EQ(fluent::controlColors(look, Fill::Standard, State::Rest).bottomStroke, c.controlBottomStroke);
    EXPECT_EQ(fluent::controlColors(look, Fill::Standard, State::Disabled).text, c.textDisabled);
    EXPECT_EQ(fluent::controlColors(look, Fill::Subtle, State::Rest).fill.a, 0.0f);
    EXPECT_EQ(fluent::controlColors(look, Fill::Subtle, State::Hover).fill, c.subtleHover);

    // Every variant draws at the control height but the FAB, which is larger; a FAB without a label is square.
    const float h = laidOut(R"(Button { "OK" })").y;
    for (const char* variant : {"tonal", "outlined", "elevated", "text"})
        EXPECT_FLOAT_EQ(laidOut(std::format(R"(Button {{ "OK", variant = "{}" }})", variant)).y, h) << variant;
    EXPECT_FLOAT_EQ(laidOut(R"(Button { "OK", variant = "fab" })").y, h + dp(16));
    const ImVec2 fab = laidOut(R"(Button { "", icon = "add", variant = "fab" })");
    EXPECT_FLOAT_EQ(fab.x, fab.y);
    EXPECT_TRUE(luaui::errors().empty());

    // buttonFill = "outline": the accent fills become an accent outline.
    applyTheme(R"(theme { design = "fluent", tokens = { buttonFill = "outline" } })");
    const fluent::Look outline = fluentSkin().look();
    EXPECT_EQ(fluent::controlColors(outline, Fill::Accent, State::Rest).fill.a, 0.0f);
    EXPECT_EQ(fluent::controlColors(outline, Fill::Accent, State::Rest).stroke, outline.colors.accent);
    EXPECT_EQ(fluent::controlColors(outline, Fill::Accent, State::Rest).text, outline.colors.accentText);
}

// Clicks and the disabled state go through the Fluent button like through Material's.
TEST_F(FluentSkinTest, ButtonClicksAndDisables) {
    applyTheme(R"(theme { design = "fluent" })");
    run(R"(
        clicks = 0
        a, b = Spot {}, Spot {}
        root = Column { spacing = 0,
            a, Button { "Go", width = 100, onClicked = function() clicks = clicks + 1 end },
            b, Button { "Off", width = 100, enabled = false, onClicked = function() clicks = clicks + 10 end } })");
    m_h.renderFrames("root", 2);
    const float h = dp(32);
    for (const char* spot : {"a", "b"}) {
        const ImVec2 at = m_h.instance<Spot>(spot)->pos();
        m_h.click("root", at.x + 50.0f, at.y + 1.0f + h * 0.5f, 2);
    }
    EXPECT_EQ(m_h.run("assert(clicks == 1, 'clicks: ' .. clicks)"), "");
}

TEST_F(FluentSkinTest, ColorsDeriveFromTheSchemeRoles) {
    const Scheme dark = generateScheme(Theme{}.seed, true), light = generateScheme(Theme{}.seed, false);
    const fluent::Colors d = fluent::colorsFor(dark, true), l = fluent::colorsFor(light, false);
    int differ = 0;
    for (const auto member : allColors) {
        EXPECT_TRUE(inUnitRange(d.*member));
        EXPECT_TRUE(inUnitRange(l.*member));
        differ += d.*member != l.*member ? 1 : 0;
    }
    EXPECT_GT(differ, 30);
    EXPECT_EQ(d.accent, dark.primary);
    EXPECT_EQ(d.onAccent, dark.onPrimary);
    // Dark: light text on a dark background; light: the other way round.
    const auto luma = [](Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
    EXPECT_GT(luma(d.text), luma(d.background));
    EXPECT_LT(luma(l.text), luma(l.background));

    // A script's role override reaches the Fluent colors.
    applyTheme(R"(theme { design = "fluent", dark = false, colors = { primary = "#FF0000", onSurface = "#102030" } })");
    EXPECT_TRUE(luaui::errors().empty());
    const fluent::Colors& o = fluentSkin().colors();
    EXPECT_EQ(o.accent, (Color{1, 0, 0, 1}));
    EXPECT_EQ(o.accentText, (Color{1, 0, 0, 1}));
    EXPECT_EQ(o.text, (*parseHex("#102030")).withAlpha(0.8956f));
    EXPECT_EQ(o, fluent::colorsFor(theme().scheme, false)); // and the cache followed the theme
}

// Under design "fluent" (preset "material") ImGui's own style follows the tokens and the Fluent colors; the `style`
// and `imgui` overrides still apply after it, and the other presets are untouched.
TEST_F(FluentSkinTest, ImGuiStyleFollowsTheTokens) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float materialPopup = st.PopupRounding;
    applyTheme(R"(theme { design = "fluent" })");
    EXPECT_FLOAT_EQ(st.FrameRounding, std::trunc(dp(4)));
    EXPECT_FLOAT_EQ(st.PopupRounding, std::trunc(dp(8)));
    EXPECT_NE(st.PopupRounding, materialPopup);
    EXPECT_FLOAT_EQ(st.FrameBorderSize, std::trunc(dp(1)));
    const fluent::Colors& c = fluentSkin().colors();
    EXPECT_EQ(Color::from(st.Colors[ImGuiCol_WindowBg]), c.background);
    EXPECT_EQ(Color::from(st.Colors[ImGuiCol_PopupBg]), c.flyout);
    EXPECT_EQ(fluentSkin().applicationBackground(), std::optional<Color>{c.background});

    applyTheme(R"(theme { design = "fluent", tokens = { controlRadius = 6, overlayRadius = 0 } })");
    EXPECT_FLOAT_EQ(st.FrameRounding, std::trunc(dp(6)));
    EXPECT_FLOAT_EQ(st.PopupRounding, 0.0f);

    applyTheme(R"(theme { design = "fluent", style = { FrameRounding = 9 }, imgui = { WindowBg = "#123456" } })");
    EXPECT_FLOAT_EQ(st.FrameRounding, std::trunc(dp(9)));
    EXPECT_EQ(toHex(Color::from(st.Colors[ImGuiCol_WindowBg])), "#123456");
    EXPECT_EQ(skin().applicationBackground(), std::nullopt); // the override shows at the root

    applyTheme(R"(theme { design = "fluent", preset = "imgui-light" })");
    ImGuiStyle stock;
    ImGui::StyleColorsLight(&stock);
    EXPECT_EQ(Color::from(st.Colors[ImGuiCol_WindowBg]), Color::from(stock.Colors[ImGuiCol_WindowBg]));
}

// The Material skin's chrome hooks are what the elements used before they asked the skin.
TEST_F(FluentSkinTest, MaterialChromeIsTheElementsOldLook) {
    const Skin material;
    const Scheme& s = theme().scheme;
    EXPECT_EQ(material.applicationBackground(), std::nullopt);
    const Skin::WindowChrome w = material.windowChrome();
    EXPECT_EQ(w.background, s.surfaceContainerLow);
    EXPECT_EQ(w.titleBar, s.surfaceContainer);
    EXPECT_EQ(w.titleBarActive, s.surfaceContainerHigh);
    EXPECT_EQ(w.border, s.outlineVariant);
    EXPECT_FLOAT_EQ(w.borderSize, dp(1));
    EXPECT_FLOAT_EQ(material.scrollViewRounding(), dp(theme().shape.medium));
    EXPECT_EQ(material.navigationBarColor(), s.surfaceContainer);
    EXPECT_EQ(material.navigationRailColor(), s.surface);
    using md::TopAppBarVariant;
    for (const auto& [name, variant, height] :
         {std::tuple{"small", TopAppBarVariant::Small, 64.0f}, std::tuple{"center", TopAppBarVariant::Center, 64.0f},
          std::tuple{"medium", TopAppBarVariant::Medium, 112.0f},
          std::tuple{"large", TopAppBarVariant::Large, 152.0f}}) {
        const Skin::TopAppBarChrome bar = material.topAppBarChrome(variant);
        EXPECT_FLOAT_EQ(bar.rowHeight, dp(64)) << name;
        EXPECT_FLOAT_EQ(bar.height, dp(height)) << name;
        EXPECT_EQ(bar.title, s.onSurface);
    }
    const Skin::TableChrome table = material.tableChrome();
    EXPECT_EQ(table.border, s.outlineVariant);
    EXPECT_EQ(table.header, s.secondaryContainer);
    EXPECT_EQ(table.selectedText, s.onSecondaryContainer);
    EXPECT_FLOAT_EQ(table.cellPaddingX, dp(16));
    EXPECT_FLOAT_EQ(table.headerHeight, dp(56));
    EXPECT_EQ(material.separatorTextColor(), s.onSurfaceVariant);
    EXPECT_EQ(material.linkColor(), std::nullopt);
}

// material -> fluent -> material at runtime: the layout follows each design's sizes, and comes back to Material's.
TEST_F(FluentSkinTest, SwitchingDesignsAtRuntime) {
    const ImVec2 material = laidOut(R"(Button { "Switch" })");
    applyTheme(R"(theme { design = "fluent" })");
    const ImVec2 fluent = laidOut(R"(Button { "Switch" })");
    EXPECT_NE(fluent.y, material.y);
    EXPECT_NE(fluent.x, material.x);
    applyTheme(R"(theme { design = "material" })");
    EXPECT_EQ(skin().name(), "material");
    const ImVec2 back = laidOut(R"(Button { "Switch" })");
    EXPECT_FLOAT_EQ(back.x, material.x);
    EXPECT_FLOAT_EQ(back.y, material.y);
    // themeState switches too.
    run(R"(ts = themeState { design = "fluent" })");
    ASSERT_TRUE(applyPendingTheme());
    EXPECT_EQ(skin().name(), "fluent");
    run(R"(ts.design = "material")");
    ASSERT_TRUE(applyPendingTheme());
    EXPECT_EQ(skin().name(), "material");
}
