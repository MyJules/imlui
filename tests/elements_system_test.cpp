// The system elements as typed elements (Snackbar, ImGuiDemo, Timer, and Repeater / If / Loader as the root),
// through the headless frame harness. The Timer ticks whether or not it's rendered: on a hidden page, an
// unselected Loader page, or nowhere in the tree. Time is the harness's ImGui time: 1/16 s per frame here
// (exact in binary), so a 250 ms timer fires exactly every 4 frames.
#include <gtest/gtest.h>

import lua;
import imgui;
import imlui.lua.errors;
import imlui.ui.frame_request;
import imlui.ui.elements.snackbar;
import imlui.ui.elements.imgui_demo;
import imlui.ui.elements.timer;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.test.headless_frame;
import imlui.test.render_counter;

using namespace imlui;

namespace {

using Probe = test::RenderCounter; // a visual element that counts its renders

class SystemTest : public ::testing::Test {
protected:
    void SetUp() override { m_h.registerType<Probe>("Probe"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    int renders(const char* name) { return m_h.instance<Probe>(name)->renders(); }
    /// The value of a Lua expression in the script environment, as a number.
    double number(const std::string& expr) {
        EXPECT_EQ(m_h.run("_G.__result = " + expr), "");
        lua::State* state = m_h.state();
        lua::getglobal(state, "__result");
        const double v = lua::tonumber(state, -1);
        lua::pop(state);
        return v;
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.deltaTime = 0.0625f, .builtins = true}};
};

} // namespace

// ------------------------------------------------------------------ Timer

TEST_F(SystemTest, TimerFiresAtItsIntervalWhenRendered) {
    run(R"(
        n = 0
        root = Column { Timer { id = "t", interval = 250, onTriggered = function() n = n + 1 end } })");
    m_h.renderFrame("root"); // starts the timer
    EXPECT_EQ(number("n"), 0);
    m_h.renderFrames("root", 3);
    EXPECT_EQ(number("n"), 0);
    m_h.renderFrame("root"); // 4 frames = 250 ms after the start
    EXPECT_EQ(number("n"), 1);
    m_h.renderFrames("root", 8);
    EXPECT_EQ(number("n"), 3);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(SystemTest, TimerFiresOnAHiddenPage) {
    run(R"(
        n = 0
        s = state { page = 1 }
        root = Column {
            Column { visible = function() return s.page == 1 end, Probe { id = "p1" } },
            Column { visible = function() return s.page == 2 end,
                     Timer { interval = 250, onTriggered = function() n = n + 1 end } },
        })");
    m_h.renderFrames("root", 1 + 8);
    EXPECT_EQ(number("n"), 2);
    EXPECT_EQ(renders("p1"), 9);
}

TEST_F(SystemTest, TimerFiresOnAnUnselectedLoaderPageAndInAFalseIf) {
    run(R"(
        a, b = 0, 0
        s = state { page = 1 }
        pages = {
            Probe { id = "p1" },
            Column { Timer { interval = 250, onTriggered = function() a = a + 1 end } },
        }
        root = Column {
            Loader { item = function() return pages[s.page] end },
            If { condition = function() return false end,
                 Timer { interval = 500, onTriggered = function() b = b + 1 end } },
        })");
    m_h.renderFrames("root", 1 + 8);
    EXPECT_EQ(number("a"), 2);
    EXPECT_EQ(number("b"), 1);
    EXPECT_EQ(renders("p1"), 9);
}

TEST_F(SystemTest, TimerOutsideTheTreeTicks) {
    run(R"(
        n = 0
        t = Timer { interval = 250, onTriggered = function(self) n = n + 1; assert(self == t) end }
        root = Column { })");
    m_h.renderFrames("root", 1 + 12);
    EXPECT_EQ(number("n"), 3);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(SystemTest, TimerNotRenderedSeesItsPropertyChanges) {
    run(R"(
        n = 0
        s = state { running = false, interval = 1000, show = false }
        root = Column { Column { visible = function() return s.show end,
            Timer { id = "t", running = function() return s.running end, interval = function() return s.interval end,
                    onTriggered = function() n = n + 1 end } } })");
    m_h.renderFrames("root", 10);
    EXPECT_EQ(number("n"), 0); // stopped

    run("s.running = true");
    m_h.renderFrames("root", 1 + 16); // 1 s
    EXPECT_EQ(number("n"), 1);

    // A shorter interval takes effect right away, without the timer being rendered.
    run("s.interval = 250");
    m_h.renderFrames("root", 4);
    EXPECT_EQ(number("n"), 2);
    m_h.renderFrames("root", 4);
    EXPECT_EQ(number("n"), 3);

    run("s.running = false");
    m_h.renderFrames("root", 8);
    EXPECT_EQ(number("n"), 3);
    EXPECT_FALSE(m_h.instance<Timer>("t")->running());
}

TEST_F(SystemTest, TimerTriggeredOnStartAndOneShotWriteBack) {
    run(R"(
        n = 0
        s = state { running = true }
        t = Timer { interval = 250, ["repeat"] = false, triggeredOnStart = true, running = bind(s, "running"),
                    onTriggered = function() n = n + 1 end }
        root = Column { })");
    m_h.renderFrame("root");
    EXPECT_EQ(number("n"), 1); // triggeredOnStart
    m_h.renderFrames("root", 4);
    EXPECT_EQ(number("n"), 2);
    run("assert(s.running == false, 'a one-shot writes running = false back')");
    m_h.renderFrames("root", 8);
    EXPECT_EQ(number("n"), 2);

    // Restarting it: triggeredOnStart fires again, then one more trigger.
    run("s.running = true");
    m_h.renderFrame("root");
    EXPECT_EQ(number("n"), 3);
    m_h.renderFrames("root", 4);
    EXPECT_EQ(number("n"), 4);
    run("assert(s.running == false)");
}

TEST_F(SystemTest, TimerDefaultsAndNil) {
    run(R"(t = Timer { interval = 50, running = false, ["repeat"] = false, triggeredOnStart = true })");
    Timer* t = m_h.instance<Timer>("t");
    ASSERT_NE(t, nullptr);
    EXPECT_DOUBLE_EQ(t->interval(), 50.0);
    EXPECT_FALSE(t->running());
    EXPECT_FALSE(t->repeat());
    EXPECT_TRUE(t->triggeredOnStart());
    run("t.interval = nil; t.running = nil; t['repeat'] = nil; t.triggeredOnStart = nil");
    t = m_h.instance<Timer>("t");
    EXPECT_DOUBLE_EQ(t->interval(), 1000.0);
    EXPECT_TRUE(t->running());
    EXPECT_TRUE(t->repeat());
    EXPECT_FALSE(t->triggeredOnStart());
    EXPECT_TRUE(t->isTicking());
}

TEST_F(SystemTest, TimerRequestsAFrameAtItsDueTime) {
    run(R"(
        t = Timer { interval = 500 }
        root = Column { })");
    m_h.renderFrame("root");
    // The harness drops the requests of a frame at its start; the timer asked for one 500 ms from now.
    const double wait = secondsUntilFrame();
    EXPECT_GT(wait, 0.3);
    EXPECT_LE(wait, 0.5);
}

TEST_F(SystemTest, ACollectedTimerStopsTicking) {
    run(R"(
        n = 0
        t = Timer { interval = 250, onTriggered = function() n = n + 1 end }
        root = Column { })");
    m_h.renderFrame("root");
    const auto ticking = Element::tickingElements().size();
    run("t = nil; collectgarbage(); collectgarbage()");
    EXPECT_EQ(Element::tickingElements().size(), ticking - 1);
    m_h.renderFrames("root", 8);
    EXPECT_EQ(number("n"), 0);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(SystemTest, ATimerThatRemovesAnotherWhileTickingIsSafe) {
    run(R"(
        n = 0
        killer = Timer { interval = 250, triggeredOnStart = true,
                         onTriggered = function() victim = nil; collectgarbage(); collectgarbage(); n = n + 1 end }
        victim = Timer { interval = 250, triggeredOnStart = true, onTriggered = function() n = n + 100 end }
        root = Column { })");
    // The killer was created first, so it ticks first: the victim dies before its turn in the same frame.
    m_h.renderFrames("root", 1 + 4);
    EXPECT_EQ(number("n"), 2);
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ Snackbar

// The harness can't draw a visible snackbar (md::snackbar measures text with the theme fonts, which a
// headless context doesn't load), so these cover the props, the defaults and the expiry write-back.
TEST_F(SystemTest, SnackbarPropsComeFromUiSnackbarAndExpiryWritesShownAtBack) {
    run(R"(
        snack = ui.__overlays[1]
        changes = 0
        snack.onShownAtChanged = function(self, v) changes = changes + 1; assert(v == -1) end
        ui.snackbar("Saved", { action = "Undo", duration = 500, closable = true }))");
    auto* s = m_h.instance<Snackbar>("snack");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->text(), "Saved");
    EXPECT_EQ(s->actionText(), "Undo");
    EXPECT_DOUBLE_EQ(s->duration(), 500.0);
    EXPECT_TRUE(s->closable());
    EXPECT_GE(s->shownAt(), 0.0);

    // Past its duration and the 150 ms fade-out: the snackbar hides itself and writes shownAt = -1 back.
    run("snack.shownAt = -1");
    m_h.renderFrames("snack", 16); // hidden, while the clock moves to 1 s
    run("snack.shownAt = ui.time() - 0.7");
    m_h.renderFrame("snack");
    EXPECT_EQ(number("snack.shownAt"), -1.0);
    EXPECT_EQ(number("changes"), 1);
    EXPECT_DOUBLE_EQ(s->shownAt(), -1.0);
    m_h.renderFrames("snack", 2); // hidden: nothing more happens
    EXPECT_EQ(number("changes"), 1);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(SystemTest, SnackbarDefaults) {
    run("bar = Snackbar { }");
    auto* s = m_h.instance<Snackbar>("bar");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->text(), "");
    EXPECT_EQ(s->actionText(), "");
    EXPECT_DOUBLE_EQ(s->shownAt(), -1.0);
    EXPECT_DOUBLE_EQ(s->duration(), 4000.0);
    EXPECT_FALSE(s->closable());
}

// ------------------------------------------------------------------ ImGuiDemo

TEST_F(SystemTest, ImGuiDemoDefaultsAndOpen) {
    run(R"(
        s = state { open = false }
        demo = ImGuiDemo { open = bind(s, "open") }
        root = Column { demo })");
    m_h.renderFrames("root", 2);
    EXPECT_FALSE(m_h.instance<ImGuiDemo>("demo")->open());
    run("s.open = true");
    m_h.renderFrames("root", 2);
    EXPECT_TRUE(m_h.instance<ImGuiDemo>("demo")->open());
    run("plain = ImGuiDemo { }");
    EXPECT_TRUE(m_h.instance<ImGuiDemo>("plain")->open());
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ Repeater / If / Loader as the root

TEST_F(SystemTest, InlineContainersRenderTheirChildrenAsTheRoot) {
    run(R"(
        s = state { show = true, page = 1 }
        made = {}
        rep = Repeater { model = { "a", "b", "c" },
                         delegate = function(item) local p = Probe { }; made[#made + 1] = p; return p end }
        cond = If { condition = function() return s.show end, Probe { id = "yes" }, otherwise = Probe { id = "no" } }
        pages = { Probe { id = "one" }, Probe { id = "two" } }
        load = Loader { item = function() return pages[s.page] end })");
    m_h.renderFrames("rep", 2);
    run("assert(#made == 3, 'delegates: ' .. #made)");

    m_h.renderFrames("cond", 2);
    EXPECT_EQ(renders("yes"), 2);
    EXPECT_EQ(renders("no"), 0);
    run("s.show = false");
    m_h.renderFrame("cond");
    EXPECT_EQ(renders("yes"), 2);
    EXPECT_EQ(renders("no"), 1);

    m_h.renderFrames("load", 2);
    EXPECT_EQ(renders("one"), 2);
    run("s.page = 2");
    m_h.renderFrame("load");
    EXPECT_EQ(renders("one"), 2);
    EXPECT_EQ(renders("two"), 1);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(SystemTest, RepeaterDelegatesRenderAsTheRoot) {
    run(R"(
        r1 = Probe { }
        r2 = Probe { }
        list = { r1, r2 }
        rep = Repeater { model = { 1, 2 }, delegate = function(item) return list[item] end })");
    m_h.renderFrames("rep", 3);
    EXPECT_EQ(renders("r1"), 3);
    EXPECT_EQ(renders("r2"), 3);
}
