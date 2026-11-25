// The layout built-ins as typed elements (Column/Item, Row, Grid, ScrollView/ListView, Card, TabBar/Tab,
// Section/Expander, TreeNode): defaults, props reaching the layout (measured with a probe element that records
// where it was placed), two-way write-back through bind, signals, ScrollView keeping its scroll position
// when it moves to another place in the tree, element identity (a recycled table address starts fresh) and the
// once-a-frame sync seeing a write that bypassed the change count. Renders headless ImGui frames
// (tests/support/headless_frame.cppm).
#include <gtest/gtest.h>

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
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
import imlui.theme.dpi;
import imlui.theme.theme;
import imlui.ui.frame_request;
import imlui.ui.item_size;
import imlui.ui.layout_cache;
import imlui.color;
import imlui.widgets.card;
import imlui.widgets.surface;

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
        ++m_queries;
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
    /// How often intrinsicSize() ran (TypeRegistry memoizes it per frame and maxWidth).
    [[nodiscard]] int queries() const { return m_queries; }
    /// Whether it could be seen when it last rendered (not clipped away).
    [[nodiscard]] bool seen() const { return m_seen; }

private:
    float m_w = 10.0f;
    float m_h = 10.0f;
    ImVec2 m_pos{-1.0f, -1.0f};
    float m_itemWidth = 0.0f;
    int m_renders = 0;
    int m_queries = 0;
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

/// Emits onRender while it renders (Lua code running mid-frame, like a handler) and takes no space.
class Writer final : public Element {
public:
    static void declare(TypeBuilder<Writer>& type) { type.signal("onRender"); }
    void render(Frame&) override { emit("onRender"); }
};

/// Takes its resolved `width` (a fill-minus) and records where it was placed and how wide it was.
class Filler final : public Element {
public:
    static void declare(TypeBuilder<Filler>&) {}
    void render(Frame& frame) override {
        m_pos = ImGui::GetCursorScreenPos();
        m_w = frame.resolvedWidth();
        ImGui::Dummy({m_w, 10.0f});
    }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }
    [[nodiscard]] float w() const { return m_w; }

private:
    ImVec2 m_pos{-1.0f, -1.0f};
    float m_w = 0.0f;
};

/// Renders its children; before each it records the child's ImGui ID (Frame::Child::id()) and the size the layout
/// cache holds for it.
class Inspect final : public Element {
public:
    static void declare(TypeBuilder<Inspect>&) {}
    void render(Frame& frame) override {
        ids.clear();
        sizes.clear();
        frame.forEachChild([&](Frame::Child& c) {
            ids.push_back(c.id());
            sizes.push_back(layout::lastSize(c.id()));
            c.render();
        });
    }
    std::vector<ImGuiID> ids;
    std::vector<std::optional<ImVec2>> sizes;
};

/// Counts its renders in ImGui's state storage, under an ID of its own scope (like a widget keeps an open tree or a
/// scroll position): the count goes on while the element's ImGui ID scope is the same.
class Counter final : public Element {
public:
    static void declare(TypeBuilder<Counter>&) {}
    void render(Frame&) override {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID key = ImGui::GetID("##renders");
        m_count = storage->GetInt(key) + 1;
        storage->SetInt(key, m_count);
        ImGui::Dummy({30.0f, 10.0f});
    }
    [[nodiscard]] int count() const { return m_count; }

private:
    int m_count = 0;
};

/// Asks each child for its instance (a layout's query: the child is synced), then, when `w` is set, writes it into
/// the child's `w` field with a raw lua_setfield, which luaui::changes() doesn't count, or through Node::set
/// (`counted`), which it does, and renders the child.
class RawWriter final : public Element {
public:
    static void declare(TypeBuilder<RawWriter>& type) {
        type.property("w", &RawWriter::w, &RawWriter::setW)
            .property("counted", &RawWriter::counted, &RawWriter::setCounted);
    }
    void render(Frame& frame) override {
        frame.forEachChild([&](Frame::Child& c) {
            (void)c.syncProperties();
            if (m_w > 0.0) {
                if (m_counted) {
                    c.node().set("w", m_w);
                } else {
                    lua::pushnumber(c.node().state(), m_w);
                    lua::setfield(c.node().state(), c.node().index(), "w");
                }
            }
            c.render();
        });
    }
    [[nodiscard]] double w() const { return m_w; }
    void setW(double w) { m_w = w; }
    [[nodiscard]] bool counted() const { return m_counted; }
    void setCounted(bool on) { m_counted = on; }

private:
    double m_w = 0.0;
    bool m_counted = false;
};

/// Records the Material surface it renders on (md::surfaceColor) and how many cards are open around it; with
/// `boom` set it throws instead, cutting the render pass short.
class SurfaceProbe final : public Element {
public:
    static void declare(TypeBuilder<SurfaceProbe>& type) {
        type.property("boom", &SurfaceProbe::boom, &SurfaceProbe::setBoom);
    }
    void render(Frame&) override {
        if (m_boom) throw std::runtime_error("surface probe exploded");
        m_surface = md::surfaceColor();
        m_cards = md::cardDepth();
        ImGui::Dummy({10.0f, 10.0f});
    }
    [[nodiscard]] bool boom() const { return m_boom; }
    void setBoom(bool on) { m_boom = on; }
    [[nodiscard]] Color surface() const { return m_surface; }
    [[nodiscard]] std::size_t cards() const { return m_cards; }

private:
    bool m_boom = false;
    Color m_surface{};
    std::size_t m_cards = 99;
};

bool hasError(std::string_view part) {
    for (const auto& e : luaui::errors())
        if (e.find(part) != std::string::npos) return true;
    return false;
}

class LayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_h.registerType<Probe>("Probe");
        m_h.registerType<test::LastItemProbe>("Last");
        m_h.registerType<RawImGui>("Raw");
        m_h.registerType<Grower>("Grower");
        m_h.registerType<Measure>("Measure");
        m_h.registerType<Writer>("Writer");
        m_h.registerType<Filler>("Filler");
        m_h.registerType<Inspect>("Inspect");
        m_h.registerType<Counter>("Counter");
        m_h.registerType<RawWriter>("RawWriter");
        m_h.registerType<SurfaceProbe>("SurfaceProbe");
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
    EXPECT_FLOAT_EQ(probe("b").itemWidth(), 200.0f - 2.0f * 10.0f); // the width less the padding on each side
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

// The padding insets a Column's content on every side, with or without a width; a width is the column's own,
// padding included (like a Card's).
TEST_F(LayoutTest, ColumnPaddingBoundsFillsOnBothSides) {
    run(R"(
        root = Column { Column { padding = 10, Last { id = "padded", ProgressBar { fillWidth = true } } },
                        Column { padding = 10, width = 200, Last { id = "sized", ProgressBar { fillWidth = true } } },
                        Column { padding = 16, width = -1, Last { id = "filled", ProgressBar { fillWidth = true } } } })");
    m_h.renderFrames("root", 2);
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    EXPECT_FLOAT_EQ(lastSize("padded").x, contentWidth() - 20.0f);
    EXPECT_FLOAT_EQ(lastMin("padded").x, x0 + 10.0f);
    EXPECT_FLOAT_EQ(lastSize("sized").x, 200.0f - 20.0f);
    EXPECT_FLOAT_EQ(lastMin("sized").x, x0 + 10.0f);
    EXPECT_FLOAT_EQ(lastSize("filled").x, contentWidth() - 32.0f); // used to reach the right edge: no inset there
    EXPECT_FLOAT_EQ(lastMin("filled").x, x0 + 16.0f);
}

// A padded Column's size (its group's, and the intrinsic size a layout places it by) is its content plus the padding
// on both sides, so a Row puts the next child after the right padding.
TEST_F(LayoutTest, APaddedColumnsSizeIncludesThePaddingOnBothSides) {
    run(R"(
        a = Probe { w = 40, h = 20, intrinsic = true } b = Probe { w = 30, h = 20, intrinsic = true }
        c = Probe { w = 10, intrinsic = true } d = Probe { w = 10, intrinsic = true }
        measure = Measure { Column { padding = 10, a },
                            Column { padding = 10, width = 200, b },
                            Column { padding = 8, width = -1, Probe { w = 10, intrinsic = true } } }
        row = Row { spacing = 0, align = "top", Column { padding = 12, c }, d })");
    m_h.renderFrames("measure", 2);
    Measure* m = m_h.instance<Measure>("measure");
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->results.size(), 3u);
    const float widths[] = {40.0f + 20.0f, 200.0f, contentWidth()};
    for (std::size_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(m->results[i].intrinsic.has_value()) << i;
        EXPECT_FLOAT_EQ(m->results[i].intrinsic->x, widths[i]) << i;
        EXPECT_FLOAT_EQ(m->results[i].rendered.x, widths[i]) << i;
        EXPECT_FLOAT_EQ(m->results[i].intrinsic->y, m->results[i].rendered.y) << i;
    }
    m_h.renderFrames("row", 2);
    EXPECT_FLOAT_EQ(probe("d").pos().x - probe("c").pos().x, 10.0f + 12.0f);
    EXPECT_TRUE(luaui::errors().empty());
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
        // The previous tree is collected first: its `it` would keep the id (the first element registered keeps it).
        run("root = nil; collectgarbage(); root = Column { width = 300, Last { id = 'it', " + e + " } }");
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

// A child of an unknown type (a misspelled `__type`) is reported and takes no slot: it used to count as visual, so a
// Row gave it a gap and advanced by the previous sibling's size, and a stack couldn't compose its size.
TEST_F(LayoutTest, AnUnknownElementTypeTakesNoSlot) {
    run(R"(
        a = Probe { w = 50, intrinsic = true } c = Probe { w = 30, intrinsic = true }
        d = Probe { w = 20, intrinsic = true }
        measure = Measure { Row { spacing = 10, a, { __type = "Buttn" }, c },
                            Column { align = "end", width = 100, { __type = "Buttn" }, d } })");
    m_h.renderFrames("measure", 2);
    EXPECT_TRUE(hasError("unknown element type 'Buttn'"));
    EXPECT_FLOAT_EQ(probe("c").pos().x - probe("a").pos().x, 50.0f + 10.0f);
    Measure* m = m_h.instance<Measure>("measure");
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->results.size(), 2u);
    ASSERT_TRUE(m->results[0].intrinsic.has_value()); // known: the unknown child isn't asked
    EXPECT_FLOAT_EQ(m->results[0].intrinsic->x, 50.0f + 10.0f + 30.0f);
    EXPECT_FLOAT_EQ(m->results[0].rendered.x, 90.0f);
    ASSERT_TRUE(m->results[1].intrinsic.has_value());
    EXPECT_TRUE(probe("d").seen()); // placed by its size, not rendered unseen for the unknown sibling
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

// A model function that returns fresh item tables on every call rebuilt every delegate on every call (identity by
// item), losing their state. With `key`, a delegate is kept while an item with its key is in the model, also when it
// moves to another index; repeated keys keep one delegate each, and a delegate whose key went away is dropped.
TEST_F(LayoutTest, KeyedDelegatesSurviveFreshModelItems) {
    run(R"(
        built, plainBuilt = 0, 0
        items = { 1, 2, 3 }
        local function fresh()
            local out = {}
            for i, id in ipairs(items) do out[i] = { id = id } end
            return out
        end
        rep = Repeater { model = fresh, key = function(item) return item.id end,
                         delegate = function(item) built = built + 1; return Probe { h = 5 } end }
        lv = ListView { height = 100, model = fresh, key = function(item, i) return item.id end,
                        delegate = function(item) return Probe { h = 5 } end }
        plain = Repeater { model = fresh, delegate = function() plainBuilt = plainBuilt + 1; return Probe {} end }
        root = Column { rep, lv, plain })");
    m_h.renderFrames("root", 3);
    check("built == 3");
    check("plainBuilt > 3"); // without a key every call rebuilds them
    run("first, third, lvFirst = rep.__list[1], rep.__list[3], lv.__list[1]");
    run("ui.__dropped = false; items = { 4, 1, 3 }");
    m_h.renderFrame("root");
    check("built == 4");
    check("#rep.__list == 3 and rep.__list[2] == first and rep.__list[3] == third");
    check("rep.__list[1] ~= first and lv.__list[2] == lvFirst");
    check("ui.__dropped"); // 2 went away
    run("items = { 3, 3, 1 }");
    m_h.renderFrames("root", 2);
    check("built == 5 and rep.__list[1] == third and rep.__list[3] == first");
    run("items = {}");
    m_h.renderFrame("root");
    check("#rep.__list == 0 and #lv.__list == 0");
    EXPECT_TRUE(luaui::errors().empty());
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

// A Card or ScrollView clipped away (below the window here, like scrolled out of view) doesn't render its content:
// under SkipItems it would record near-zero sizes, ask for settle frames, and place a Row inside it wrong on the
// frame it comes back.
TEST_F(LayoutTest, ClippedCardAndScrollViewSkipTheirContent) {
    run(R"(
        big = Probe { h = 10 }
        a = Probe { w = 50 } b = Probe {}
        c = Probe { w = 50 } d = Probe {}
        root = Column { big, Card { Row { a, b } }, ScrollView { height = 100, Row { c, d } } })");
    m_h.renderFrames("root", 6);
    Probe* a = m_h.instance<Probe>("a");
    Probe* b = m_h.instance<Probe>("b");
    Probe* c = m_h.instance<Probe>("c");
    Probe* d = m_h.instance<Probe>("d");
    ASSERT_TRUE(a && b && c && d);
    const float bx = b->pos().x - a->pos().x;
    const float dx = d->pos().x - c->pos().x;
    EXPECT_GE(bx, 50.0f);
    EXPECT_GE(dx, 50.0f);

    run("big.h = 2000"); // both go below the 600 px window
    m_h.renderFrames("root", 3);
    const int aRenders = a->renders();
    const int cRenders = c->renders();
    m_h.renderFrames("root", 4);
    EXPECT_EQ(a->renders(), aRenders);
    EXPECT_EQ(c->renders(), cRenders);
    EXPECT_EQ(secondsUntilFrame(), std::numeric_limits<double>::infinity()); // settled, nothing to redo

    run("big.h = 10"); // back in view: the first frame places the Rows right
    m_h.renderFrame("root");
    EXPECT_GT(a->renders(), aRenders);
    EXPECT_GT(c->renders(), cRenders);
    EXPECT_FLOAT_EQ(b->pos().x - a->pos().x, bx);
    EXPECT_FLOAT_EQ(d->pos().x - c->pos().x, dx);
    EXPECT_TRUE(luaui::errors().empty());
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

// An error inside a Card (a C++ exception here, a Lua error alike) cuts the render pass short past md::endCard():
// the card's surface and card entries are dropped with the ImGui stacks, so what renders afterwards (the rest of the
// frame, the next frames) isn't drawn on the dead card's surface.
TEST_F(LayoutTest, ARecoveredRenderErrorLeavesNoCardOrSurfaceBehind) {
    run(R"(
        boom = SurfaceProbe { boom = true } inside = SurfaceProbe {} outside = SurfaceProbe {}
        root = Column { Card { color = "#ff0000", inside, Card { boom } }, outside })");
    m_h.renderFrame("root");
    EXPECT_TRUE(hasError("surface probe exploded"));
    EXPECT_EQ(md::surfaceDepth(), 0u); // unwound in the frame that threw
    EXPECT_EQ(md::cardDepth(), 0u);
    luaui::clearErrors();
    run("boom.boom = false");
    m_h.renderFrame("root");
    EXPECT_TRUE(luaui::errors().empty());
    SurfaceProbe* inside = m_h.instance<SurfaceProbe>("inside");
    SurfaceProbe* outside = m_h.instance<SurfaceProbe>("outside");
    ASSERT_NE(inside, nullptr);
    ASSERT_NE(outside, nullptr);
    EXPECT_EQ(inside->surface().vec4().x, 1.0f);
    EXPECT_EQ(inside->cards(), 1u);
    EXPECT_EQ(outside->cards(), 0u);
    const Color windowBg = Color::from(ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
    EXPECT_EQ(outside->surface().vec4().x, windowBg.vec4().x);
    EXPECT_EQ(outside->surface().vec4().y, windowBg.vec4().y);
    EXPECT_EQ(outside->surface().vec4().z, windowBg.vec4().z);
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

// Sizes of elements that went away are dropped: a list whose items change every frame doesn't grow the cache past
// its cap (the least recently used entries go first), and entries not used for long (in frames and in time) go.
TEST_F(LayoutTest, TheLayoutCacheDropsElementsThatAreGone) {
    run(R"(
        items = {}
        root = Column { align = "center",
                        Repeater { model = function() return items end,
                                   delegate = function(item) return Text { text = item } end } }
        function refill(n, count) items = {} for i = 1, count do items[i] = n .. "-" .. i end end)");
    constexpr int perFrame = 500;
    const int frames = static_cast<int>(2 * layout::maxEntries / perFrame); // past the cap
    std::size_t most = 0;
    for (int n = 0; n < frames; ++n) {
        run("refill(" + std::to_string(n) + ", " + std::to_string(perFrame) + ")");
        m_h.renderFrame("root");
        most = std::max(most, layout::cacheEntries());
    }
    // It keeps gone items for a while (a hidden page's sizes), up to the cap (Lua reuses some tables' addresses,
    // which are the keys, so not every frame adds `perFrame`) ...
    EXPECT_GT(most, layout::maxEntries);
    EXPECT_LE(most, layout::maxEntries + perFrame); // ... and no further (one frame's new entries over it at most)
    EXPECT_LE(layout::cacheEntries(), layout::maxEntries + perFrame);

    // Unused for keepFrames frames and keepSeconds seconds: dropped, down to what is still shown.
    run("refill(-1, 3)");
    const int idle = std::max(layout::keepFrames, static_cast<int>(layout::keepSeconds * 60.0)) + 2;
    m_h.renderFrames("root", idle);
    EXPECT_LE(layout::cacheEntries(), 10u);
    EXPECT_TRUE(luaui::errors().empty());
}

// A page a TabBar hides keeps its sizes: switching back shows its aligned and Row children in place on the first
// frame, never unseen (the cache used to drop them after two frames, so every switch re-measured them unseen).
TEST_F(LayoutTest, SwitchingTabsNeverRendersAChildUnseenAgain) {
    run(R"(
        a = Probe { w = 40 } b = Probe { w = 50 } c = Probe { w = 30, h = 30 }
        root = TabBar {
            Tab { title = "A", Row { justify = "end", a } },
            Tab { title = "B", Column { align = "center", b }, Row { Probe { intrinsic = true }, c } } })");
    m_h.renderFrame("root");
    renderWhileRequested("root");
    ASSERT_TRUE(probe("a").seen());
    run("root.currentIndex = 2");
    m_h.renderFrame("root"); // b's first appearance: unseen is fine here
    renderWhileRequested("root");
    ASSERT_TRUE(probe("b").seen());
    for (int round = 0; round < 6; ++round) {
        run("root.currentIndex = 1");
        m_h.renderFrame("root");
        EXPECT_TRUE(probe("a").seen()) << round;
        m_h.renderFrames("root", 100); // hidden long past the old two frames
        run("root.currentIndex = 2");
        m_h.renderFrame("root");
        EXPECT_TRUE(probe("b").seen()) << round;
        EXPECT_TRUE(probe("c").seen()) << round;
        m_h.renderFrames("root", 100);
    }
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    EXPECT_FLOAT_EQ(probe("b").pos().x, x0 + (contentWidth() - 50.0f) * 0.5f);
    EXPECT_TRUE(luaui::errors().empty());
}

// A fill-minus child under a centered (or end-aligned) stack: its width is the box's less N, known before it
// renders, so it is placed on its first frame, and nothing asks for frames to settle it.
TEST_F(LayoutTest, AFillMinusChildUnderAnAlignedStackSettlesOnItsFirstFrame) {
    run(R"(
        centered = Filler { width = -100 } ended = Filler { width = -60 }
        root = Column { Column { align = "center", centered }, Column { align = "end", ended } })");
    m_h.renderFrame("root");
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    const auto check = [&] {
        Filler* c = m_h.instance<Filler>("centered");
        Filler* e = m_h.instance<Filler>("ended");
        ASSERT_NE(c, nullptr);
        ASSERT_NE(e, nullptr);
        EXPECT_FLOAT_EQ(c->w(), contentWidth() - 100.0f);
        EXPECT_FLOAT_EQ(c->pos().x, x0 + 50.0f);
        EXPECT_FLOAT_EQ(e->w(), contentWidth() - 60.0f);
        EXPECT_FLOAT_EQ(e->pos().x, x0 + 60.0f);
    };
    check();
    EXPECT_LE(renderWhileRequested("root"), 1);
    check();
    EXPECT_GT(secondsUntilFrame(), 0.0);
    EXPECT_TRUE(luaui::errors().empty());
}

// A Row asks a child whether it fills and for its intrinsic size, then renders it: one property sync a frame (a
// function-bound prop is read once).
TEST_F(LayoutTest, ARowSyncsEachChildOncePerFrame) {
    run(R"(
        calls = 0
        root = Row { Text { text = function() calls = calls + 1 return "label" end }, Probe { w = 20 } })");
    m_h.renderFrames("root", 3);
    run("calls = 0");
    m_h.renderFrame("root");
    check("calls == 1");
    run("calls = 0");
    m_h.renderFrames("root", 4);
    check("calls == 4");
}

// A container's children are resolved once a frame: a Repeater's model function ran for every pass (a Row's three,
// its parent's fill and size questions). A handler that runs in between makes the next pass resolve them again, so it
// sees the change on the same frame.
TEST_F(LayoutTest, ChildrenAreResolvedOncePerFrame) {
    run(R"(
        calls, built = 0, 0
        items = { "a", "b", "c" }
        root = Column { align = "center",
                        Row { Repeater { model = function() calls = calls + 1 return items end,
                                         delegate = function() built = built + 1 return Probe { w = 5 } end } } })");
    m_h.renderFrames("root", 3);
    run("calls = 0");
    m_h.renderFrame("root");
    check("calls == 1");
    run("calls = 0");
    m_h.renderFrames("root", 4);
    check("calls == 4 and built == 3");
    run(R"(
        calls = 0
        added = false
        mid = Row { Writer { onRender = function() if not added then added = true; items[#items + 1] = "d" end end },
                    Repeater { model = function() calls = calls + 1 return items end,
                               delegate = function() built = built + 1 return Probe { w = 5 } end } })");
    m_h.renderFrame("mid");
    check("calls == 2"); // the layout passes, then the render after the handler
    check("built == 3 + 4");
}

// A numeric model is items 1..n, without a table of them: fractions round down, and NaN or less than 0 is none.
TEST_F(LayoutTest, ANumericModelIsItsIndices) {
    run(R"(
        seen = {}
        n = 3
        root = Column { Repeater { model = function() return n end,
                                   delegate = function(item, i) seen[#seen + 1] = item * 10 + i return Probe {} end } })");
    m_h.renderFrame("root");
    check("#seen == 3 and seen[1] == 11 and seen[3] == 33");
    for (const char* count : {"2.7", "-1", "0 / 0", "0"}) {
        run(std::string{"seen = {}; n = "} + count);
        m_h.renderFrame("root");
        run("count = #root[1].__list");
        check(std::string{"count == "} + (count[0] == '2' ? "2" : "0"));
        check("#seen == 0"); // nothing new built
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// ... but Lua code that runs between a layout's questions and the render (a handler) may change the child: it is
// synced again, so the change shows on the same frame.
TEST_F(LayoutTest, AChangeMadeMidFrameIsSyncedBeforeTheChildRenders) {
    run(R"(
        label = Probe { w = 10 } after = Probe { w = 10 }
        root = Row { align = "top", spacing = 0,
                     Writer { onRender = function() if grow then label.w = 50 end end }, label, after })");
    m_h.renderFrames("root", 3);
    EXPECT_FLOAT_EQ(probe("after").pos().x - probe("label").pos().x, 10.0f);
    run("grow = true");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(probe("after").pos().x - probe("label").pos().x, 50.0f);
}

// A Lua write that bypasses luaui::changes() (a raw lua_setfield from C++, between a layout's query and the render)
// still reaches the render on the same frame: the sync skip also compares the element's declared fields. A debug
// build reports the bypass; a write through Node::set is counted and reported nowhere.
TEST_F(LayoutTest, AWriteThatBypassesTheChangeCountIsSyncedBeforeTheRender) {
    run("p = Probe { w = 10 } root = RawWriter { p }");
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(probe("p").w(), 10.0f);
    run("root.w = 70");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(probe("p").w(), 70.0f); // written after the query's sync, rendered with the new value
    const bool reported = hasError("Probe: a declared field of the element table changed without luaui::changes()");
#ifdef NDEBUG
    EXPECT_FALSE(reported); // handled, not reported
#else
    EXPECT_TRUE(reported);
#endif
    luaui::clearErrors();
    run("root.w = 0 p.w = 10");
    m_h.renderFrame("root");
    run("root.w = 90 root.counted = true");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(probe("p").w(), 90.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ Element identity

// An element's ImGui ID (its ID scope and its layout cache key) comes from its instance's id, never reused: an
// element that takes the place of a collected one at the same table address starts fresh, with no cached size and
// none of the old one's ImGui state. (Simulated: the table keeps its address and gets a new instance.)
TEST_F(LayoutTest, AnElementAtARecycledAddressStartsFresh) {
    run("a = Counter {} root = Inspect { a }");
    m_h.renderFrames("root", 3);
    Inspect* inspect = m_h.instance<Inspect>("root");
    ASSERT_NE(inspect, nullptr);
    ASSERT_EQ(inspect->sizes.size(), 1u);
    ASSERT_TRUE(inspect->sizes[0]);
    EXPECT_FLOAT_EQ(inspect->sizes[0]->x, 30.0f);
    ASSERT_NE(m_h.instance<Counter>("a"), nullptr);
    EXPECT_EQ(m_h.instance<Counter>("a")->count(), 3);
    const ImGuiID before = inspect->ids[0];

    // The same table (same address) loses its instance; the next render gives it a new one.
    run("rawset(a, '__native', nil) collectgarbage() collectgarbage()");
    m_h.renderFrame("root");
    ASSERT_EQ(inspect->ids.size(), 1u);
    EXPECT_NE(inspect->ids[0], before);
    EXPECT_FALSE(inspect->sizes[0]);                   // no size inherited from the dead element
    EXPECT_EQ(m_h.instance<Counter>("a")->count(), 1); // nor its ImGui state
    const ImGuiID after = inspect->ids[0];
    m_h.renderFrame("root");
    EXPECT_EQ(inspect->ids[0], after);
    EXPECT_EQ(m_h.instance<Counter>("a")->count(), 2); // stable while the element lives
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ Intrinsic sizes

// An intrinsic size must be exactly what the element then renders at (or layouts would keep asking for frames):
// every built-in leaf whose size follows from its props, the font and the theme reports its own (fills: what they take
// of the available width), and stacks compose theirs from their children's. Checked at several zooms (the theme
// re-applied at each), where sizes aren't whole pixels. The last ones report none: a titled Separator (ImGui draws it
// to the window's edge), a fill height, a custom element without one, and a stack holding one.
TEST_F(LayoutTest, IntrinsicSizesAreWhatTheElementsRender) {
    // A window tall enough for all of them: ImGui doesn't lay out a child window (a Card) out of view, so it would
    // keep its old size there.
    test::HeadlessFrame h{test::HeadlessFrame::Options{.height = 30000.0f, .builtins = true}};
    h.registerType<Probe>("Probe");
    h.registerType<Measure>("Measure");
    ASSERT_EQ(h.run(R"(
        root = Measure {
            Text { "Hello" }, Text { "" }, Text { "Two\nlines", style = "headlineSmall" },
            Text { "centered", align = "center" }, Text { "at the end", align = "end", style = "titleLarge" },
            Text { "A paragraph long enough to wrap over a few lines of the window, and then some more words.",
                   wrap = true },
            Icon { "home" }, Icon { "home", size = 40 },
            Button { "OK" }, Button { "Save", icon = "save", variant = "tonal" }, Button { "Go", variant = "text" },
            Button { variant = "fab", icon = "add" }, Button { "Compose", variant = "fab", icon = "edit" },
            Button { "Wide", width = 200, height = 50 }, Button { "fill", fillWidth = true },
            Button { "minus", width = -40 }, IconButton { "menu" },
            Spacer {}, Spacer { width = 30, height = 12 }, Spacer { fill = true }, Spacer { width = -30, height = 4 },
            Link { "imlui", url = "https://example.com" },
            Checkbox { "Check" }, Checkbox {}, Switch { "Wi-Fi" }, Switch {}, RadioButton { "One" },
            RadioGroup { model = { "a", "bb", "ccc" } }, RadioGroup { model = { "a", "bb" }, horizontal = true },
            Chip { "Filter" }, Chip { "Selected", checked = true },
            Chip { "Assist", variant = "assist", icon = "event" },
            ProgressBar { value = 0.5 }, ProgressBar { fillWidth = true }, ProgressBar { width = 100 },
            CircularProgress { value = 0.3 }, CircularProgress { size = 30 },
            BusyIndicator {}, BusyIndicator { running = false, size = 20 },
            Divider {}, Divider { width = 120 }, Separator {},
            TextField { label = "Name" }, TextField { width = 200, supportingText = "Help" },
            TextField { password = true, variant = "outlined" }, TextArea { label = "Notes" }, TextArea { height = 30 },
            Slider {}, Slider { width = 150 },
            SpinBox { label = "Count" }, SpinBox { width = 150, variant = "outlined" },
            ComboBox { model = { "a", "b" } }, ComboBox { model = { "a" }, width = 100, supportingText = "Pick" },
            SegmentedButton { model = { "Day", "Week", "Month" } }, SegmentedButton { model = { "A" }, width = 90 },
            ListItem { "Item" }, ListItem { "Two", supportingText = "lines" },
            Plot { values = { 1, 2, 3 } }, Plot { values = { 1 }, width = 100, height = 40 },
            ColorPicker { color = "#ff0000", label = "Color" }, Canvas {}, Canvas { width = 100, height = 50 },
            NavigationItem { "Home", icon = "home" }, Image { width = 40, height = 30 }, Image {},
            Section { "Header" }, Section { "Open", supportingText = "two lines", expanded = true, Text { "inside" } },
            TopAppBar { title = "Title" }, TopAppBar { title = "Medium", variant = "medium", IconButton { "search" } },
            NavigationBar { NavigationItem { "A", icon = "home" }, NavigationItem { "B", icon = "star" } },
            Column { Text { "a" }, Button { "b" } },
            Column { padding = 12, spacing = 4, align = "center", Text { "x" }, Icon { "home" }, TextField {} },
            Column { width = 200, Text { "a" } }, Column { padding = 3, Checkbox { "c" } }, Column { TextField {} },
            Row { Text { "a" }, Button { "b" } }, Row { justify = "end", Text { "x" } },
            Row { justify = "spaceBetween", Chip { "a" }, Chip { "b" } }, Row { justify = "center", Icon { "home" } },
            Row { Text { "a" }, Spacer { fill = true }, Button { "b" } },
            Row { align = "top", spacing = 0, Icon { "home" }, Text { "x" } },
            Row { Text { "label" }, Text { "wraps in its share of the row, over a few lines", wrap = true } },
            Card { Text { "in a card" }, Button { "OK" } }, Card { width = 300, padding = 8, Column { Text { "a" } } },
            Card { variant = "outlined", height = 90, Text { "fixed" } },
            Column { Row { Column { Text { "deep" }, Button { "b" } }, Icon { "home" } },
                     Card { Row { Text { "x" } } } },
            Separator { "Titled" }, TextArea { height = -1 }, Probe {}, Column { Probe {} },
            Row { Text { "a" }, Probe {} },
            Card { Probe {} } })"),
              "");
    constexpr std::size_t none = 6; // the last ones
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        setZoom(scale);
        reapplyTheme();
        h.renderFrames("root", 3); // a Card's child window fits its content a frame later (ImGui)
        Measure* m = h.instance<Measure>("root");
        ASSERT_NE(m, nullptr);
        ASSERT_GT(m->results.size(), none);
        for (std::size_t i = 0; i < m->results.size(); ++i) {
            const Measure::Result& r = m->results[i];
            if (i >= m->results.size() - none) {
                EXPECT_FALSE(r.intrinsic) << scale << " " << i << " " << r.type;
                continue;
            }
            ASSERT_TRUE(r.intrinsic) << scale << " " << i << " " << r.type;
            EXPECT_FLOAT_EQ(r.intrinsic->x, r.rendered.x) << scale << " " << i << " " << r.type;
            EXPECT_FLOAT_EQ(r.intrinsic->y, r.rendered.y) << scale << " " << i << " " << r.type;
        }
    }
    setZoom(1.0f);
    reapplyTheme();
    EXPECT_TRUE(luaui::errors().empty());
}

// Wrapping text is as wide and as tall as it wraps in the width it's given, at any width; so is a Column (or a
// Card) of it.
TEST_F(LayoutTest, WrappedTextReportsTheSizeItWrapsTo) {
    run(R"(
        local words = "The quick brown fox jumps over the lazy dog, again and again, until the line runs out."
        local function measured(w)
            return Column { width = w, Measure { id = "m" .. w, Text { words, wrap = true },
                                                 Column { Text { words, wrap = true, style = "titleMedium" } },
                                                 Card { Text { words, wrap = true } } } }
        end
        root = Column { measured(60), measured(150), measured(333), measured(790) })");
    m_h.renderFrames("root", 3);
    float lastHeight = 1e9f;
    for (const int w : {60, 150, 333, 790}) {
        Measure* m = m_h.instance<Measure>("m" + std::to_string(w));
        ASSERT_NE(m, nullptr);
        ASSERT_EQ(m->results.size(), 3u);
        for (const Measure::Result& r : m->results) {
            ASSERT_TRUE(r.intrinsic) << w << " " << r.type;
            EXPECT_FLOAT_EQ(r.intrinsic->x, r.rendered.x) << w << " " << r.type;
            EXPECT_FLOAT_EQ(r.intrinsic->y, r.rendered.y) << w << " " << r.type;
        }
        EXPECT_LE(m->results[0].rendered.x, static_cast<float>(w)) << w;
        EXPECT_LT(m->results[0].rendered.y, lastHeight) << w; // wider: fewer lines
        lastHeight = m->results[0].rendered.y;
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

// Stacks of children with intrinsic sizes have one too, so nested containers are in place on their first frame:
// a stack centered in a Column, a Column at the end of a justified Row, a Column that fills its share of a Row (it
// holds a fill), a Column centered by height in a Row. Nothing renders unseen, nothing asks for another frame.
TEST_F(LayoutTest, NestedContainersArePlacedOnTheirFirstFrame) {
    run(R"(
        title = Probe { w = 40, h = 20, intrinsic = true } ok = Probe { w = 20, intrinsic = true }
        cancel = Probe { w = 30, intrinsic = true }
        ended = Probe { w = 50, intrinsic = true }
        label = Probe { w = 50, intrinsic = true } after = Probe { w = 20, intrinsic = true }
        tall = Probe { w = 10, h = 60, intrinsic = true } a = Probe { h = 10, intrinsic = true }
        b = Probe { h = 10, intrinsic = true }
        root = Column {
            Column { align = "center", Column { title, Row { spacing = 0, ok, cancel } } },
            Row { justify = "end", Column { padding = 5, ended } },
            Row { spacing = 0, label, Column { ProgressBar { fillWidth = true } }, after },
            Row { spacing = 0, tall, Column { spacing = 0, a, b } } })");
    m_h.renderFrame("root");
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    // The centered stack is as wide as its widest row: the title (40) under OK and Cancel (50).
    EXPECT_FLOAT_EQ(probe("title").pos().x, x0 + (contentWidth() - 50.0f) * 0.5f);
    EXPECT_FLOAT_EQ(probe("ok").pos().x, probe("title").pos().x);
    EXPECT_FLOAT_EQ(probe("cancel").pos().x, probe("ok").pos().x + 20.0f);
    // The padded column (5 + 50 + 5) at the row's end.
    EXPECT_FLOAT_EQ(probe("ended").pos().x, x0 + contentWidth() - 55.0f);
    // The column holding a fill takes what the probes leave.
    EXPECT_FLOAT_EQ(probe("after").pos().x, x0 + contentWidth() - 20.0f);
    // The two-probe column (10 + spacing 0 + 10) centered by height next to the 60px probe.
    EXPECT_FLOAT_EQ(probe("a").pos().y, probe("tall").pos().y + 20.0f);
    for (const char* name : {"title", "ok", "cancel", "ended", "label", "after", "tall", "a", "b"})
        EXPECT_TRUE(probe(name).seen()) << name;
    EXPECT_GT(secondsUntilFrame(), 0.0); // nothing was misplaced or unseen
    // And they stay there.
    const ImVec2 title = probe("title").pos(), ended = probe("ended").pos(), a = probe("a").pos();
    m_h.renderFrames("root", 3);
    EXPECT_FLOAT_EQ(probe("title").pos().x, title.x);
    EXPECT_FLOAT_EQ(probe("ended").pos().x, ended.x);
    EXPECT_FLOAT_EQ(probe("a").pos().y, a.y);
    EXPECT_TRUE(luaui::errors().empty());
}

// Composing sizes neither syncs an element more than once a frame (a Column in a Row in a Column: the outer stack's
// `align` asks the Row, which asks the inner Column, which asks its children, and then each renders) nor computes an
// intrinsic size more than once a frame for one available width.
TEST_F(LayoutTest, ComposedSizesSyncEachElementOncePerFrame) {
    run(R"(
        counts = {}
        local function counted(name, value)
            return function() counts[name] = (counts[name] or 0) + 1 return value end
        end
        p = Probe { w = counted("probe", 20), intrinsic = true }
        root = Column { align = "center", padding = counted("outer", 0),
                        Row { justify = counted("row", "start"),
                              Column { padding = counted("inner", 2), Text { text = counted("text", "label") }, p },
                              Icon { name = counted("icon", "home") } } })");
    m_h.renderFrames("root", 3);
    const int queries = probe("p").queries();
    run("counts = {}");
    m_h.renderFrames("root", 2);
    for (const char* name : {"outer", "row", "inner", "text", "probe", "icon"})
        check(std::string{"counts."} + name + " == 2");
    EXPECT_LE(probe("p").queries() - queries, 2 * 2); // at most two widths a frame (the outer box's, the row's)
    EXPECT_TRUE(luaui::errors().empty());
}

// A custom element without an intrinsic size makes its Column fall back to the layout cache: no intrinsic size, so
// a centered parent renders it unseen on its first frame, and it is in place on the next.
TEST_F(LayoutTest, AStackWithACustomChildFallsBackToTheCache) {
    run(R"(
        custom = Probe { w = 40 }
        root = Column { align = "center", Measure { id = "m", Column { custom, Text { "x" } } } })");
    m_h.renderFrame("root");
    Measure* m = m_h.instance<Measure>("m");
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->results.size(), 1u);
    EXPECT_FALSE(m->results[0].intrinsic);
    run("root = Column { align = 'center', Column { custom, Text { 'x' } } }");
    m_h.renderFrame("root");
    EXPECT_FALSE(probe("custom").seen()); // its first frame in this place: measured, not seen
    EXPECT_GE(renderWhileRequested("root"), 1);
    EXPECT_TRUE(probe("custom").seen());
    const float x0 = ImGui::GetStyle().WindowPadding.x;
    EXPECT_FLOAT_EQ(probe("custom").pos().x, x0 + (contentWidth() - 40.0f) * 0.5f);
    EXPECT_GT(secondsUntilFrame(), 0.0); // settled
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

// A Row that only aligns heights (the default align = "center", justify = "start", no fills) needs no widths to
// place its children: a child without a known height renders seen at the top on its first frame (the others are
// centered among the known heights) and the row asks for the frame that centers it, a shift of a few pixels at most.
TEST_F(LayoutTest, ARowShowsAChildWithAnUnknownHeightOnItsFirstFrame) {
    run(R"(
        tall = Probe { w = 20, h = 30 } short = Probe { w = 20, h = 10, intrinsic = true }
        root = Row { spacing = 0, tall, short })");
    m_h.renderFrame("root");
    EXPECT_TRUE(probe("tall").seen());
    EXPECT_TRUE(probe("short").seen());
    const float top = ImGui::GetStyle().WindowPadding.y;
    EXPECT_FLOAT_EQ(probe("tall").pos().y, top);
    EXPECT_FLOAT_EQ(probe("short").pos().y, top); // the only known height: nothing to center it in yet
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    EXPECT_GE(renderWhileRequested("root"), 1);
    EXPECT_FLOAT_EQ(probe("tall").pos().y, top);
    EXPECT_FLOAT_EQ(probe("short").pos().y, top + 10.0f);
    EXPECT_FLOAT_EQ(probe("short").pos().x, probe("tall").pos().x + 20.0f);
    EXPECT_GT(secondsUntilFrame(), 0.0);
}
