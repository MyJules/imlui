// The typed data elements (CircularProgress, BusyIndicator, Plot, Table, Canvas), through the headless frame
// harness: defaults, props reaching the instance, nil resets, the two-way Table.currentRow, the signals with
// their arguments (injected mouse clicks), and the Canvas painter.
#include <gtest/gtest.h>

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.circular_progress;
import imlui.ui.elements.busy_indicator;
import imlui.ui.elements.plot;
import imlui.ui.elements.table;
import imlui.ui.elements.canvas;
import imlui.ui.frame_request;
import imlui.test.headless_frame;
import imlui.test.last_item_probe;

using namespace imlui;

namespace {

using Probe = test::LastItemProbe;

class DataTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_h.registerType<Probe>("Probe");
    }

    void run(const std::string& code) { ASSERT_EQ(m_h.run(code), ""); }
    void frames(int n = 1) { m_h.renderFrames("root", n); }
    /// Moves the mouse to (x, y) and clicks there, a frame for each step.
    void click(float x, float y) { m_h.click("root", x, y); }
    [[nodiscard]] Probe& probe() { return *m_h.instance<Probe>("root"); }
    [[nodiscard]] ImVec2 probeSize() { return probe().size(); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// ------------------------------------------------------------------ CircularProgress / BusyIndicator

TEST_F(DataTest, CircularProgressPropsDefaultsAndSize) {
    run(R"(
        s = state { v = 0.4 }
        root = Probe { CircularProgress { id = "progress", value = function() return s.v end, size = 40 } })");
    frames();
    CircularProgress* progress = m_h.instance<CircularProgress>("progress");
    EXPECT_DOUBLE_EQ(progress->value(), 0.4);
    EXPECT_FALSE(progress->indeterminate());
    EXPECT_DOUBLE_EQ(progress->thickness(), 4.0);
    EXPECT_NEAR(probeSize().x, 40.0f, 1.0f);
    run("progress.indeterminate = true; progress.thickness = 6; s.v = nil");
    frames();
    EXPECT_TRUE(progress->indeterminate());
    EXPECT_DOUBLE_EQ(progress->thickness(), 6.0);
    EXPECT_DOUBLE_EQ(progress->value(), 0.0);
}

// A value of NaN (done / total with total == 0) or below 0 is not "indeterminate": the widgets read any negative
// fraction as indeterminate and animate (and request frames) forever.
TEST_F(DataTest, ProgressValuesAreClampedNotIndeterminate) {
    for (const char* value : {"0/0", "-0.5", "-math.huge", "math.huge", "2"}) {
        run(std::string{"root = Probe { ProgressBar { value = "} + value + " }, CircularProgress { value = " + value +
            " } }");
        frames(3);
        EXPECT_EQ(secondsUntilFrame(), std::numeric_limits<double>::infinity()) << value;
    }
    run("root = Probe { ProgressBar { indeterminate = true } }");
    frames(2);
    EXPECT_EQ(secondsUntilFrame(), 0.0); // indeterminate still animates
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(DataTest, BusyIndicatorKeepsItsSpaceWhenStopped) {
    run(R"(
        s = state { running = false }
        root = Probe { BusyIndicator { id = "busy", running = function() return s.running end } })");
    frames();
    BusyIndicator* busy = m_h.instance<BusyIndicator>("busy");
    EXPECT_FALSE(busy->running());
    EXPECT_DOUBLE_EQ(busy->thickness(), 4.0);
    EXPECT_NEAR(probeSize().x, 48.0f, 0.5f); // the 48 dp default
    EXPECT_NEAR(probeSize().y, 48.0f, 0.5f);
    run("busy.size = 30; s.running = nil");
    frames(2);
    EXPECT_TRUE(busy->running()); // nil: the default
    run("s.running = false");
    frames(2);
    EXPECT_NEAR(probeSize().x, 30.0f, 0.5f);
}

// ------------------------------------------------------------------ Plot

TEST_F(DataTest, PlotPropsAndDefaults) {
    run(R"(
        s = state { values = { 1, 2.5, 3 } }
        root = Probe { Plot { id = "plain" },
                       Plot { id = "set", values = function() return s.values end, kind = "bars", fill = false,
                              label = "Label", color = "#FF0000", max = 10, height = 120 } })");
    frames();
    Plot* plain = m_h.instance<Plot>("plain");
    EXPECT_TRUE(plain->values().empty());
    EXPECT_EQ(plain->kind(), "lines");
    EXPECT_TRUE(plain->fill());
    EXPECT_FALSE(plain->title().has_value());
    EXPECT_FALSE(plain->color().has_value());
    EXPECT_FALSE(plain->min().has_value());
    EXPECT_FALSE(plain->max().has_value());
    Plot* set = m_h.instance<Plot>("set");
    EXPECT_EQ(set->values(), (std::vector<float>{1.0f, 2.5f, 3.0f}));
    EXPECT_EQ(set->kind(), "bars");
    EXPECT_FALSE(set->fill());
    EXPECT_EQ(set->label(), "Label");
    ASSERT_TRUE(set->color().has_value());
    EXPECT_EQ(toHex(*set->color()), "#FF0000");
    EXPECT_FALSE(set->min().has_value());
    EXPECT_EQ(set->max(), std::optional<double>{10.0});
    EXPECT_NEAR(probeSize().y, 120.0f, 1.0f);
    run(R"(set.title = "Title"; s.values = { 4 })");
    frames();
    EXPECT_EQ(set->title(), std::optional<std::string>{"Title"});
    EXPECT_EQ(set->values(), (std::vector<float>{4.0f}));
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ Table

TEST_F(DataTest, TablePropsAndDefaults) {
    run(R"(root = Probe { Table { id = "plain" },
                         Table { id = "set", columns = { "A", "B" }, rowHeight = 40, outlined = false, currentRow = 2 } })");
    frames();
    Table* plain = m_h.instance<Table>("plain");
    EXPECT_TRUE(plain->columns().empty());
    EXPECT_DOUBLE_EQ(plain->rowHeight(), 52.0);
    EXPECT_TRUE(plain->outlined());
    EXPECT_EQ(plain->currentRow(), 0);
    Table* set = m_h.instance<Table>("set");
    EXPECT_EQ(set->columns(), (std::vector<std::string>{"A", "B"}));
    EXPECT_DOUBLE_EQ(set->rowHeight(), 40.0);
    EXPECT_FALSE(set->outlined());
    EXPECT_EQ(set->currentRow(), 2);
}

TEST_F(DataTest, TableRowClickWritesBackAndEmitsRowClicked) {
    run(R"(
        s = state { row = 0, rows = { { "x", 1 }, { "y", 2 }, { "z" } } }
        got = nil
        root = Probe { Table { columns = { "Name", "Value" }, rows = function() return s.rows end,
                               currentRow = bind(s, "row"), outlined = false, width = 400,
                               onRowClicked = function(_, row) got = row end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    EXPECT_NEAR(b.y - a.y, 56.0f + 3 * 52.0f, 2.0f); // header + three rows, from Lua
    click(a.x + 40.0f, a.y + 56.0f + 52.0f + 26.0f);  // the second row
    run(R"(assert(s.row == 2 and math.type(s.row) == "integer", tostring(s.row)))");
    run(R"(assert(got == 2, tostring(got)))");
}

TEST_F(DataTest, TableRowsThatArentListsRenderEmptyAndAreReported) {
    run(R"(root = Probe { Table { columns = { "A", "B" }, rows = { "a", { "x", "y" }, 3 }, outlined = false } })");
    frames(2);
    EXPECT_NEAR(probe().max().y - probe().min().y, 56.0f + 3 * 52.0f, 2.0f); // every row keeps its place
    const auto& errors = luaui::errors();
    ASSERT_EQ(errors.size(), 2u); // reported once each, not every frame
    EXPECT_NE(errors[0].find("rows[1]"), std::string::npos) << errors[0];
    EXPECT_NE(errors[1].find("rows[3]"), std::string::npos) << errors[1];
}

TEST_F(DataTest, TableColumnsAreCappedAtImGuisLimit) {
    run(R"(
        local columns, row = {}, {}
        for i = 1, 600 do columns[i] = "c" .. i; row[i] = i end
        root = Probe { Table { columns = columns, rows = { row }, outlined = false } })");
    frames(2);
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("at most 511"), std::string::npos) << luaui::errors()[0];
}

// `width` applies with or without the card.
TEST_F(DataTest, TableWidthAppliesWithoutTheCard) {
    run(R"(root = Probe { Table { columns = { "A", "B" }, rows = { { 1, 2 } }, outlined = false, width = 300 } })");
    frames(2);
    EXPECT_FLOAT_EQ(probeSize().x, 300.0f);
    run(R"(root = Probe { Table { columns = { "A", "B" }, rows = { { 1, 2 } }, outlined = false } })");
    frames(2);
    EXPECT_GT(probeSize().x, 700.0f); // unset: fills
}

// onRowClicked runs after the rows are drawn: a handler that removes rows doesn't make the rest of the frame
// read rows that are gone (reported as "rows[i] is not a list of cells").
TEST_F(DataTest, TableRowClickedHandlerMayRemoveRows) {
    run(R"(
        rows = { { "x" }, { "y" }, { "z" } }
        got = nil
        root = Probe { Table { columns = { "Name" }, rows = rows, outlined = false, width = 400,
                               onRowClicked = function(_, row) got = row; for i = #rows, 1, -1 do rows[i] = nil end end } })");
    frames(2);
    const ImVec2 a = probe().min();
    click(a.x + 40.0f, a.y + 56.0f + 26.0f); // the first row
    run(R"(assert(got == 1, tostring(got)))");
    frames(2);
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(DataTest, PlotOfLargeOrConstantValuesDraws) {
    // A float `max = min + 1` doesn't change 1e9, and the axis math took log10(0).
    run(R"(root = Probe { Plot { values = { 1e9, 1e9, 1e9 } }, Plot { values = { 5, 5 }, min = 3, max = 3 },
                         Plot { values = { 0 / 0, 1, 2 } }, Plot { values = { 3e38, -3e38 } } })");
    frames(2);
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ Canvas

TEST_F(DataTest, CanvasPaintsWithThePainterAndEmitsClickedInDp) {
    run(R"(
        painted = nil
        clicked = nil
        root = Probe { Canvas { id = "canvas", width = 200, height = 100,
            onClicked = function(_, x, y) clicked = { x, y } end,
            onPaint = function(_, p)
                painted = { p.width, p.height, p.hovered, p.time ~= nil }
                p:rect(0, 0, p.width, p.height, "primary", 8)
                p:rect(0, 0, p.width, p.height, "outlineVariant", 8, true)
                p:circle(50, 50, 10, "tertiary")
                p:circle(50, 50, 10, "tertiary", true, 2)
                p:line(0, 10, 200, 10, "onSurface", 1)
                p:line(10, 0, 10, 100, "onSurface", 2)
                p:line(0, 0, 200, 100)
                p:polyline({ 0, 0, 50, 50, 100, 0 }, "primary", 3)
                p:text(4, 4, "hi")
                p:icon(20, 20, "star")
            end } })");
    frames();
    EXPECT_FALSE(m_h.instance<Canvas>("canvas")->animated());
    EXPECT_NEAR(probeSize().x, 200.0f, 0.5f);
    EXPECT_NEAR(probeSize().y, 100.0f, 0.5f);
    run(R"(assert(painted and painted[1] == 200 and painted[2] == 100 and painted[3] == false and painted[4],
                  "onPaint"))");
    EXPECT_TRUE(luaui::errors().empty());

    const ImVec2 a = probe().min();
    click(a.x + 50.0f, a.y + 20.0f);
    run(R"(assert(clicked and clicked[1] == 50 and clicked[2] == 20, "onClicked"))");
    run(R"(assert(math.type(clicked[1]) == "integer", "whole positions are integers, like the handler's"))");
}

TEST_F(DataTest, PainterOutsideOnPaintIsAnError) {
    run(R"(
        saved = nil
        root = Probe { Canvas { onPaint = function(_, p) saved = p end } })");
    frames();
    EXPECT_NE(m_h.run("saved:rect(0, 0, 1, 1)").find("painter used outside of Canvas.onPaint"), std::string::npos);
}
