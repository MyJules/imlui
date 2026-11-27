// The Fluent ranges (group A): Slider, ProgressBar and CircularProgress under design "fluent" take Fluent's sizes
// (the tokens and the family's constants), resolve a fixed width and fill, and the slider moves like the Material one
// (clicks write the stepped value back, disabled it doesn't react).
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.lua.errors;
import imlui.skin;
import imlui.skin.fluent.common;
import imlui.skin.fluent.progress;
import imlui.skin.fluent.slider;
import imlui.theme.dpi;
import imlui.theme.theme;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.progress;
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

class FluentRangesTest : public ::testing::Test {
protected:
    FluentRangesTest() { m_h.registerType<Spot>("Spot"); }
    ~FluentRangesTest() override {
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
    /// An element's width and height as laid out: between Spots in a Row (`rowWidth` > 0: a Row that wide) and in a
    /// Column.
    ImVec2 laidOut(std::string_view element, float rowWidth = 0) {
        const std::string width = rowWidth > 0 ? std::format("width = {}, ", rowWidth) : std::string{};
        run(std::format(R"(
            a, b, c, d = Spot {{}}, Spot {{}}, Spot {{}}, Spot {{}}
            root = Column {{ spacing = 0, Row {{ {1}spacing = 0, a, {0}, b }}, c, {0}, d }})",
                        element, width));
        m_h.renderFrames("root", 2);
        return {across("a", "b"), down("c", "d")};
    }
    void clickAfter(std::string_view spot, float dx, float dy) {
        const ImVec2 at = m_h.instance<Spot>(spot)->pos();
        m_h.click("root", at.x + dx, at.y + 1.0f + dy, 2);
    }
    void check(std::string_view lua) { EXPECT_EQ(m_h.run(lua), "") << lua; }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

TEST_F(FluentRangesTest, SliderSizes) {
    const ImVec2 fixed = laidOut(R"(Slider { width = 250 })");
    EXPECT_FLOAT_EQ(fixed.x, 250.0f);
    EXPECT_FLOAT_EQ(fixed.y, dp(32));
    // Unset, it fills: the Row's width less the two Spots.
    EXPECT_FLOAT_EQ(laidOut(R"(Slider {})", 400.0f).x, 398.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(Slider { width = -1 })", 400.0f).x, 398.0f);
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 20, density = 2 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(Slider { width = 250 })").y, dp(40));
    // Never lower than the thumb.
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 10 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(Slider { width = 250 })").y, dp(fluent::sliderThumbDp));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentRangesTest, SliderClicksStepAndDisabledDoesnt) {
    run(R"(
        s = state { v = 2, off = 2 }
        moved = 0
        s1, s2 = Spot {}, Spot {})");
    run(R"(root = Column { spacing = 0,
        s1, Slider { value = bind(s, "v"), from = 0, to = 10, stepSize = 1, width = 300,
                     onMoved = function() moved = moved + 1 end },
        s2, Slider { value = bind(s, "off"), from = 0, to = 10, stepSize = 1, width = 300, enabled = false } })");
    m_h.renderFrames("root", 2);
    // The track runs between the thumb's radii (10dp), as md's.
    const float r = dp(fluent::sliderThumbDp) * 0.5f, x0 = r, x1 = 300.0f - r;
    clickAfter("s1", x0 + 0.7f * (x1 - x0), dp(16));
    clickAfter("s2", x0 + 0.7f * (x1 - x0), dp(16));
    check(R"(assert(s.v == 7 and math.type(s.v) == "integer" and moved == 1 and s.off == 2,
                    s.v .. " " .. moved .. " " .. s.off))");
    clickAfter("s1", 299.0f, dp(16));
    check(R"(assert(s.v == 10, tostring(s.v)))");
    // Clicking on the current value changes nothing.
    clickAfter("s1", x1, dp(16));
    check(R"(assert(moved == 2, tostring(moved)))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentRangesTest, ProgressSizes) {
    const ImVec2 bar = laidOut(R"(ProgressBar { value = 0.5, width = 0 })");
    EXPECT_FLOAT_EQ(bar.x, dp(240));
    EXPECT_FLOAT_EQ(bar.y, dp(fluent::progressBarDp));
    EXPECT_FLOAT_EQ(laidOut(R"(ProgressBar { value = 0.5, width = 120 })").x, 120.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(ProgressBar { indeterminate = true, width = -1 })", 300.0f).x, 298.0f);

    const ImVec2 ring = laidOut(R"(CircularProgress { value = 0.3 })");
    EXPECT_FLOAT_EQ(ring.x, dp(fluent::progressRingDp));
    EXPECT_FLOAT_EQ(ring.y, dp(fluent::progressRingDp));
    EXPECT_FLOAT_EQ(laidOut(R"(CircularProgress { indeterminate = true, size = 40, thickness = 3 })").x, dp(40));
    EXPECT_FLOAT_EQ(laidOut(R"(BusyIndicator {})").y, laidOut(R"(BusyIndicator {})").x);
    EXPECT_TRUE(luaui::errors().empty());

    // Material keeps its own sizes (md's: a 12dp bar, a 48dp ring, a 44dp slider).
    applyTheme(R"(theme { design = "material" })");
    EXPECT_FLOAT_EQ(laidOut(R"(ProgressBar { width = 0 })").y, dp(12));
    EXPECT_FLOAT_EQ(laidOut(R"(CircularProgress {})").x, md::circularProgressSize(0).x);
    EXPECT_FLOAT_EQ(laidOut(R"(Slider { width = 250 })").y, dp(44));
}
