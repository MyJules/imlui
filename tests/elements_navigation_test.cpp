// The navigation and content built-ins as typed elements, through the headless frame harness: props reaching
// the instances and the widgets, defaults, positional text, aliases, two-way write-back, signals from injected
// mouse and key input, the NavigationScope styles, the scrolling NavigationRail and the three forms of
// ModalNavigationDrawer's `open` (plain value, bind(), function binding).
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.theme.typography;
import imlui.ui.elements.text;
import imlui.ui.elements.icon;
import imlui.ui.elements.link;
import imlui.ui.elements.divider;
import imlui.ui.elements.separator;
import imlui.ui.elements.spacer;
import imlui.ui.elements.top_app_bar;
import imlui.ui.elements.navigation_item;
import imlui.ui.elements.navigation_bar;
import imlui.ui.elements.navigation_rail;
import imlui.ui.elements.modal_navigation_drawer;
import imlui.ui.elements.menu_bar;
import imlui.ui.elements.menu;
import imlui.ui.elements.menu_item;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.test.headless_frame;
import imlui.widgets.top_app_bar;

using namespace imlui;

namespace {

struct Rect {
    ImVec2 min, max;
    [[nodiscard]] ImVec2 center() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
    [[nodiscard]] float width() const { return max.x - min.x; }
    [[nodiscard]] float height() const { return max.y - min.y; }
};

/// Screen rects of the last frame, by element id (Probe children and Boxes).
std::map<std::string, Rect> rects;

/// A container that renders its children in a column and records each child's rect under its `id`.
class Probe final : public Element {
public:
    static void declare(TypeBuilder<Probe>&) {}
    void render(Frame& frame) override {
        frame.forEachChild([](Frame::Child& child) {
            child.render();
            if (child.isVisual()) rects[child.node().string("id")] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        });
    }
};

/// A fixed-size visual element that records its own rect under its `id` and counts its renders.
class Box final : public Element {
public:
    static void declare(TypeBuilder<Box>& type) {
        type.property("w", &Box::w, &Box::setW).property("h", &Box::h, &Box::setH);
    }
    void render(Frame& frame) override {
        ++m_renders;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy({m_w, m_h});
        rects[frame.node().string("id")] = {p, {p.x + m_w, p.y + m_h}};
    }
    [[nodiscard]] float w() const { return m_w; }
    void setW(float w) { m_w = w; }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    float m_w = 40.0f;
    float m_h = 40.0f;
    int m_renders = 0;
};

/// Its constructor throws: the element has no instance, so it counts as visual but renders nothing.
class Broken final : public Element {
public:
    static void declare(TypeBuilder<Broken>&) {}
    Broken() { throw std::runtime_error("broken on purpose"); }
    void render(Frame&) override {}
};

/// Whether a script error contains `part`.
bool hasError(std::string_view part) {
    return std::ranges::any_of(luaui::errors(),
                               [&](const std::string& e) { return e.find(part) != std::string::npos; });
}

class NavigationElementsTest : public ::testing::Test {
protected:
    void SetUp() override {
        rects.clear();
        m_h.registerType<Probe>("Probe");
        m_h.registerType<Box>("Box");
        m_h.registerType<Broken>("Broken");
    }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    /// Asserts a Lua expression (in the script environment) is true.
    void check(std::string_view expr) { EXPECT_EQ(m_h.run("assert(" + std::string{expr} + ")"), "") << expr; }
    void frames(int n = 1) { m_h.renderFrames("root", n); }
    /// A left click at (x, y): move, press, release, one frame each.
    void click(ImVec2 at) { m_h.click("root", at.x, at.y); }
    void pressEscape() {
        run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
        frames(3);
    }
    void expectNoErrors() { EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front(); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// ------------------------------------------------------------------ content

TEST_F(NavigationElementsTest, TextTakesItsPropsAndPositionalTextAndLabelIsAnAlias) {
    run(R"(
        s = state { t = "bound" }
        root = Probe {
            Text { id = "plain" },
            Text { id = "positional", "hello" },
            Text { id = "styled", text = function() return s.t end, style = "titleMedium", color = "#FF0000",
                   wrap = true, align = "center" },
            Text { id = "unknownStyle", text = "x", style = "nope" },
            Label { id = "label", "a label" },
        })");
    frames();
    const Text* plain = m_h.instance<Text>("plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->text(), "");
    EXPECT_EQ(plain->style(), "bodyLarge");
    EXPECT_EQ(plain->textStyle(), TextStyle::BodyLarge);
    EXPECT_FALSE(plain->color().has_value());
    EXPECT_FALSE(plain->wrap());
    EXPECT_EQ(plain->align(), "start");

    EXPECT_EQ(m_h.instance<Text>("positional")->text(), "hello");
    const Text* styled = m_h.instance<Text>("styled");
    EXPECT_EQ(styled->text(), "bound");
    EXPECT_EQ(styled->textStyle(), TextStyle::TitleMedium);
    ASSERT_TRUE(styled->color().has_value());
    EXPECT_EQ(*styled->color(), (Color{1.0f, 0.0f, 0.0f, 1.0f}));
    EXPECT_TRUE(styled->wrap());
    EXPECT_EQ(styled->align(), "center");
    EXPECT_EQ(m_h.instance<Text>("unknownStyle")->textStyle(), TextStyle::BodyLarge);
    ASSERT_NE(m_h.instance<Text>("label"), nullptr);
    EXPECT_EQ(m_h.instance<Text>("label")->text(), "a label");

    run(R"(s.t = "changed")");
    frames();
    EXPECT_EQ(m_h.instance<Text>("styled")->text(), "changed");
    // A non-empty text takes space; an empty one is a zero-width line.
    EXPECT_GT(rects["positional"].width(), 0.0f);
    expectNoErrors();
}

TEST_F(NavigationElementsTest, IconPicksNameThenIconThenText) {
    run(R"(
        root = Probe {
            Icon { id = "byName", name = "home", icon = "star", "search" },
            Icon { id = "byIcon", icon = "star", "search" },
            Icon { id = "byText", "search", size = 48, color = "#00FF00" },
            Icon { id = "empty" },
        })");
    frames();
    EXPECT_EQ(m_h.instance<Icon>("byName")->glyph(), "home");
    EXPECT_EQ(m_h.instance<Icon>("byIcon")->glyph(), "star");
    const Icon* byText = m_h.instance<Icon>("byText");
    EXPECT_EQ(byText->glyph(), "search");
    EXPECT_FLOAT_EQ(byText->size(), 48.0f);
    EXPECT_EQ(byText->color(), (Color{0.0f, 1.0f, 0.0f, 1.0f}));
    const Icon* empty = m_h.instance<Icon>("empty");
    EXPECT_EQ(empty->glyph(), "");
    EXPECT_FLOAT_EQ(empty->size(), 24.0f);
    EXPECT_FALSE(empty->color().has_value());
    expectNoErrors();
}

// md::drawIcon skips a size below a pixel (ImGui asserts on it) and caps a huge one.
TEST_F(NavigationElementsTest, IconOfAnyNumericSizeRendersWithoutAborting) {
    run(R"(
        root = Probe {
            Icon { "star", size = 0 }, Icon { "star", size = -8 }, Icon { "star", size = 0/0 },
            Icon { "star", size = 0.01 }, Icon { "star", size = 1e9 }, Icon { "star", size = 1/0 },
        })");
    frames(2);
    expectNoErrors();
}

TEST_F(NavigationElementsTest, LinkDefaultsItsUrlToTheText) {
    run(R"(root = Probe { Link { id = "a", "https://example.com" }, Link { id = "b", text = "Docs", url = "https://x.y" } })");
    frames();
    EXPECT_EQ(m_h.instance<Link>("a")->text(), "https://example.com");
    EXPECT_FALSE(m_h.instance<Link>("a")->url().has_value());
    EXPECT_EQ(m_h.instance<Link>("b")->url(), "https://x.y");
    EXPECT_GT(rects["a"].width(), 0.0f);
    expectNoErrors();
}

TEST_F(NavigationElementsTest, DividerAndSeparatorTakeInsetsAndSeparatorText) {
    run(R"(
        root = Probe {
            Divider { id = "d" },
            Divider { id = "inset", inset = 16, insetEnd = 8 },
            Separator { id = "plain", inset = 4 },
            Separator { id = "titled", "Section" },
        })");
    frames();
    EXPECT_FLOAT_EQ(m_h.instance<Divider>("d")->inset(), 0.0f);
    EXPECT_FLOAT_EQ(m_h.instance<Divider>("d")->insetEnd(), 0.0f);
    EXPECT_FLOAT_EQ(m_h.instance<Divider>("inset")->inset(), 16.0f);
    EXPECT_FLOAT_EQ(m_h.instance<Divider>("inset")->insetEnd(), 8.0f);
    EXPECT_FLOAT_EQ(m_h.instance<Separator>("plain")->inset(), 4.0f); // inherited from Divider
    EXPECT_EQ(m_h.instance<Separator>("plain")->text(), "");
    EXPECT_EQ(m_h.instance<Separator>("titled")->text(), "Section");
    EXPECT_GT(rects["titled"].height(), rects["plain"].height()); // the heading line is taller than a rule
    expectNoErrors();
}

TEST_F(NavigationElementsTest, SpacerTakesItsSizeAndFill) {
    run(R"(root = Probe { Spacer { id = "a", width = 30, height = 12 }, Spacer { id = "b", fill = true } })");
    frames();
    EXPECT_FLOAT_EQ(rects["a"].width(), 30.0f);
    EXPECT_FLOAT_EQ(rects["a"].height(), 12.0f);
    // `fill` is the spacer's width policy: the available width here (the window's), a share in a Row.
    EXPECT_FLOAT_EQ(rects["b"].width(), 800.0f - 2.0f * ImGui::GetStyle().WindowPadding.x);
    EXPECT_FALSE(m_h.instance<Spacer>("a")->fill());
    EXPECT_TRUE(m_h.instance<Spacer>("b")->fill());
    expectNoErrors();
}

// ------------------------------------------------------------------ app bar, bar, rail

TEST_F(NavigationElementsTest, TopAppBarRightAlignsActionsAndEmitsOnNavigation) {
    run(R"(
        navigations = 0
        root = Probe {
            TopAppBar { id = "bar", title = "Inbox", navigationIcon = "menu", width = 500,
                        onNavigation = function(self) navigations = navigations + 1; assert(self == bar) end,
                        Box { id = "a1", w = 48, h = 48 }, Box { id = "a2", w = 40, h = 30 } },
            TopAppBar { id = "large", variant = "large", title = "Large" },
        })");
    frames(2); // actions are placed by their size on the last frame
    const TopAppBar* bar = m_h.instance<TopAppBar>("bar");
    EXPECT_EQ(bar->title(), "Inbox");
    EXPECT_EQ(bar->navigationIcon(), "menu");
    EXPECT_EQ(bar->variant(), md::TopAppBarVariant::Small);
    EXPECT_FALSE(bar->color().has_value());

    const Rect b = rects["bar"];
    EXPECT_FLOAT_EQ(b.width(), 500.0f);
    EXPECT_FLOAT_EQ(b.height(), 64.0f);
    EXPECT_FLOAT_EQ(rects["large"].height(), 152.0f);
    EXPECT_FLOAT_EQ(rects["a2"].max.x, b.max.x - 4.0f); // right-aligned, 4dp from the edge
    EXPECT_FLOAT_EQ(rects["a1"].max.x, rects["a2"].min.x);
    EXPECT_FLOAT_EQ(rects["a1"].center().y, b.min.y + 32.0f); // centered in the 64dp row
    EXPECT_FLOAT_EQ(rects["a2"].center().y, b.min.y + 32.0f);

    click({b.min.x + 4.0f + 24.0f, b.min.y + 32.0f}); // the 48dp navigation icon button
    check("navigations == 1");
    expectNoErrors();
}

// A variant is one of the TopAppBarVariant names: a typo is reported with the names, and the bar keeps its variant.
TEST_F(NavigationElementsTest, TopAppBarReportsAnUnknownVariant) {
    run(R"(root = TopAppBar { title = "T", variant = "large" })");
    frames();
    EXPECT_EQ(m_h.instance<TopAppBar>("root")->variant(), md::TopAppBarVariant::Large);
    run(R"(root.variant = "huge")");
    frames();
    EXPECT_EQ(m_h.instance<TopAppBar>("root")->variant(), md::TopAppBarVariant::Large);
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_TRUE(hasError(
        R"(TopAppBar.variant: TopAppBarVariant ('small'|'center'|'medium'|'large') expected, got string "huge")"))
        << luaui::errors()[0];
    run("root.variant = nil");
    frames();
    EXPECT_EQ(m_h.instance<TopAppBar>("root")->variant(), md::TopAppBarVariant::Small);
}

// An action that doesn't render (its constructor threw) submits no item: the bar read the previous action's rect as
// its width and moved the ones after it past the bar's edge. It takes no width now.
TEST_F(NavigationElementsTest, TopAppBarActionThatFailsToRenderTakesNoWidth) {
    run(R"(
        root = Probe {
            TopAppBar { id = "bar", title = "Inbox", width = 500,
                        Box { id = "a1", w = 48, h = 48 }, Broken {}, Box { id = "a2", w = 40, h = 30 } },
        })");
    frames(2);
    EXPECT_TRUE(hasError("broken on purpose"));
    EXPECT_FLOAT_EQ(rects["a2"].max.x, rects["bar"].max.x - 4.0f);
    EXPECT_FLOAT_EQ(rects["a1"].max.x, rects["a2"].min.x);
}

TEST_F(NavigationElementsTest, NavigationBarSharesItsWidthAndItemsEmitOnClicked) {
    run(R"(
        s = state { selected = 1 }
        local function item(i, name) return NavigationItem { id = name, icon = "home", text = name,
            selected = function() return s.selected == i end, onClicked = function() s.selected = i end } end
        root = Probe {
            NavigationBar { id = "bar", width = 400, item(1, "One"), item(2, "Two"), Box { visible = false },
                            item(3, "Three"), item(4, "Four") },
        })");
    frames();
    const Rect b = rects["bar"];
    EXPECT_FLOAT_EQ(b.width(), 400.0f);
    EXPECT_FLOAT_EQ(b.height(), 80.0f);
    EXPECT_TRUE(m_h.instance<NavigationItem>("One")->selected());
    EXPECT_EQ(m_h.instance<NavigationItem>("Two")->text(), "Two");
    EXPECT_FALSE(m_h.instance<NavigationBar>("bar")->color().has_value());

    click({b.min.x + 250.0f, b.min.y + 40.0f}); // the third 100px cell (the hidden Box takes none)
    check("s.selected == 3");
    EXPECT_TRUE(m_h.instance<NavigationItem>("Three")->selected());
    EXPECT_FALSE(m_h.instance<NavigationItem>("One")->selected());
    expectNoErrors();
}

TEST_F(NavigationElementsTest, NavigationItemDrawsAsADrawerItemOutsideNavigationContainers) {
    run(R"(
        clicks = 0
        root = Probe {
            NavigationItem { id = "wide", icon = "home", "Home", badge = "3", onClicked = function() clicks = clicks + 1 end },
            NavigationItem { id = "narrow", icon = "home", "Home", width = 200 },
        })");
    frames();
    EXPECT_EQ(m_h.instance<NavigationItem>("wide")->badge(), "3");
    EXPECT_FLOAT_EQ(rects["wide"].height(), 56.0f);
    EXPECT_GT(rects["wide"].width(), 600.0f); // fills the 800px window by default
    EXPECT_FLOAT_EQ(rects["narrow"].width(), 200.0f);
    click(rects["wide"].center());
    check("clicks == 1");
    expectNoErrors();
}

TEST_F(NavigationElementsTest, NavigationRailStacksDestinationsBelowItsHeader) {
    run(R"(
        s = state { selected = 1 }
        local function item(i) return NavigationItem { icon = "home", text = "D" .. i,
            selected = function() return s.selected == i end, onClicked = function() s.selected = i end } end
        root = Probe {
            NavigationRail { id = "rail", height = 500, header = Box { id = "header", w = 56, h = 56 },
                             item(1), item(2), item(3) },
        })");
    frames(2);
    const NavigationRail* rail = m_h.instance<NavigationRail>("rail");
    EXPECT_FLOAT_EQ(rail->spacing(), 12.0f);
    EXPECT_EQ(rail->align(), "top");
    const Rect r = rects["rail"];
    EXPECT_FLOAT_EQ(r.width(), 80.0f);
    EXPECT_FLOAT_EQ(r.height(), 500.0f);
    EXPECT_FLOAT_EQ(rects["header"].min.y, r.min.y + 12.0f);
    EXPECT_FLOAT_EQ(rects["header"].center().x, r.min.x + 40.0f); // centered

    // Destinations start 28dp below the header: 56dp each, 12dp apart.
    const float firstTop = r.min.y + 12.0f + 56.0f + 28.0f;
    click({r.min.x + 40.0f, firstTop + 56.0f + 12.0f + 28.0f}); // the second one
    check("s.selected == 2");
    expectNoErrors();
}

TEST_F(NavigationElementsTest, NavigationRailAlignsDestinations) {
    run(R"(
        root = Probe {
            NavigationRail { id = "rail", height = 400, align = "bottom", Box { id = "d1", w = 56, h = 50 },
                             Box { id = "d2", w = 56, h = 50 } },
        })");
    frames(2); // aligned by the total height of the last frame
    const Rect r = rects["rail"];
    EXPECT_FLOAT_EQ(rects["d2"].max.y, r.max.y - 12.0f);
    EXPECT_FLOAT_EQ(rects["d1"].max.y + 12.0f, rects["d2"].min.y);
    expectNoErrors();
}

// A destination that doesn't render (its constructor threw) took the previous one's height: the ones after it moved
// down by that. It takes its gap and no height now.
TEST_F(NavigationElementsTest, NavigationRailDestinationThatFailsToRenderTakesNoHeight) {
    run(R"(
        root = Probe {
            NavigationRail { id = "rail", height = 400, spacing = 10, Box { id = "d1", w = 56, h = 50 }, Broken {},
                             Box { id = "d2", w = 56, h = 50 } },
        })");
    frames(2);
    EXPECT_TRUE(hasError("broken on purpose"));
    EXPECT_FLOAT_EQ(rects["d2"].min.y - rects["d1"].max.y, 10.0f + 10.0f);
}

TEST_F(NavigationElementsTest, NavigationRailScrollsDestinationsThatDoNotFitAndKeepsTheHeaderFixed) {
    run(R"(
        root = Probe {
            NavigationRail { id = "rail", height = 300, spacing = 10, header = Box { id = "header", w = 56, h = 40 },
                             Box { id = "d1", w = 56, h = 56 }, Box { id = "d2", w = 56, h = 56 },
                             Box { id = "d3", w = 56, h = 56 }, Box { id = "d4", w = 56, h = 56 },
                             Box { id = "d5", w = 56, h = 56 } },
        })");
    frames(2);
    const Rect r = rects["rail"];
    EXPECT_FLOAT_EQ(r.height(), 300.0f); // the rail keeps its size
    const float headerTop = rects["header"].min.y;
    const float d1Top = rects["d1"].min.y;
    EXPECT_FLOAT_EQ(d1Top, r.min.y + 12.0f + 40.0f + 28.0f);
    EXPECT_FLOAT_EQ(rects["d1"].center().x, r.min.x + 40.0f); // centered, the scrollbar doesn't move them

    // Wheel down over the destinations: they scroll, the header stays.
    run("ui.native.injectMouse(" + std::to_string(r.min.x + 40.0f) + ", " + std::to_string(d1Top + 20.0f) +
        ", false, -5)");
    frames(3);
    EXPECT_FLOAT_EQ(rects["header"].min.y, headerTop);
    EXPECT_LT(rects["d1"].min.y, d1Top);
    // Scrolled to the end, the last destination ends 12dp above the rail's bottom edge.
    EXPECT_FLOAT_EQ(rects["d5"].max.y, r.max.y - 12.0f);
    expectNoErrors();
}

TEST_F(NavigationElementsTest, NavigationRailThatFitsDoesNotScroll) {
    run(R"(
        root = Probe {
            NavigationRail { id = "rail", height = 400, Box { id = "d1", w = 56, h = 56 },
                             Box { id = "d2", w = 56, h = 56 } },
        })");
    frames(2);
    const float d1Top = rects["d1"].min.y;
    const Rect r = rects["rail"];
    run("ui.native.injectMouse(" + std::to_string(r.min.x + 40.0f) + ", " + std::to_string(d1Top + 20.0f) +
        ", false, -5)");
    frames(3);
    EXPECT_FLOAT_EQ(rects["d1"].min.y, d1Top);
    expectNoErrors();
}

// ------------------------------------------------------------------ modal navigation drawer

TEST_F(NavigationElementsTest, DrawerWithAPlainOpenValueWritesFalseBackWhenClosed) {
    run(R"(
        closed = 0
        root = Column {
            ModalNavigationDrawer { id = "drawer", open = false, onClosed = function() closed = closed + 1 end,
                                    Box { id = "content" } },
        })");
    frames(3);
    const ModalNavigationDrawer* drawer = m_h.instance<ModalNavigationDrawer>("drawer");
    EXPECT_FALSE(drawer->open());
    EXPECT_FLOAT_EQ(drawer->width(), 360.0f);
    EXPECT_EQ(m_h.instance<Box>("content")->renders(), 0);

    for (int round = 1; round <= 2; ++round) { // it opens again after closing itself
        run("drawer.open = true");
        frames(30); // the sheet slides in
        EXPECT_TRUE(m_h.instance<ModalNavigationDrawer>("drawer")->shown());
        EXPECT_GT(m_h.instance<Box>("content")->renders(), 0);
        pressEscape();
        check("drawer.open == false"); // written back to the element's own field
        check("closed == " + std::to_string(round));
        EXPECT_FALSE(m_h.instance<ModalNavigationDrawer>("drawer")->open());
    }
    frames(30); // slid out: the contents are no longer rendered
    const int renders = m_h.instance<Box>("content")->renders();
    frames(3);
    EXPECT_EQ(m_h.instance<Box>("content")->renders(), renders);
    expectNoErrors();
}

TEST_F(NavigationElementsTest, DrawerWithBindWritesTheStateAndClosesOnAScrimClick) {
    run(R"(
        s = state { open = true }
        closed = 0
        root = Column {
            ModalNavigationDrawer { id = "drawer", open = bind(s, "open"), width = 300,
                                    onClosed = function() closed = closed + 1 end,
                                    NavigationItem { id = "item", icon = "home", "Home" } },
        })");
    frames(30);
    EXPECT_FLOAT_EQ(m_h.instance<ModalNavigationDrawer>("drawer")->width(), 300.0f);
    check("s.open == true and closed == 0");
    click({600.0f, 300.0f}); // the scrim right of the 300dp sheet
    check("s.open == false");
    check("closed == 1");
    check("rawget(drawer, 'open') ~= false"); // the bind() itself stays
    run("s.open = true");
    frames(30);
    EXPECT_TRUE(m_h.instance<ModalNavigationDrawer>("drawer")->shown());
    expectNoErrors();
}

TEST_F(NavigationElementsTest, DrawerWithAFunctionBindingHidesUntilTheBindingTurnsFalse) {
    run(R"(
        s = state { open = true }
        closed = 0
        root = Column {
            ModalNavigationDrawer { id = "drawer", open = function() return s.open end,
                                    onClosed = function() closed = closed + 1 end, Box { id = "content" } },
        })");
    frames(30);
    EXPECT_TRUE(m_h.instance<ModalNavigationDrawer>("drawer")->shown());
    pressEscape();
    check("closed == 1");
    check("s.open == true"); // the drawer can't change what the function returns
    check("type(rawget(drawer, 'open')) == 'function'");
    EXPECT_TRUE(m_h.instance<ModalNavigationDrawer>("drawer")->open());
    EXPECT_FALSE(m_h.instance<ModalNavigationDrawer>("drawer")->shown()); // but it closes visibly
    frames(30);
    const int renders = m_h.instance<Box>("content")->renders();
    frames(3);
    EXPECT_EQ(m_h.instance<Box>("content")->renders(), renders); // slid out, stays hidden

    // Once the function returns false, true opens it again.
    run("s.open = false");
    frames();
    run("s.open = true");
    frames(30);
    EXPECT_TRUE(m_h.instance<ModalNavigationDrawer>("drawer")->shown());
    EXPECT_GT(m_h.instance<Box>("content")->renders(), renders);
    expectNoErrors();
}

TEST_F(NavigationElementsTest, DrawerWithAFunctionBindingWhoseHandlerClosesTheStateReopens) {
    run(R"(
        s = state { open = true }
        root = Column {
            ModalNavigationDrawer { id = "drawer", open = function() return s.open end,
                                    onClosed = function() s.open = false end, Box { id = "content" } },
        })");
    frames(30);
    pressEscape();
    check("s.open == false");
    EXPECT_FALSE(m_h.instance<ModalNavigationDrawer>("drawer")->shown());
    run("s.open = true");
    frames(30);
    EXPECT_TRUE(m_h.instance<ModalNavigationDrawer>("drawer")->shown());
    expectNoErrors();
}

TEST_F(NavigationElementsTest, DrawerItemsUseTheDrawerStyleInsideARail) {
    // The drawer opens a default (drawer) NavigationScope, even when declared inside a rail.
    run(R"(
        clicked = false
        root = Probe {
            NavigationRail { id = "rail", height = 300,
                ModalNavigationDrawer { open = true,
                    NavigationItem { icon = "home", "Home", onClicked = function() clicked = true end } } },
        })");
    frames(30);
    click({200.0f, 12.0f + 28.0f}); // a drawer pill spans the sheet (12dp padding); a rail item would not
    check("clicked");
    expectNoErrors();
}

// ------------------------------------------------------------------ menus

namespace {

/// Renders its children inside an always-open popup at (100, 100), recording their rects like Probe.
class PopupProbe final : public Element {
public:
    static void declare(TypeBuilder<PopupProbe>&) {}
    void render(Frame& frame) override {
        if (!ImGui::IsPopupOpen("##probe")) ImGui::OpenPopup("##probe");
        ImGui::SetNextWindowPos({100.0f, 100.0f});
        if (ImGui::BeginPopup("##probe")) {
            frame.forEachChild([](Frame::Child& child) {
                child.render();
                if (child.isVisual())
                    rects[child.node().string("id")] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
            });
            ImGui::EndPopup();
        }
    }
};

} // namespace

TEST_F(NavigationElementsTest, MenuItemTriggersAndTogglesCheckedTwoWay) {
    m_h.registerType<PopupProbe>("PopupProbe");
    run(R"(
        s = state { checked = false }
        triggered = 0
        root = PopupProbe {
            MenuItem { id = "check", text = "Wrap lines", checked = bind(s, "checked"),
                       onTriggered = function() triggered = triggered + 1 end },
            MenuItem { id = "plain", "Plain", shortcut = "Ctrl+P", onTriggered = function() triggered = triggered + 10 end },
        })");
    frames(2);
    const MenuItem* plain = m_h.instance<MenuItem>("plain");
    EXPECT_EQ(plain->text(), "Plain");
    EXPECT_EQ(plain->shortcut(), "Ctrl+P");
    EXPECT_EQ(plain->icon(), "");
    EXPECT_FALSE(plain->checked());

    click(rects["check"].center());
    check("s.checked == true");
    check("triggered == 1");
    EXPECT_TRUE(m_h.instance<MenuItem>("check")->checked());
    click(rects["check"].center()); // a checkable item keeps the menu open
    check("s.checked == false");
    check("triggered == 2");

    click(rects["plain"].center());
    check("triggered == 12");
    expectNoErrors();
}

TEST_F(NavigationElementsTest, MenuBarAndMenuTakeTheirTitle) {
    run(R"(root = Probe { MenuBar { Menu { id = "file", title = "File", MenuItem { "Open" } } } })");
    frames(2);
    EXPECT_EQ(m_h.instance<Menu>("file")->title(), "File");
    expectNoErrors();
}
