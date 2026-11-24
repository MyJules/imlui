// The layout built-ins as typed elements (Column/Item, Row, Grid, ScrollView/ListView, Card, TabBar/Tab,
// Section/Expander, TreeNode): defaults, props reaching the layout (measured with a probe element that records
// where it was placed), two-way write-back through bind, signals, and ScrollView keeping its scroll position
// when it moves to another place in the tree. Renders headless ImGui frames (tests/support/headless_frame.cppm).
#include <gtest/gtest.h>

import imgui;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.card;
import imlui.ui.elements.column;
import imlui.ui.elements.grid;
import imlui.ui.elements.row;
import imlui.ui.elements.scroll_view;
import imlui.ui.elements.section;
import imlui.ui.elements.tab;
import imlui.ui.elements.tab_bar;
import imlui.ui.elements.tree_node;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

/// A w x h box that records where it was placed, the item width it got and how often it rendered.
class Probe final : public Element {
public:
    static void declare(TypeBuilder<Probe>& type) {
        type.property("w", &Probe::w, &Probe::setW).property("h", &Probe::h, &Probe::setH);
    }
    void render(Frame&) override {
        m_pos = ImGui::GetCursorScreenPos();
        m_itemWidth = ImGui::CalcItemWidth();
        ++m_renders;
        ImGui::Dummy({m_w, m_h});
    }

    [[nodiscard]] float w() const { return m_w; }
    void setW(float w) { m_w = w; }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }
    [[nodiscard]] float itemWidth() const { return m_itemWidth; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    float m_w = 10.0f;
    float m_h = 10.0f;
    ImVec2 m_pos{-1.0f, -1.0f};
    float m_itemWidth = 0.0f;
    int m_renders = 0;
};

class LayoutTest : public ::testing::Test {
protected:
    void SetUp() override { m_h.registerType<Probe>("Probe"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void check(std::string_view lua) { EXPECT_EQ(m_h.run("assert(" + std::string{lua} + ")"), "") << lua; }
    Probe& probe(std::string_view name) {
        Probe* p = m_h.instance<Probe>(name);
        EXPECT_NE(p, nullptr) << name;
        static Probe none;
        return p ? *p : none;
    }
    /// A left click at (x, y): move, press, release, one frame each.
    void click(std::string_view root, float x, float y) {
        const std::string at = std::to_string(x) + ", " + std::to_string(y);
        run("native.injectMouse(" + at + ")");
        m_h.renderFrame(root);
        run("native.injectMouse(" + at + ", true)");
        m_h.renderFrame(root);
        run("native.injectMouse(" + at + ", false)");
        m_h.renderFrame(root);
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

// ------------------------------------------------------------ Column

TEST_F(LayoutTest, ColumnDefaultsAndAlias) {
    run("col = Column {} item = Item { padding = 4, spacing = 6, align = 'center' }");
    Column* col = m_h.instance<Column>("col");
    ASSERT_NE(col, nullptr);
    EXPECT_FLOAT_EQ(col->padding(), 0.0f);
    EXPECT_FALSE(col->spacing().has_value());
    EXPECT_EQ(col->align(), "start");
    Column* item = m_h.instance<Column>("item"); // Item is the same class
    ASSERT_NE(item, nullptr);
    EXPECT_FLOAT_EQ(item->padding(), 4.0f);
    EXPECT_EQ(item->spacing(), 6.0);
    EXPECT_EQ(item->align(), "center");
}

TEST_F(LayoutTest, ColumnPaddingSpacingWidthAndAlign) {
    run(R"(
        a = Probe {} b = Probe {} c = Probe {} d = Probe { w = 40 }
        root = Row { align = "top",
                     Column { a },
                     Column { padding = 10, spacing = 20, width = 200, b, c },
                     Column { width = 100, align = "end", d } })");
    m_h.renderFrames("root", 3);
    const ImVec2 a = probe("a").pos(), b = probe("b").pos(), c = probe("c").pos();
    EXPECT_FLOAT_EQ(b.y - a.y, 10.0f);         // the padding above
    EXPECT_FLOAT_EQ(c.y - b.y, 10.0f + 20.0f); // b's height, then the spacing
    EXPECT_FLOAT_EQ(probe("b").itemWidth(), 200.0f);
    EXPECT_FLOAT_EQ(b.x - a.x, probe("a").w() + ImGui::GetStyle().ItemSpacing.x + 10.0f); // and on the left
    EXPECT_FLOAT_EQ(probe("d").itemWidth(), 100.0f);
}

// ------------------------------------------------------------ Row

TEST_F(LayoutTest, RowSpacingAlignAndDefaults) {
    run(R"(
        a = Probe { w = 50, h = 20 } b = Probe { w = 30, h = 40 }
        root = Row { id = "row", spacing = 10, a, b })");
    Row* row = m_h.instance<Row>("root");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->align(), "center");
    EXPECT_EQ(row->justify(), "start");
    EXPECT_EQ(row->spacing(), 10.0);
    m_h.renderFrames("root", 3);
    EXPECT_FLOAT_EQ(probe("b").pos().x - probe("a").pos().x, 60.0f);
    EXPECT_FLOAT_EQ(probe("a").pos().y - probe("b").pos().y, 10.0f); // centered on the 40 high row
    run("root.align = 'bottom'");
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(probe("a").pos().y - probe("b").pos().y, 20.0f);
    run("root.align = 'top'");
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(probe("a").pos().y, probe("b").pos().y);
}

TEST_F(LayoutTest, RowJustifyAndFillSpacers) {
    run(R"(
        a = Probe { w = 50 } b = Probe { w = 30 } c = Probe { w = 50 } d = Probe { w = 30 }
        s = state { justify = "end" }
        justified = Row { width = 400, spacing = 0, justify = function() return s.justify end, a, b }
        filled = Row { width = 400, c, Spacer { fill = true }, d })");
    const float x0 = ImGui::GetStyle().WindowPadding.x; // where the root starts
    m_h.renderFrames("filled", 3);
    EXPECT_FLOAT_EQ(probe("c").pos().x, x0);
    EXPECT_FLOAT_EQ(probe("d").pos().x + 30.0f, x0 + 400.0f); // the fill takes what's left

    m_h.renderFrames("justified", 2);
    EXPECT_FLOAT_EQ(probe("b").pos().x + 30.0f, x0 + 400.0f);
    EXPECT_FLOAT_EQ(probe("a").pos().x, x0 + 320.0f);
    run("s.justify = 'center'");
    m_h.renderFrames("justified", 2);
    EXPECT_FLOAT_EQ(probe("a").pos().x, x0 + 160.0f);
    run("s.justify = 'spaceBetween'");
    m_h.renderFrames("justified", 2);
    EXPECT_FLOAT_EQ(probe("a").pos().x, x0);
    EXPECT_FLOAT_EQ(probe("b").pos().x + 30.0f, x0 + 400.0f);
}

TEST_F(LayoutTest, RowSkipsHiddenAndRendersNonVisualChildren) {
    run(R"(
        fired = 0
        a = Probe { w = 50 } b = Probe { w = 30, visible = false } c = Probe { w = 30 }
        root = Row { spacing = 0, a, b, Timer { interval = 0, onTriggered = function() fired = fired + 1 end }, c })");
    m_h.renderFrames("root", 3);
    EXPECT_EQ(probe("b").renders(), 0);
    EXPECT_FLOAT_EQ(probe("c").pos().x - probe("a").pos().x, 50.0f);
    check("fired > 0");
}

// ------------------------------------------------------------ Grid

TEST_F(LayoutTest, GridColumnsBindingSpacingAndBorder) {
    run(R"(
        s = state { cols = 2 }
        a = Probe {} b = Probe {} c = Probe {}
        root = Grid { columns = function() return s.cols end, a, b, c })");
    m_h.renderFrames("root", 2);
    Grid* grid = m_h.instance<Grid>("root");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->columns(), 2);
    EXPECT_FLOAT_EQ(grid->spacing(), 8.0f);
    EXPECT_FALSE(grid->border());
    EXPECT_FLOAT_EQ(probe("a").pos().y, probe("b").pos().y);
    EXPECT_GT(probe("c").pos().y, probe("a").pos().y);
    EXPECT_FLOAT_EQ(probe("c").pos().x, probe("a").pos().x);
    run("s.cols = 3; root.border = true; root.spacing = 0");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(grid->columns(), 3);
    EXPECT_TRUE(grid->border());
    EXPECT_FLOAT_EQ(probe("c").pos().y, probe("a").pos().y);
    EXPECT_GT(probe("c").pos().x, probe("b").pos().x);
    run("s.cols = 0"); // at least one column
    m_h.renderFrames("root", 2);
    EXPECT_GT(probe("b").pos().y, probe("a").pos().y);
}

TEST_F(LayoutTest, GridHiddenChildKeepsItsCell) {
    run(R"(
        a = Probe {} b = Probe { visible = false } c = Probe {}
        root = Grid { columns = 2, a, b, c })");
    m_h.renderFrames("root", 2);
    EXPECT_GT(probe("c").pos().y, probe("a").pos().y); // c is in the second row, as before the port
}

// ------------------------------------------------------------ ScrollView / ListView

TEST_F(LayoutTest, ScrollViewDefaultsPaddingAndListView) {
    run(R"(
        a = Probe {} b = Probe {}
        plain = ScrollView { height = 100, a }
        padded = ScrollView { height = 100, padding = 20, color = "primary", rounding = 4, border = true, b }
        list = ListView { height = 100, spacing = 3, model = { "x", "y" }, delegate = function() return Probe {} end })");
    m_h.renderFrames("plain", 2);
    ScrollView* plain = m_h.instance<ScrollView>("plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_FLOAT_EQ(plain->padding(), 0.0f);
    EXPECT_FALSE(plain->color().has_value());
    EXPECT_FALSE(plain->rounding().has_value());
    EXPECT_FALSE(plain->border());
    m_h.renderFrames("padded", 2);
    ScrollView* padded = m_h.instance<ScrollView>("padded");
    ASSERT_NE(padded, nullptr);
    EXPECT_TRUE(padded->color().has_value());
    EXPECT_EQ(padded->rounding(), 4.0f);
    EXPECT_TRUE(padded->border());
    EXPECT_FLOAT_EQ(probe("b").pos().x - probe("a").pos().x, 20.0f);
    EXPECT_FLOAT_EQ(probe("b").pos().y - probe("a").pos().y, 20.0f);
    ScrollView* list = m_h.instance<ScrollView>("list"); // ListView is the same class
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->spacing(), 3.0);
    m_h.renderFrames("list", 2);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LayoutTest, ListViewRendersItsDelegates) {
    run(R"(
        made = {}
        list = ListView { height = 100, model = { "x", "y", "z" },
                          delegate = function(item) local p = Probe { h = 5 }; made[item] = p; return p end })");
    m_h.renderFrames("list", 2);
    for (const char* item : {"x", "y", "z"}) {
        run(std::string{"p = made."} + item);
        EXPECT_GT(probe("p").renders(), 0) << item;
    }
    run("px, pz = made.x, made.z");
    EXPECT_LT(probe("px").pos().y, probe("pz").pos().y);
}

TEST_F(LayoutTest, ScrollViewKeepsScrollWhenItMovesToAnotherParent) {
    // The same ScrollView, first inside a Column, then inside a Row (like Responsive switching layouts): a
    // different ImGui ID path, so a different child window.
    run(R"(
        s = state { wide = false }
        local kids = { width = 300, height = 200 }
        for i = 1, 40 do kids[#kids + 1] = Probe { h = 30 } end
        sv = ScrollView(kids)
        narrow = Column { sv }
        wide = Row { Probe { w = 40 }, sv }
        root = Column { Loader { item = function() return s.wide and wide or narrow end } })");
    m_h.renderFrames("root", 3);
    ScrollView* sv = m_h.instance<ScrollView>("sv");
    ASSERT_NE(sv, nullptr);
    EXPECT_FLOAT_EQ(sv->scroll().y, 0.0f);
    run("native.injectMouse(100, 100, nil, -5)"); // the wheel over the view
    m_h.renderFrames("root", 3);
    const float scrolled = sv->scroll().y;
    ASSERT_GT(scrolled, 0.0f);

    run("s.wide = true");
    m_h.renderFrames("root", 6);
    EXPECT_FLOAT_EQ(sv->scroll().y, scrolled);

    // It doesn't fight the user afterwards: the wheel still scrolls, and it stays there.
    run("native.injectMouse(150, 100, nil, 2)");
    m_h.renderFrames("root", 3);
    const float back = sv->scroll().y;
    EXPECT_LT(back, scrolled);
    m_h.renderFrames("root", 6);
    EXPECT_FLOAT_EQ(sv->scroll().y, back);

    // And back to the first parent: the position comes along again (not the old window's).
    run("s.wide = false");
    m_h.renderFrames("root", 6);
    EXPECT_FLOAT_EQ(sv->scroll().y, back);
}

// ------------------------------------------------------------ Card

TEST_F(LayoutTest, CardDefaultsAndPadding) {
    run(R"(
        a = Probe {} b = Probe {}
        plain = Card { a }
        custom = Card { variant = "outlined", padding = 30, color = "primary", rounding = 2, b })");
    Card* plain = m_h.instance<Card>("plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->variant(), "elevated");
    EXPECT_FLOAT_EQ(plain->padding(), 16.0f);
    EXPECT_FALSE(plain->color().has_value());
    EXPECT_FALSE(plain->rounding().has_value());
    m_h.renderFrames("plain", 2);
    const ImVec2 a = probe("a").pos();
    m_h.renderFrames("custom", 2);
    Card* custom = m_h.instance<Card>("custom");
    ASSERT_NE(custom, nullptr);
    EXPECT_EQ(custom->variant(), "outlined");
    EXPECT_EQ(custom->rounding(), 2.0f);
    EXPECT_FLOAT_EQ(probe("b").pos().x - a.x, 14.0f);
    EXPECT_FLOAT_EQ(probe("b").pos().y - a.y, 14.0f);
}

// ------------------------------------------------------------ TabBar / Tab

TEST_F(LayoutTest, TabBarShowsTheCurrentTabAndWritesBackClicks) {
    run(R"(
        s = state { tab = 1 }
        changed = nil
        a = Probe {} b = Probe {}
        t1 = Tab { title = "One", a }
        t2 = Tab { title = "Two", icon = "home", b }
        root = TabBar { currentIndex = bind(s, "tab"), t1, t2,
                        onCurrentChanged = function(self, i) changed = i end })");
    TabBar* bar = m_h.instance<TabBar>("root");
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->variant(), "primary");
    Tab* t2 = m_h.instance<Tab>("t2");
    ASSERT_NE(t2, nullptr);
    EXPECT_EQ(t2->title(), "Two");
    EXPECT_EQ(t2->icon(), "home");
    run("t3 = Tab {}");
    EXPECT_EQ(m_h.instance<Tab>("t3")->title(), "Tab");

    m_h.renderFrames("root", 2);
    EXPECT_GT(probe("a").renders(), 0);
    EXPECT_EQ(probe("b").renders(), 0);

    // Two tabs across the 784 px wide content: click the second one (tabs with an icon are 64 high).
    const ImVec2 origin = ImGui::GetStyle().WindowPadding;
    click("root", origin.x + 784.0f * 0.75f, origin.y + 32.0f);
    m_h.renderFrame("root");
    EXPECT_EQ(bar->currentIndex(), 2);
    check("s.tab == 2");
    check("changed == 2");
    EXPECT_GT(probe("b").renders(), 0);

    // From Lua, and out of range clamps (without writing back).
    run("s.tab = 1");
    const int before = probe("a").renders();
    m_h.renderFrame("root");
    EXPECT_EQ(probe("a").renders(), before + 1);
    run("s.tab = 9");
    const int bBefore = probe("b").renders();
    m_h.renderFrame("root");
    EXPECT_EQ(probe("b").renders(), bBefore + 1);
    check("s.tab == 9");
}

TEST_F(LayoutTest, StrayTabRendersItsContent) {
    run("a = Probe {} root = Tab { title = 'x', a }");
    m_h.renderFrame("root");
    EXPECT_EQ(probe("a").renders(), 1);
}

// ------------------------------------------------------------ Section / Expander

TEST_F(LayoutTest, SectionTogglesTwoWay) {
    run(R"(
        s = state { open = true }
        toggled = nil
        a = Probe {}
        root = Section { title = "Advanced", supportingText = "More", expanded = bind(s, "open"),
                         onToggled = function(self, open) toggled = open end, a })");
    Section* sec = m_h.instance<Section>("root");
    ASSERT_NE(sec, nullptr);
    EXPECT_EQ(sec->title(), "Advanced");
    EXPECT_EQ(sec->supportingText(), "More");
    m_h.renderFrames("root", 2);
    EXPECT_GT(probe("a").renders(), 0);

    const ImVec2 origin = ImGui::GetStyle().WindowPadding;
    click("root", origin.x + 100.0f, origin.y + 20.0f);
    EXPECT_FALSE(sec->expanded());
    check("s.open == false");
    check("toggled == false");
    const int renders = probe("a").renders();
    m_h.renderFrames("root", 2);
    EXPECT_EQ(probe("a").renders(), renders);

    run("s.open = true");
    m_h.renderFrame("root");
    EXPECT_EQ(probe("a").renders(), renders + 1);
    // The indent: children 16 dp right of the header.
    EXPECT_FLOAT_EQ(probe("a").pos().x, origin.x + 16.0f);
}

TEST_F(LayoutTest, ExpanderDefaultsAndTextTitle) {
    run("a = Probe {} root = Expander { 'Details', a }");
    Section* sec = m_h.instance<Section>("root");
    ASSERT_NE(sec, nullptr);
    EXPECT_TRUE(sec->expanded());
    EXPECT_FALSE(sec->title().has_value());
    EXPECT_EQ(sec->text(), "Details");
    EXPECT_EQ(sec->supportingText(), "");
    m_h.renderFrame("root");
    EXPECT_EQ(probe("a").renders(), 1);
}

// ------------------------------------------------------------ TreeNode

TEST_F(LayoutTest, TreeNodeTextAndInitialExpansion) {
    run(R"(
        a = Probe {} b = Probe {}
        closed = TreeNode { "Files", a }
        open = TreeNode { text = "Docs", expanded = true, b })");
    TreeNode* closed = m_h.instance<TreeNode>("closed");
    ASSERT_NE(closed, nullptr);
    EXPECT_EQ(closed->text(), "Files");
    EXPECT_FALSE(closed->expanded());
    m_h.renderFrames("closed", 2);
    EXPECT_EQ(probe("a").renders(), 0);
    m_h.renderFrames("open", 2);
    EXPECT_EQ(probe("b").renders(), 2);
    EXPECT_TRUE(luaui::errors().empty());
}

} // namespace
