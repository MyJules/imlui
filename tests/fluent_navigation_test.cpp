// The Fluent navigation: drawer, rail and bar destinations, tabs and the modal drawer. Their sizes follow the tokens
// (density included), fixed widths resolve, clicks select like under Material (disabled ones don't), the tab bar's
// variants pick their tabs, the drawer opens, closes on Escape and on the smoke, and the chrome hooks give Fluent's
// container colors.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.skin;
import imlui.skin.fluent.common;
import imlui.skin.fluent.tabs;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.theme;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.common;
import imlui.widgets.tabs;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

/// Records where it renders, and how often: a 1px item.
class Spot final : public Element {
public:
    static void declare(TypeBuilder<Spot>&) {}
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{1.0f, 1.0f}; }
    void render(Frame&) override {
        m_pos = ImGui::GetCursorScreenPos();
        ++m_renders;
        ImGui::Dummy({1.0f, 1.0f});
    }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    ImVec2 m_pos{};
    int m_renders = 0;
};

/// What InFrame runs when it renders: calls that need a current ImGui window (a size resolving a width).
std::function<void()> inFrameCall;

/// Runs inFrameCall while it renders.
class InFrame final : public Element {
public:
    static void declare(TypeBuilder<InFrame>&) {}
    void render(Frame&) override {
        if (inFrameCall) inFrameCall();
    }
};

class FluentNavigationTest : public ::testing::Test {
protected:
    FluentNavigationTest() {
        m_h.registerType<Spot>("Spot");
        m_h.registerType<InFrame>("InFrame");
    }
    ~FluentNavigationTest() override {
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override { applyTheme(R"(theme { design = "fluent" })"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void check(std::string_view expr) { EXPECT_EQ(m_h.run("assert(" + std::string{expr} + ")"), "") << expr; }
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
        ASSERT_EQ(skin().name(), "fluent");
    }
    void frames(int n = 1) { m_h.renderFrames("root", n); }
    void click(ImVec2 at) { m_h.click("root", at.x, at.y); }
    /// Runs `f` inside a frame (in the root window), where the layout functions have a window.
    void inFrame(std::function<void()> f) {
        inFrameCall = std::move(f);
        run("root = InFrame {}");
        frames(1);
        inFrameCall = {};
    }
    void pressEscape() {
        run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
        frames(3);
    }
    [[nodiscard]] const Spot& spot(std::string_view name) {
        const Spot* s = m_h.instance<Spot>(name);
        EXPECT_NE(s, nullptr) << name;
        return *s;
    }
    float across(std::string_view a, std::string_view b) { return spot(b).pos().x - spot(a).pos().x - 1.0f; }
    float down(std::string_view a, std::string_view b) { return spot(b).pos().y - spot(a).pos().y - 1.0f; }
    ImVec2 laidOut(std::string_view element) {
        run(std::format(R"(
            a, b, c, d = Spot {{}}, Spot {{}}, Spot {{}}, Spot {{}}
            root = Column {{ spacing = 0, Row {{ spacing = 0, a, {0}, b }}, c, {0}, d }})",
                        element));
        frames(2);
        return {across("a", "b"), down("c", "d")};
    }
    [[nodiscard]] static fluent::Look look() { return dynamic_cast<const FluentSkin&>(skin()).look(); }
    void expectNoErrors() { EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front(); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// Drawer item: controlHeight + 8dp; rail: + 24dp; bar: the NavigationBar's 80dp. The extras scale with the density.
TEST_F(FluentNavigationTest, DestinationSizesFollowTheTokens) {
    EXPECT_FLOAT_EQ(laidOut(R"(NavigationItem { icon = "home", "Home", width = 200 })").y, dp(40));
    EXPECT_FLOAT_EQ(laidOut(R"(NavigationItem { icon = "home", "Home", width = 200 })").x, 200.0f);
    inFrame([] { EXPECT_FLOAT_EQ(skin().navigationItemSize(0).x, dp(280)); });
    EXPECT_FLOAT_EQ(skin().navigationRailItemSize(80).y, dp(56));
    EXPECT_FLOAT_EQ(skin().navigationRailItemSize(80).x, 80.0f);
    EXPECT_FLOAT_EQ(skin().navigationBarItemSize(100).y, dp(80));
    EXPECT_FLOAT_EQ(skin().navigationBarItemSize(100).x, 100.0f);

    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(NavigationItem { icon = "home", "Home", width = 200 })").y, dp(48));
    EXPECT_FLOAT_EQ(skin().navigationRailItemSize(80).y, dp(64));
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 30, density = 2 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(NavigationItem { icon = "home", "Home", width = 200 })").y, dp(60 + 16));
    EXPECT_FLOAT_EQ(skin().navigationRailItemSize(80).y, dp(60 + 48));
    EXPECT_FLOAT_EQ(skin().navigationBarItemSize(100).y, dp(80));
    expectNoErrors();
}

TEST_F(FluentNavigationTest, DrawerItemsClickAndDisable) {
    run(R"(
        clicks = 0
        a, b = Spot {}, Spot {}
        root = Column { spacing = 0,
            a, NavigationItem { icon = "home", "Home", badge = "3", selected = true,
                                onClicked = function() clicks = clicks + 1 end },
            b, NavigationItem { icon = "home", "Off", badge = ".", enabled = false,
                                onClicked = function() clicks = clicks + 10 end } })");
    frames(2);
    click({600.0f, spot("a").pos().y + 1.0f + dp(20)}); // fills the width
    check("clicks == 1");
    click({100.0f, spot("b").pos().y + 1.0f + dp(20)});
    check("clicks == 1");
    expectNoErrors();
}

TEST_F(FluentNavigationTest, BarAndRailDestinationsSelect) {
    run(R"(
        s = state { bar = 1, rail = 1 }
        local function item(key, i, name)
            return NavigationItem { icon = "home", text = name, badge = i == 2 and "5" or "",
                selected = function() return s[key] == i end, onClicked = function() s[key] = i end } end
        a, r = Spot {}, Spot {}
        root = Column { spacing = 0,
            a, NavigationBar { width = 400, item("bar", 1, "One"), item("bar", 2, "Two"), item("bar", 3, "Three"),
                               item("bar", 4, "Four") },
            r, NavigationRail { height = 400, spacing = 0, item("rail", 1, "A"), item("rail", 2, "B") } })");
    frames(2);
    const ImVec2 bar = spot("a").pos();
    click({bar.x + 1.0f + 250.0f, bar.y + 1.0f + dp(40)}); // the third 100px cell
    check("s.bar == 3");
    // The rail's destinations start 12dp below its top; the second one is a rail item's height lower.
    const ImVec2 rail = spot("r").pos();
    click({rail.x + 1.0f + dp(40), rail.y + 1.0f + dp(12) + dp(56) + dp(28)});
    check("s.rail == 2");
    check("s.bar == 3");
    expectNoErrors();
}

// Both variants: controlHeight + 8dp, the tabs share the width; a click picks a tab two-way.
TEST_F(FluentNavigationTest, TabsPickTheirTabInBothVariants) {
    const std::vector<md::TabInfo> items{{"One", ""}, {"Second", "home"}};
    inFrame([&] {
        const fluent::Look l = look();
        EXPECT_FLOAT_EQ(fluent::tabsSize(l, items, false, 300).y, dp(40));
        EXPECT_FLOAT_EQ(fluent::tabsSize(l, items, true, 300).x, 300.0f);
        // Natural: each tab as wide as the widest content plus paddingX on both sides.
        const float widest = dp(16) + dp(8) + md::measure(fluent::bodyStyle, "Second").x;
        EXPECT_FLOAT_EQ(fluent::tabsSize(l, items, false, 0).x, 2.0f * (widest + 2.0f * dp(12)));
    });
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 20, density = 1.5 } })");
    inFrame([&] { EXPECT_FLOAT_EQ(fluent::tabsSize(look(), items, false, 300).y, dp(30 + 12)); });
    applyTheme(R"(theme { design = "fluent" })");

    for (const char* variant : {"primary", "secondary"}) {
        run(std::format(R"(
            s = state {{ tab = 1 }}
            changed = nil
            a = Spot {{}}
            root = Column {{ spacing = 0, a,
                TabBar {{ variant = "{}", width = 400, currentIndex = bind(s, "tab"),
                          onCurrentChanged = function(self, i) changed = i end,
                          Tab {{ title = "One" }}, Tab {{ title = "Two", icon = "home" }} }} }})",
                        variant));
        frames(2);
        const ImVec2 at = spot("a").pos();
        click({at.x + 1.0f + 300.0f, at.y + 1.0f + dp(20)});
        check("s.tab == 2");
        check("changed == 2");
        click({at.x + 1.0f + 300.0f, at.y + 1.0f + dp(20)}); // the current tab again: no change
        check("changed == 2");
        run("changed = nil");
        click({at.x + 1.0f + 100.0f, at.y + 1.0f + dp(20)});
        check("s.tab == 1");
        check("changed == 1");
    }
    expectNoErrors();
}

TEST_F(FluentNavigationTest, ModalDrawerOpensAndClosesOnEscapeAndTheSmoke) {
    run(R"(
        s = state { open = false }
        closed = 0
        content = Spot {}
        root = Column {
            ModalNavigationDrawer { id = "drawer", open = bind(s, "open"), width = 300,
                                    onClosed = function() closed = closed + 1 end,
                                    content, NavigationItem { icon = "home", "Home" } } })");
    frames(3);
    EXPECT_EQ(spot("content").renders(), 0);
    run("s.open = true");
    frames(30); // the pane slides in
    EXPECT_GT(spot("content").renders(), 0);
    // The content starts at the pane's padding.
    EXPECT_NEAR(spot("content").pos().x, dp(4), 1.0f);
    pressEscape();
    check("s.open == false");
    check("closed == 1");
    frames(30);
    const int renders = spot("content").renders();
    frames(3);
    EXPECT_EQ(spot("content").renders(), renders);

    run("s.open = true");
    frames(30);
    click({600.0f, 300.0f}); // the smoke right of the 300dp pane
    check("s.open == false");
    check("closed == 2");
    expectNoErrors();
}

TEST_F(FluentNavigationTest, ChromeGivesFluentContainerColors) {
    const fluent::Colors& c = look().colors;
    EXPECT_EQ(skin().navigationBarColor(), c.layer);
    EXPECT_EQ(skin().navigationRailColor(), c.background);
    run(R"(root = Column { NavigationBar { NavigationItem { icon = "home", "A" } },
                           NavigationRail { height = 200, NavigationItem { icon = "home", "B" } } })");
    frames(2);
    expectNoErrors();
}
