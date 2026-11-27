// Keyboard navigation through the skins: under Material and under Fluent, Tab reaches the controls and Space or Enter
// activates them (md::navButton), and a focused Slider steps with the arrow keys, PageUp/PageDown and Home/End
// (md::sliderKeys) while Up/Down still move the focus on.
#include <gtest/gtest.h>

import std;

import imlui.lua.errors;
import imlui.theme.theme;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

class KeyboardNavTest : public ::testing::TestWithParam<const char*> {
protected:
    ~KeyboardNavTest() override {
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override {
        run(std::format(R"(theme {{ design = "{}" }})", GetParam()));
        ASSERT_TRUE(applyPendingTheme());
    }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void check(std::string_view lua) { EXPECT_EQ(m_h.run(lua), "") << lua; }
    /// Presses and releases `key`, then lets the UI react.
    void press(std::string_view key) {
        run(std::format(R"(ui.native.injectKey("{0}", true); ui.native.injectKey("{0}", false))", key));
        m_h.renderFrames("root", 4);
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

TEST_P(KeyboardNavTest, TabAndSpaceActivateControls) {
    run(R"(
        clicks, s = 0, state { on = false }
        root = Column {
            Button { "One", onClicked = function() clicks = clicks + 1 end },
            Checkbox { "Two", checked = bind(s, "on") },
            Switch { "Three" } })");
    m_h.renderFrames("root", 2);
    press("Tab"); // the first control
    press("Space");
    check(R"(assert(clicks == 1 and not s.on, clicks .. " " .. tostring(s.on)))");
    press("Tab");
    press("Space");
    check(R"(assert(clicks == 1 and s.on, clicks .. " " .. tostring(s.on)))");
    press("Enter");
    check(R"(assert(not s.on, tostring(s.on)))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_P(KeyboardNavTest, FocusedSliderStepsWithKeys) {
    run(R"(
        s, clicks = state { v = 5, w = 0.5 }, 0
        root = Column {
            Slider { value = bind(s, "v"), from = 0, to = 100, stepSize = 1, width = 300 },
            Slider { value = bind(s, "w"), from = 0, to = 1, width = 300 },
            Button { "After", onClicked = function() clicks = clicks + 1 end } })");
    m_h.renderFrames("root", 2);
    press("Tab");
    press("Right");
    check(R"(assert(s.v == 6 and math.type(s.v) == "integer", tostring(s.v)))");
    press("Left");
    press("Left");
    check(R"(assert(s.v == 4, tostring(s.v)))");
    press("PageUp"); // a tenth of the range, in whole steps
    check(R"(assert(s.v == 14, tostring(s.v)))");
    press("PageDown");
    check(R"(assert(s.v == 4, tostring(s.v)))");
    press("End");
    check(R"(assert(s.v == 100, tostring(s.v)))");
    press("Right"); // at the end: stays
    check(R"(assert(s.v == 100, tostring(s.v)))");
    press("Home");
    check(R"(assert(s.v == 0, tostring(s.v)))");
    // Space activates it but doesn't move it (no mouse drag).
    press("Space");
    check(R"(assert(s.v == 0, tostring(s.v)))");

    // Down moves the focus on: to the continuous slider, which steps by a hundredth of its range.
    press("Down");
    press("Right");
    check(R"(assert(s.v == 0 and math.abs(s.w - 0.51) < 1e-9, s.v .. " " .. s.w))");
    press("Down");
    press("Space");
    check(R"(assert(clicks == 1, tostring(clicks)))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_P(KeyboardNavTest, DisabledSliderIgnoresKeys) {
    run(R"(
        s = state { v = 5 }
        root = Column {
            Button { "Before" },
            Slider { value = bind(s, "v"), from = 0, to = 10, stepSize = 1, enabled = false } })");
    m_h.renderFrames("root", 2);
    press("Tab");
    press("Tab");
    press("Right");
    press("End");
    check(R"(assert(s.v == 5, tostring(s.v)))");
    EXPECT_TRUE(luaui::errors().empty());
}

INSTANTIATE_TEST_SUITE_P(Skins, KeyboardNavTest, ::testing::Values("material", "fluent"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string{info.param}; });
