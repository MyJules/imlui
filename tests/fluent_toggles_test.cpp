// The Fluent toggles and selection controls (group A): Checkbox, RadioButton, Switch, Chip, SegmentedButton and
// IconButton under design "fluent" take Fluent's sizes from the tokens (and the family's constants), resolve a fixed
// width, map the Material variants as documented, and click like the Material ones (disabled ones don't react).
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.lua.errors;
import imlui.skin;
import imlui.skin.fluent.chip;
import imlui.skin.fluent.common;
import imlui.skin.fluent.icon_button;
import imlui.skin.fluent.segmented;
import imlui.skin.fluent.toggles;
import imlui.theme.dpi;
import imlui.theme.theme;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.button;
import imlui.widgets.common;
import imlui.widgets.selection;
import imlui.test.headless_frame;

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

class FluentTogglesTest : public ::testing::Test {
protected:
    FluentTogglesTest() { m_h.registerType<Spot>("Spot"); }
    ~FluentTogglesTest() override {
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override { applyTheme(R"(theme { design = "fluent" })"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
    }
    float across(std::string_view a, std::string_view b) {
        return m_h.instance<Spot>(b)->pos().x - m_h.instance<Spot>(a)->pos().x - 1.0f;
    }
    float down(std::string_view a, std::string_view b) {
        return m_h.instance<Spot>(b)->pos().y - m_h.instance<Spot>(a)->pos().y - 1.0f;
    }
    /// An element's width and height as laid out: between Spots in a Row and in a Column.
    ImVec2 laidOut(std::string_view element) {
        run(std::format(R"(
            a, b, c, d = Spot {{}}, Spot {{}}, Spot {{}}, Spot {{}}
            root = Column {{ spacing = 0, Row {{ spacing = 0, a, {0}, b }}, c, {0}, d }})",
                        element));
        m_h.renderFrames("root", 2);
        return {across("a", "b"), down("c", "d")};
    }
    /// Renders `elements` in a Column after Spot `a`, each preceded by a Spot (`s1`, `s2`, ...).
    void column(std::string_view elements) {
        run(std::format("root = Column {{ spacing = 0, {} }}", elements));
        m_h.renderFrames("root", 2);
    }
    /// Clicks `dx` pixels right of Spot `spot`, `dy` below it (from the Spot's own pixel).
    void clickAfter(std::string_view spot, float dx, float dy) {
        const ImVec2 at = m_h.instance<Spot>(spot)->pos();
        m_h.click("root", at.x + dx, at.y + 1.0f + dy, 2);
    }
    void check(std::string_view lua) { EXPECT_EQ(m_h.run(lua), "") << lua; }
    [[nodiscard]] static fluent::Look look() {
        const auto* f = dynamic_cast<const FluentSkin*>(&skin());
        EXPECT_NE(f, nullptr);
        return f->look();
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

float textW(std::string_view s) { return md::measure(fluent::bodyStyle, s).x; }

} // namespace

TEST_F(FluentTogglesTest, MarkSizesFollowTheTokens) {
    // The mark, the gap, the label; controlHeight high.
    const ImVec2 cb = laidOut(R"(Checkbox { text = "Go" })");
    EXPECT_FLOAT_EQ(cb.y, dp(32));
    EXPECT_FLOAT_EQ(cb.x, dp(fluent::markDp) + dp(8) + textW("Go"));
    EXPECT_FLOAT_EQ(laidOut(R"(Checkbox {})").x, dp(fluent::markDp)); // no label: no gap
    const ImVec2 rb = laidOut(R"(RadioButton { text = "Go" })");
    EXPECT_FLOAT_EQ(rb.x, cb.x);
    EXPECT_FLOAT_EQ(rb.y, dp(32));
    const ImVec2 sw = laidOut(R"(Switch { text = "Go" })");
    EXPECT_FLOAT_EQ(sw.x, dp(fluent::switchTrackWidthDp) + dp(8) + textW("Go"));
    EXPECT_FLOAT_EQ(sw.y, dp(32));

    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(Checkbox { text = "Go" })").y, dp(40));
    EXPECT_FLOAT_EQ(laidOut(R"(Switch { text = "Go" })").y, dp(40));

    // Density scales the height and the gap, not the mark.
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 30, density = 2 } })");
    const ImVec2 dense = laidOut(R"(RadioButton { text = "Go" })");
    EXPECT_FLOAT_EQ(dense.y, dp(60));
    EXPECT_FLOAT_EQ(dense.x, dp(fluent::markDp) + dp(16) + textW("Go"));
    // The height is at least the mark.
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 10 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(Checkbox {})").y, dp(fluent::markDp));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentTogglesTest, MarksToggleOnClicksAndNotWhenDisabled) {
    run(R"(
        s = state { cb = false, off = false, sw = false, swOff = true, r1 = false }
        s1, s2, s3, s4, s5 = Spot {}, Spot {}, Spot {}, Spot {}, Spot {})");
    column(R"(
        s1, Checkbox { text = "A", checked = bind(s, "cb") },
        s2, Checkbox { text = "B", checked = bind(s, "off"), enabled = false },
        s3, Switch { text = "C", checked = bind(s, "sw") },
        s4, Switch { text = "D", checked = bind(s, "swOff"), enabled = false },
        s5, RadioButton { text = "E", checked = bind(s, "r1") })");
    const float h = dp(32);
    for (const char* spot : {"s1", "s2", "s3", "s4", "s5"})
        clickAfter(spot, dp(10), h * 0.5f);
    check(R"(assert(s.cb == true and s.off == false, tostring(s.cb) .. " " .. tostring(s.off)))");
    check(R"(assert(s.sw == true and s.swOff == true, tostring(s.sw) .. " " .. tostring(s.swOff)))");
    check(R"(assert(s.r1 == true, tostring(s.r1)))");
    // Clicking the label toggles too: the whole item is the hit area.
    clickAfter("s1", dp(fluent::markDp) + dp(8) + 2.0f, h * 0.5f);
    check(R"(assert(s.cb == false, tostring(s.cb)))");
    EXPECT_TRUE(luaui::errors().empty());
}

// filter, suggestion, input -> accent when selected, standard otherwise; assist -> standard.
TEST_F(FluentTogglesTest, ChipsAreToggleButtons) {
    using fluent::Fill, md::ChipVariant;
    for (const ChipVariant v : {ChipVariant::Filter, ChipVariant::Suggestion, ChipVariant::Input}) {
        EXPECT_EQ(fluent::chipFill(v, true), Fill::Accent);
        EXPECT_EQ(fluent::chipFill(v, false), Fill::Standard);
    }
    EXPECT_EQ(fluent::chipFill(ChipVariant::Assist, true), Fill::Standard);
    EXPECT_EQ(fluent::chipFill(ChipVariant::Assist, false), Fill::Standard);

    // paddingX, the label, paddingX; the selection doesn't change the size (no check); an icon adds itself + gap.
    const ImVec2 tag = laidOut(R"(Chip { "Tag" })");
    EXPECT_FLOAT_EQ(tag.y, dp(32));
    EXPECT_FLOAT_EQ(tag.x, textW("Tag") + 2.0f * dp(12));
    EXPECT_FLOAT_EQ(laidOut(R"(Chip { "Tag", checked = true })").x, tag.x);
    EXPECT_FLOAT_EQ(laidOut(R"(Chip { "Tag", icon = "add" })").x, tag.x + dp(16) + dp(8));
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 36, paddingX = 4 } })");
    const ImVec2 small = laidOut(R"(Chip { "Tag", variant = "assist" })");
    EXPECT_FLOAT_EQ(small.y, dp(36));
    EXPECT_FLOAT_EQ(small.x, textW("Tag") + 2.0f * dp(4));

    // A filter chip toggles; disabled it doesn't.
    applyTheme(R"(theme { design = "fluent" })");
    run(R"(s = state { on = false, off = false }; s1, s2 = Spot {}, Spot {})");
    column(
        R"(s1, Chip { "One", checked = bind(s, "on") }, s2, Chip { "Two", checked = bind(s, "off"), enabled = false })");
    clickAfter("s1", 4.0f, dp(16));
    clickAfter("s2", 4.0f, dp(16));
    check(R"(assert(s.on == true and s.off == false, tostring(s.on) .. " " .. tostring(s.off)))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentTogglesTest, SegmentedSizesAndSelection) {
    const ImVec2 seg = laidOut(R"(SegmentedButton { model = { "Day", "Week", "Month" } })");
    const float widest = std::max({textW("Day"), textW("Week"), textW("Month")});
    EXPECT_FLOAT_EQ(seg.y, dp(32));
    EXPECT_NEAR(seg.x, 3.0f * (widest + dp(16) + dp(8) + 2.0f * dp(12)) + 2.0f * dp(fluent::segmentedInsetDp), 0.01f);
    EXPECT_FLOAT_EQ(laidOut(R"(SegmentedButton { model = { "A", "B" }, width = 300 })").x, 300.0f);
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 44 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(SegmentedButton { model = { "A", "B" } })").y, dp(44));

    // A click on the third segment selects it (1-based, two-way) and reports it; disabled nothing changes.
    applyTheme(R"(theme { design = "fluent" })");
    run(R"(
        s = state { i = 1, j = 1 }
        picked = nil
        s1, s2 = Spot {}, Spot {})");
    column(R"(
        s1, SegmentedButton { model = { "A", "B", "C" }, width = 300, currentIndex = bind(s, "i"),
                              onActivated = function(_, i, text) picked = i .. text end },
        s2, SegmentedButton { model = { "A", "B", "C" }, width = 300, currentIndex = bind(s, "j"), enabled = false })");
    clickAfter("s1", 250.0f, dp(16));
    clickAfter("s2", 250.0f, dp(16));
    check(R"(assert(s.i == 3 and picked == "3C" and s.j == 1, s.i .. " " .. tostring(picked) .. " " .. s.j))");
    // Clicking the selected segment again changes nothing.
    run("picked = nil");
    clickAfter("s1", 250.0f, dp(16));
    check(R"(assert(s.i == 3 and picked == nil, s.i .. " " .. tostring(picked)))");
    EXPECT_TRUE(luaui::errors().empty());
}

// standard -> subtle; filled -> accent (an unselected toggle: standard); tonal, outlined -> standard (a selected
// toggle: accent). A controlHeight square.
TEST_F(FluentTogglesTest, IconButtonVariantsAndSize) {
    using fluent::Fill, md::IconButtonVariant;
    EXPECT_EQ(fluent::iconButtonFill(IconButtonVariant::Standard, false, false), Fill::Subtle);
    EXPECT_EQ(fluent::iconButtonFill(IconButtonVariant::Standard, true, true), Fill::Subtle);
    EXPECT_EQ(fluent::iconButtonFill(IconButtonVariant::Filled, false, false), Fill::Accent);
    EXPECT_EQ(fluent::iconButtonFill(IconButtonVariant::Filled, true, true), Fill::Accent);
    EXPECT_EQ(fluent::iconButtonFill(IconButtonVariant::Filled, true, false), Fill::Standard);
    for (const IconButtonVariant v : {IconButtonVariant::Tonal, IconButtonVariant::Outlined}) {
        EXPECT_EQ(fluent::iconButtonFill(v, false, false), Fill::Standard);
        EXPECT_EQ(fluent::iconButtonFill(v, true, false), Fill::Standard);
        EXPECT_EQ(fluent::iconButtonFill(v, true, true), Fill::Accent);
    }

    const ImVec2 ib = laidOut(R"(IconButton { "menu" })");
    EXPECT_FLOAT_EQ(ib.x, dp(32));
    EXPECT_FLOAT_EQ(ib.y, dp(32));
    for (const char* variant : {"filled", "tonal", "outlined"})
        EXPECT_FLOAT_EQ(laidOut(std::format(R"(IconButton {{ "menu", variant = "{}" }})", variant)).x, dp(32));
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 20, density = 2 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(IconButton { "menu" })").y, dp(40));
    EXPECT_FLOAT_EQ(fluent::iconButtonSize(look()).x, dp(40));

    // A toggle flips `checked`; clicks are reported; disabled nothing happens.
    applyTheme(R"(theme { design = "fluent" })");
    run(R"(s = state { on = false }; clicks = 0; s1, s2 = Spot {}, Spot {})");
    column(R"(
        s1, IconButton { "star", checked = bind(s, "on"), onClicked = function() clicks = clicks + 1 end },
        s2, IconButton { "star", enabled = false, onClicked = function() clicks = clicks + 10 end })");
    clickAfter("s1", dp(16), dp(16));
    clickAfter("s2", dp(16), dp(16));
    check(R"(assert(s.on == true and clicks == 1, tostring(s.on) .. " " .. clicks))");
    EXPECT_TRUE(luaui::errors().empty());
}

// Material keeps its own sizes: the Fluent functions only draw under design "fluent".
TEST_F(FluentTogglesTest, MaterialKeepsItsSizes) {
    applyTheme(R"(theme { design = "material" })");
    EXPECT_FLOAT_EQ(laidOut(R"(Checkbox { text = "Go" })").y, md::checkboxSize("Go").y);
    EXPECT_FLOAT_EQ(laidOut(R"(IconButton { "menu" })").x, md::iconButtonSize().x);
    EXPECT_FLOAT_EQ(laidOut(R"(Chip { "Tag" })").x, md::chipSize("Tag", false).x);
}
