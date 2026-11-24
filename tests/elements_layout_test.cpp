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
import imlui.test.last_item_probe;
import imlui.ui.frame_request;
import imlui.ui.item_size;
import imlui.ui.layout_cache;

using namespace imlui;

namespace {

/// A w x h box that records where it was placed, the item width it got and how often it rendered. With
/// `intrinsic = true` it reports its size before rendering (Element::intrinsicSize).
class Probe final : public Element {
public:
    static void declare(TypeBuilder<Probe>& type) {
        type.property("w", &Probe::w, &Probe::setW)
            .property("h", &Probe::h, &Probe::setH)
            .property("intrinsic", &Probe::intrinsic, &Probe::setIntrinsic);
    }
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override {
        if (!m_intrinsic) return std::nullopt;
        return ImVec2{m_w, m_h};
    }
    void render(Frame&) override {
        m_pos = ImGui::GetCursorScreenPos();
        m_itemWidth = ImGui::CalcItemWidth();
        const ImDrawList* dl = ImGui::GetWindowDrawList();
        m_seen = dl->GetClipRectMax().x > dl->GetClipRectMin().x && dl->GetClipRectMax().y > dl->GetClipRectMin().y;
        ++m_renders;
        ImGui::Dummy({m_w, m_h});
    }

    [[nodiscard]] float w() const { return m_w; }
    void setW(float w) { m_w = w; }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }
    [[nodiscard]] bool intrinsic() const { return m_intrinsic; }
    void setIntrinsic(bool intrinsic) { m_intrinsic = intrinsic; }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }
    [[nodiscard]] float itemWidth() const { return m_itemWidth; }
    [[nodiscard]] int renders() const { return m_renders; }
    /// Whether it could be seen when it last rendered (not clipped away).
    [[nodiscard]] bool seen() const { return m_seen; }

private:
    float m_w = 10.0f;
    float m_h = 10.0f;
    ImVec2 m_pos{-1.0f, -1.0f};
    float m_itemWidth = 0.0f;
    int m_renders = 0;
    bool m_seen = false;
    bool m_intrinsic = false;
};

/// Renders its children top to bottom and records, for each, the intrinsic size it reported before rendering and
/// the size it took.
class Measure final : public Element {
public:
    struct Result {
        std::string type;
        std::optional<ImVec2> intrinsic;
        ImVec2 rendered;
    };
    static void declare(TypeBuilder<Measure>&) {}
    void render(Frame& frame) override {
        results.clear();
        frame.forEachChild([&](Frame::Child& c) {
            if (!c.isVisual()) return;
            const std::optional<ImVec2> intrinsic = c.intrinsicSize(frame.available().x);
            c.render();
            results.push_back({c.type(), intrinsic, ImGui::GetItemRectSize()});
        });
    }
    std::vector<Result> results;
};

/// Raw ImGui inside an element: wrapped text and the item width ImGui gives, like a custom element would draw.
class RawImGui final : public Element {
public:
    static void declare(TypeBuilder<RawImGui>&) {}
    void render(Frame&) override {
        m_itemWidth = ImGui::CalcItemWidth();
        ImGui::TextWrapped("%s", "The quick brown fox jumps over the lazy dog, again and again and again.");
        m_text = ImGui::GetItemRectSize();
    }
    [[nodiscard]] float itemWidth() const { return m_itemWidth; }
    [[nodiscard]] ImVec2 text() const { return m_text; }

private:
    float m_itemWidth = 0.0f;
    ImVec2 m_text{};
};

/// A box that grows by 10 px every frame and never asks for frames: a layout that can never settle.
class Grower final : public Element {
public:
    static void declare(TypeBuilder<Grower>&) {}
    void render(Frame&) override {
        m_w += 10.0f;
        ImGui::Dummy({m_w, 10.0f});
    }

private:
    float m_w = 10.0f;
};

class LayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_h.registerType<Probe>("Probe");
        m_h.registerType<test::LastItemProbe>("Last");
        m_h.registerType<RawImGui>("Raw");
        m_h.registerType<Grower>("Grower");
        m_h.registerType<Measure>("Measure");
    }
    [[nodiscard]] ImVec2 lastSize(std::string_view name) {
        test::LastItemProbe* p = m_h.instance<test::LastItemProbe>(name);
        EXPECT_NE(p, nullptr) << name;
        return p ? p->size() : ImVec2{};
    }
    [[nodiscard]] ImVec2 lastMin(std::string_view name) {
        test::LastItemProbe* p = m_h.instance<test::LastItemProbe>(name);
        EXPECT_NE(p, nullptr) << name;
        return p ? p->min() : ImVec2{};
    }
    /// The headless window's content width (800 px wide, 8 px padding).
    [[nodiscard]] static float contentWidth() { return 800.0f - 2.0f * ImGui::GetStyle().WindowPadding.x; }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void check(std::string_view lua) { EXPECT_EQ(m_h.run("assert(" + std::string{lua} + ")"), "") << lua; }
    Probe& probe(std::string_view name) {
        Probe* p = m_h.instance<Probe>(name);
        EXPECT_NE(p, nullptr) << name;
        static Probe none;
        return p ? *p : none;
    }
    /// On-demand rendering, like the Engine: renders frames while one is due now (requested), at most `limit`.
    /// Returns how many it rendered.
    int renderWhileRequested(std::string_view root, int limit = 50) {
        int n = 0;
        for (; n < limit && secondsUntilFrame() == 0.0; ++n)
            m_h.renderFrame(root);
        return n;
    }
    /// A left click at (x, y): move, press, release, one frame each.
    void click(std::string_view root, float x, float y) { m_h.click(root, x, y); }

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

// A fixed width is the children's available width: `align` and fills measure the column, not the window.
TEST_F(LayoutTest, AFixedWidthColumnConstrainsAlignAndFills) {
    m_h.registerType<test::LastItemProbe>("Last");
    run(R"(
        start = Probe { w = 0, h = 0 } d = Probe { w = 30 } e = Probe { w = 30 }
        root = Row { spacing = 0, align = "top", start,
                     Column { width = 100, align = "end", d },
                     Column { width = 200, align = "center", e },
                     Column { width = 120, Last { id = "fill", ProgressBar { fillWidth = true } } },
                     Column { width = 200, Last { id = "field", TextField { fillWidth = true } } },
                     Column { width = 150, Last { id = "minus", Spacer { width = -20 } } } })");
    m_h.renderFrames("root", 3);
    const float x0 = probe("start").pos().x;
    EXPECT_FLOAT_EQ(probe("d").pos().x - x0, 100.0f - 30.0f); // at the column's right edge, not the window's
    EXPECT_FLOAT_EQ(probe("e").pos().x - x0, 100.0f + (200.0f - 30.0f) * 0.5f);
    EXPECT_FLOAT_EQ(m_h.instance<test::LastItemProbe>("fill")->size().x, 120.0f);
    EXPECT_FLOAT_EQ(m_h.instance<test::LastItemProbe>("field")->size().x, 200.0f);
    EXPECT_FLOAT_EQ(m_h.instance<test::LastItemProbe>("minus")->size().x, 150.0f - 20.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// The narrowing ends with the column: a sibling after it fills the window again.
TEST_F(LayoutTest, AFixedWidthColumnRestoresTheRegion) {
    m_h.registerType<test::LastItemProbe>("Last");
    run(R"(
        root = Column { Column { width = 100, Probe {} },
                        Last { id = "after", ProgressBar { fillWidth = true } } })");
    m_h.renderFrames("root", 2);
    EXPECT_GT(m_h.instance<test::LastItemProbe>("after")->size().x, 500.0f);
}

// Wrapping text measures the column too: md::text (Text { wrap }) and raw ImGui::TextWrapped in a custom element,
// which also gets the column's width as its item width.
TEST_F(LayoutTest, AFixedWidthColumnWrapsText) {
    run(R"(
        local long = "The quick brown fox jumps over the lazy dog, again and again and again."
        rawText = Raw {}
        root = Column { Last { id = "line", Text { long } },
                        Column { width = 150, Last { id = "wrapped", Text { long, wrap = true } } },
                        Column { width = 120, rawText } })");
    m_h.renderFrames("root", 2);
    const ImVec2 line = lastSize("line"), wrapped = lastSize("wrapped");
    EXPECT_GT(line.x, 150.0f);
    EXPECT_LE(wrapped.x, 150.0f);
    EXPECT_GT(wrapped.x, 100.0f);
    EXPECT_GE(wrapped.y, 3.0f * line.y - 1.0f); // at least three lines
    RawImGui* raw = m_h.instance<RawImGui>("rawText");
    ASSERT_NE(raw, nullptr);
    EXPECT_FLOAT_EQ(raw->itemWidth(), 120.0f);
    EXPECT_LE(raw->text().x, 120.0f);
    EXPECT_GT(raw->text().y, 2.0f * line.y);
    EXPECT_TRUE(luaui::errors().empty());
}

// A padded Column without a width keeps the padding on the right too; with a width, the width is the content's.
TEST_F(LayoutTest, ColumnPaddingBoundsFillsOnBothSides) {
    run(R"(
        root = Column { Column { padding = 10, Last { id = "padded", ProgressBar { fillWidth = true } } },
                        Column { padding = 10, width = 200, Last { id = "sized", ProgressBar { fillWidth = true } } } })");
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(lastSize("padded").x, contentWidth() - 20.0f);
    EXPECT_FLOAT_EQ(lastMin("padded").x, ImGui::GetStyle().WindowPadding.x + 10.0f);
    EXPECT_FLOAT_EQ(lastSize("sized").x, 200.0f);
}

// The containers that size from the available width take a fixed Column's: Grid (a table), ScrollView (a child
// window), Card, Table, TopAppBar, NavigationBar, Divider, TabBar and Section.
TEST_F(LayoutTest, ContainersInAFixedWidthColumnTakeItsWidth) {
    const std::string elements[] = {
        "Grid { Probe {} }",
        "ScrollView { height = 40, Probe {} }",
        "Card { Probe {} }",
        "Table { columns = { 'A' }, rows = { { 1 } } }",
        "Table { outlined = false, columns = { 'A' }, rows = { { 1 } } }",
        "TopAppBar { title = 'x' }",
        "NavigationBar {}",
        "Divider {}",
        "TabBar { Tab { title = 'One' } }",
        "Section { title = 'S' }",
    };
    for (const std::string& e : elements) {
        run("root = Column { width = 300, Last { id = 'it', " + e + " } }");
        m_h.renderFrames("root", 2);
        EXPECT_FLOAT_EQ(lastSize("it").x, 300.0f) << e;
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// Inside a Card (a child window) and a Grid cell (a table cell in the same window), fills measure the card's content
// and the cell, also when the Card or Grid is in a fixed Column.
TEST_F(LayoutTest, CardsAndGridCellsAreTheirChildrensBox) {
    run(R"(
        root = Column { width = 300,
                        Card { padding = 10, Last { id = "inCard", ProgressBar { fillWidth = true } } },
                        Grid { columns = 2, spacing = 0, Last { id = "cell", ProgressBar { fillWidth = true } },
                               Probe {} } })");
    m_h.renderFrames("root", 3);
    EXPECT_FLOAT_EQ(lastSize("inCard").x, 300.0f - 20.0f);
    EXPECT_NEAR(lastSize("cell").x, 150.0f, 2.0f);
    EXPECT_TRUE(luaui::errors().empty());
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

// Children that fill share the width the others leave: a fill width (`fillWidth`), a type that fills by default
// (TextField, Card), each in a layout box of its share. The zero-width probes between them mark where each ends.
TEST_F(LayoutTest, RowFillsShareTheFreeWidth) {
    run(R"(
        a1 = Probe { w = 0, h = 0 } a2 = Probe { w = 0, h = 0 }
        halves = Row { width = 400, spacing = 0, Button { "A", fillWidth = true }, a1,
                                                 Button { "B", fillWidth = true }, a2 }
        b0 = Probe { w = 0, h = 0 } b1 = Probe { w = 0, h = 0 } b2 = Probe { w = 0, h = 0 }
        field = Row { width = 400, spacing = 10, b0, TextField {}, b1, Button { "OK" }, b2 }
        c1 = Probe { w = 0, h = 0 } c2 = Probe { w = 0, h = 0 }
        cards = Row { width = 500, spacing = 0, Card { Probe {} }, c1, Card { Probe {} }, c2 })");
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    m_h.renderFrames("halves", 3);
    EXPECT_FLOAT_EQ(probe("a1").pos().x, x0 + 200.0f);
    EXPECT_FLOAT_EQ(probe("a2").pos().x, x0 + 400.0f);

    m_h.renderFrames("field", 3);
    const float fieldW = probe("b1").pos().x - probe("b0").pos().x - 2.0f * 10.0f; // less the gaps around it
    const float buttonW = probe("b2").pos().x - probe("b1").pos().x - 2.0f * 10.0f;
    EXPECT_GT(buttonW, 40.0f);
    EXPECT_FLOAT_EQ(probe("b2").pos().x, x0 + 400.0f); // the button ends the row, nothing overflows
    EXPECT_FLOAT_EQ(fieldW, 400.0f - buttonW - 4.0f * 10.0f);

    m_h.renderFrames("cards", 3);
    EXPECT_FLOAT_EQ(probe("c1").pos().x, x0 + 250.0f);
    EXPECT_FLOAT_EQ(probe("c2").pos().x, x0 + 500.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// A fill-minus width resolves against the child's share; a child that doesn't fill is bounded by the ones after it,
// so a fill inside it (a Column with a filling bar) stops there.
TEST_F(LayoutTest, RowFillMinusAndBoundedChildren) {
    run(R"(
        p = Probe { w = 0, h = 0 }
        minus = Row { width = 400, spacing = 0, ProgressBar { width = -20 }, p, Probe { w = 50 } }
        boxed = Row { width = 400, spacing = 0, Last { id = "inner", width = -20, ProgressBar { width = -20 } },
                      Probe { w = 50 } }
        bounded = Row { width = 400, spacing = 10, Column { Last { id = "bar", ProgressBar { fillWidth = true } } },
                        Probe { w = 50 } })");
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    m_h.renderFrames("minus", 3);
    EXPECT_FLOAT_EQ(probe("p").pos().x, x0 + 350.0f); // the slot is the share
    m_h.renderFrames("boxed", 3);
    EXPECT_FLOAT_EQ(lastSize("inner").x, 350.0f - 20.0f); // the bar is the share minus 20
    m_h.renderFrames("bounded", 3);
    EXPECT_FLOAT_EQ(lastSize("bar").x, 400.0f - 10.0f - 50.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// Wrapping Text takes a share and wraps at it: the row stays within its width, the text runs over several lines.
TEST_F(LayoutTest, RowWidthBoundsAWrappingText) {
    run(R"(
        local long = "The quick brown fox jumps over the lazy dog, again and again and again."
        line = Last { Text { long } }
        root = Last { Row { width = 250, spacing = 0, Probe { w = 100, h = 10 }, Text { long, wrap = true } } })");
    m_h.renderFrame("line");
    const ImVec2 line = lastSize("line");
    ASSERT_GT(line.x, 300.0f);
    m_h.renderFrames("root", 3);
    const ImVec2 row = lastSize("root");
    EXPECT_FLOAT_EQ(row.x, 250.0f);         // the text's share is 150
    EXPECT_GE(row.y, 3.0f * line.y - 1.0f); // so it runs over three lines
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LayoutTest, RowSkipsHiddenAndRendersNonVisualChildren) {
    run(R"(
        fired = 0
        a = Probe { w = 50 } b = Probe { w = 30, visible = false } c = Probe { w = 30 }
        root = Row { spacing = 0, a, b, Timer { interval = 1, onTriggered = function() fired = fired + 1 end }, c })");
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

TEST_F(LayoutTest, GridColumnsAreCappedAtImGuisLimit) {
    run(R"(
        a = Probe {} b = Probe {}
        root = Grid { columns = 100000, a, b })");
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(probe("b").pos().y, probe("a").pos().y);
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("at most 511"), std::string::npos) << luaui::errors()[0];
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

// Non-visual children are no tabs: they don't count for currentIndex and render whichever tab is shown, so a
// Dialog in a TabBar opens.
TEST_F(LayoutTest, TabBarRendersNonVisualChildrenInPlace) {
    run(R"(
        fired = 0
        a = Probe {} b = Probe {} c = Probe {}
        root = TabBar { currentIndex = 2,
                        Timer { interval = 10, onTriggered = function() fired = fired + 1 end },
                        Tab { title = "One", a },
                        Dialog { title = "Dlg", open = true, c },
                        Tab { title = "Two", b } })");
    m_h.renderFrames("root", 3);
    EXPECT_EQ(probe("a").renders(), 0);
    EXPECT_EQ(probe("b").renders(), 3); // the second Tab, not the Timer's slot
    EXPECT_GE(probe("c").renders(), 2); // the dialog is open
    check("fired > 0");

    // The index is clamped among the tabs only.
    run("root.currentIndex = 3");
    m_h.renderFrame("root");
    EXPECT_EQ(probe("b").renders(), 4);
    run("root.currentIndex = math.mininteger");
    m_h.renderFrame("root");
    EXPECT_EQ(probe("a").renders(), 1);
    EXPECT_TRUE(luaui::errors().empty());
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

// ------------------------------------------------------------ Sizes

TEST(ItemSize, ResolveSizeRules) {
    EXPECT_FLOAT_EQ(resolveSize(40.0f, 300.0f, 7.0f), 40.0f);   // fixed
    EXPECT_FLOAT_EQ(resolveSize(0.0f, 300.0f, 7.0f), 7.0f);     // unset: natural
    EXPECT_FLOAT_EQ(resolveSize(-1.0f, 300.0f, 7.0f), 300.0f);  // fill
    EXPECT_FLOAT_EQ(resolveSize(-0.5f, 300.0f, 7.0f), 300.0f);  // anything from -1 up to 0 fills
    EXPECT_FLOAT_EQ(resolveSize(-20.0f, 300.0f, 7.0f), 280.0f); // fill minus
    EXPECT_FLOAT_EQ(resolveSize(-400.0f, 300.0f, 7.0f), 1.0f);  // at least 1px
}

// Every element resolves fill (-1) and fill minus (< -1) widths the same way: the available width, and that
// minus |v| dp. (Column and Canvas used to be a pixel wider on fill minus, the others a pixel narrower on fill, and
// Spacer ignored fill values.)
TEST_F(LayoutTest, FillAndFillMinusWidthsAreTheSameForEveryElement) {
    m_h.registerType<test::LastItemProbe>("Last");
    // Each element, with its width as `W`.
    const std::string elements[] = {
        "ProgressBar { width = W }",
        "Column { width = W }",
        "Canvas { width = W, height = 10 }",
        "Row { width = W, Spacer { fill = true } }",
        "Spacer { width = W }",
        "TopAppBar { width = W }",
        "NavigationBar { width = W }",
    };
    float fill = 0.0f;
    for (const std::string& e : elements) {
        for (const std::string w : {"-1", "-20"}) {
            std::string code = e;
            code.replace(code.find('W'), 1, w);
            run("root = Last { " + code + " }");
            m_h.renderFrame("root"); // right from the first frame: a Row gives a fill its share before it renders
            const float got = m_h.instance<test::LastItemProbe>("root")->size().x;
            if (fill == 0.0f) fill = got; // the first: ProgressBar, through md::itemWidth
            EXPECT_FLOAT_EQ(got, w == "-1" ? fill : fill - 20.0f) << code;
        }
    }
    EXPECT_GT(fill, 100.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LayoutTest, FillHeights) {
    m_h.registerType<test::LastItemProbe>("Last");
    run("root = Last { Spacer { height = 30 } }");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(m_h.instance<test::LastItemProbe>("root")->size().y, 30.0f);
    run("root = Last { Spacer { } }");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(m_h.instance<test::LastItemProbe>("root")->size().y, 1.0f); // unset: 1px
    run("root = Last { Canvas { height = -1 } }");
    m_h.renderFrame("root");
    const float full = m_h.instance<test::LastItemProbe>("root")->size().y;
    EXPECT_GT(full, 200.0f); // fills the window, not the 200dp default
    run("root = Last { Canvas { height = -50 } }");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(m_h.instance<test::LastItemProbe>("root")->size().y, full - 50.0f);
    run("root = Last { Spacer { height = -50 } }");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(m_h.instance<test::LastItemProbe>("root")->size().y, full - 50.0f);
}

// ------------------------------------------------------------ Layout cache and convergence

// Rendering is on demand: a Timer (or a posted change) renders one frame, in which `align` still places a changed
// label by its old width. The cache sees the mismatch and asks for another frame, so the label ends up centered once
// the frames stop, and a settled layout asks for nothing more.
TEST_F(LayoutTest, ACenteredLabelSettlesAfterATimerChangesIt) {
    run(R"(
        root = Column { align = "center",
                        Last { id = "boxed", Text { id = "caption", text = "Hi" } },
                        Timer { interval = 50, ["repeat"] = false,
                                onTriggered = function() caption.text = "A much longer label than before" end } })");
    const auto centered = [&] {
        const ImVec2 min = lastMin("boxed"), size = lastSize("boxed");
        return std::fabs(min.x + size.x * 0.5f - (ImGui::GetStyle().WindowPadding.x + contentWidth() * 0.5f)) <= 1.0f;
    };
    m_h.renderFrame("root");
    renderWhileRequested("root");
    EXPECT_TRUE(centered());
    // Settled: nothing asks for a frame now (the Timer's own wake-up is later).
    EXPECT_GT(secondsUntilFrame(), 0.0);

    // The frames the Timer's wake-ups render, until it fires.
    const float before = lastSize("boxed").x;
    for (int i = 0; i < 20 && lastSize("boxed").x == before; ++i)
        m_h.renderFrame("root");
    ASSERT_GT(lastSize("boxed").x, before);
    EXPECT_FALSE(centered());            // placed by the old width on the frame it changed
    EXPECT_EQ(secondsUntilFrame(), 0.0); // so the cache asked for another frame
    EXPECT_GE(renderWhileRequested("root"), 1);
    EXPECT_TRUE(centered());
    EXPECT_GT(secondsUntilFrame(), 0.0);

    // A posted change (a script setting a prop between frames) settles the same way.
    run("caption.text = 'Hi'");
    m_h.renderFrame("root");
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    renderWhileRequested("root");
    EXPECT_TRUE(centered());
    EXPECT_TRUE(luaui::errors().empty());
}

// A layout that can never settle (a child that grows every frame) asks for a few frames, then stops.
TEST_F(LayoutTest, AnUnsettledLayoutStopsAskingForFrames) {
    run(R"(root = Column { align = "center", Grower {} })");
    m_h.renderFrame("root");
    const int frames = renderWhileRequested("root", 100);
    EXPECT_LE(frames, layout::maxSettleFrames + 1);
    EXPECT_GT(secondsUntilFrame(), 0.0);
}

// Sizes of elements that went away are dropped: a list whose items change every frame doesn't grow the cache.
TEST_F(LayoutTest, TheLayoutCacheDropsElementsThatAreGone) {
    run(R"(
        items = {}
        root = Column { align = "center",
                        Repeater { model = function() return items end,
                                   delegate = function(item) return Text { text = item } end } }
        function refill(n) items = {} for i = 1, 20 do items[i] = n .. "-" .. i end end)");
    std::size_t first = 0;
    for (int n = 0; n < 50; ++n) {
        run("refill(" + std::to_string(n) + ")");
        m_h.renderFrame("root");
        if (n == 2) first = layout::cacheEntries();
    }
    EXPECT_GE(first, 20u);
    EXPECT_LE(layout::cacheEntries(), first + 25u); // at most this frame's and the last frame's items
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ Intrinsic sizes

// An intrinsic size must be exactly what the element then renders at (or layouts would keep asking for frames):
// Text, Icon, Button, IconButton and Spacer report theirs; wrapping or aligned Text and fills don't.
TEST_F(LayoutTest, IntrinsicSizesAreWhatTheElementsRender) {
    run(R"(
        root = Measure {
            Text { "Hello" }, Text { "" }, Text { "Two\nlines", style = "headlineSmall" },
            Icon { "home" }, Icon { "home", size = 40 },
            Button { "OK" }, Button { "Save", icon = "save", variant = "tonal" }, Button { "Go", variant = "text" },
            Button { variant = "fab", icon = "add" }, Button { "Compose", variant = "fab", icon = "edit" },
            Button { "Wide", width = 200, height = 50 }, IconButton { "menu" },
            Spacer {}, Spacer { width = 30, height = 12 },
            Text { "wrapped", wrap = true }, Text { "centered", align = "center" },
            Button { "fill", fillWidth = true }, Spacer { fill = true }, Probe {} })");
    m_h.renderFrames("root", 2);
    Measure* m = m_h.instance<Measure>("root");
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->results.size(), 19u);
    for (std::size_t i = 0; i < m->results.size(); ++i) {
        const Measure::Result& r = m->results[i];
        if (i >= 14) {
            EXPECT_FALSE(r.intrinsic) << i << " " << r.type;
            continue;
        }
        ASSERT_TRUE(r.intrinsic) << i << " " << r.type;
        EXPECT_FLOAT_EQ(r.intrinsic->x, r.rendered.x) << i << " " << r.type;
        EXPECT_FLOAT_EQ(r.intrinsic->y, r.rendered.y) << i << " " << r.type;
        EXPECT_GT(r.rendered.y, 0.0f) << i << " " << r.type;
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// Layouts place children with an intrinsic size right from their first frame (no misplaced frame, no extra one):
// a centered Column, a Row justified to the end and aligned to the center, TopAppBar actions.
TEST_F(LayoutTest, IntrinsicSizesPlaceChildrenOnTheirFirstFrame) {
    run(R"(
        centered = Probe { w = 40, intrinsic = true }
        short = Probe { w = 30, h = 10, intrinsic = true } tall = Probe { w = 40, h = 40, intrinsic = true }
        action = Probe { w = 48, h = 48, intrinsic = true }
        root = Column {
            Column { align = "center", centered },
            Row { width = 300, spacing = 0, justify = "end", align = "center", short, tall },
            TopAppBar { title = "Title", action } })");
    m_h.renderFrame("root");
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    EXPECT_FLOAT_EQ(probe("centered").pos().x, x0 + (contentWidth() - 40.0f) * 0.5f);
    EXPECT_FLOAT_EQ(probe("short").pos().x, x0 + 300.0f - 70.0f);
    EXPECT_FLOAT_EQ(probe("tall").pos().x, x0 + 300.0f - 40.0f);
    EXPECT_FLOAT_EQ(probe("short").pos().y, probe("tall").pos().y + 15.0f);
    EXPECT_FLOAT_EQ(probe("action").pos().x, x0 + contentWidth() - 4.0f - 48.0f);
    EXPECT_TRUE(probe("centered").seen());
    EXPECT_TRUE(probe("short").seen());
    EXPECT_TRUE(probe("action").seen());
    // Nothing was misplaced, so nothing asks for another frame.
    EXPECT_GT(secondsUntilFrame(), 0.0);
    EXPECT_TRUE(luaui::errors().empty());
}

// A child without an intrinsic size or a size from the last frame, which a non-start align must place, renders
// unseen on its first frame (measured, not drawn in the wrong place) and asks for the frame that shows it in place.
TEST_F(LayoutTest, AChildWithoutASizeRendersUnseenOnItsFirstFrame) {
    run(R"(
        centered = Probe { w = 40 } rowed = Probe { w = 40 } plain = Probe { w = 40 }
        root = Column { Column { align = "center", centered }, Row { justify = "end", rowed }, plain })");
    m_h.renderFrame("root");
    EXPECT_FALSE(probe("centered").seen());
    EXPECT_FALSE(probe("rowed").seen());
    EXPECT_TRUE(probe("plain").seen()); // placed without knowing its size
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    EXPECT_GE(renderWhileRequested("root"), 1);
    EXPECT_TRUE(probe("centered").seen());
    EXPECT_TRUE(probe("rowed").seen());
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    EXPECT_FLOAT_EQ(probe("centered").pos().x, x0 + (contentWidth() - 40.0f) * 0.5f);
    EXPECT_FLOAT_EQ(probe("rowed").pos().x, x0 + contentWidth() - 40.0f);
    EXPECT_GT(secondsUntilFrame(), 0.0);
}
