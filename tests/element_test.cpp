// Element lifecycle and rendering, through the headless frame harness (tests/support/headless_frame.cppm):
// componentComplete() after the first sync, render() every frame, children through Frame, lazy sync before
// method calls, nil resetting ported built-ins (Timer, ProgressBar) to their defaults, and key injection
// (native.injectKey) closing a Dialog and a ModalNavigationDrawer with Escape.
#include <gtest/gtest.h>

import imgui;
import imlui.lua.errors;
import imlui.ui.elements.progress_bar;
import imlui.ui.elements.timer;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

std::vector<std::string> events;

/// A container that logs its lifecycle and renders its children through Frame.
class Panel final : public Element {
public:
    static void declare(TypeBuilder<Panel>& type) {
        type.property("title", &Panel::title, &Panel::setTitle).method("describe", &Panel::describe);
    }

    void componentComplete() override { events.push_back("complete " + m_title); }
    void render(Frame& frame) override {
        ++m_renders;
        events.push_back("render " + m_title);
        frame.renderChildren();
    }

    [[nodiscard]] const std::string& title() const { return m_title; }
    void setTitle(std::string title) {
        m_title = std::move(title);
        events.push_back("set " + m_title);
    }
    [[nodiscard]] std::string describe() const { return m_title + " after " + std::to_string(m_renders) + " frames"; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    std::string m_title = "untitled";
    int m_renders = 0;
};

/// A visual child that counts its renders.
class Dot final : public Element {
public:
    static void declare(TypeBuilder<Dot>&) {}
    void render(Frame& frame) override {
        ++m_renders;
        ImGui::Dummy({frame.dp(4), frame.dp(4)});
    }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    int m_renders = 0;
};

long countOf(const std::string& event) {
    long n = 0;
    for (const auto& e : events)
        n += e == event ? 1 : 0;
    return n;
}

class ElementTest : public ::testing::Test {
protected:
    void SetUp() override {
        events.clear();
        m_h.registerType<Panel>("Panel");
        m_h.registerType<Dot>("Dot");
    }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

TEST_F(ElementTest, ComponentCompleteRunsOnceAfterTheFirstSyncAndRenderEveryFrame) {
    run(R"(root = Panel { title = "A", Dot { id = "first" }, Panel { title = "inner", Dot { id = "second" } } })");
    EXPECT_TRUE(events.empty()) << "constructing must not sync or complete";

    m_h.renderFrame("root");
    const std::vector<std::string> first{"set A",     "complete A",     "render A",
                                         "set inner", "complete inner", "render inner"};
    EXPECT_EQ(events, first);

    m_h.renderFrames("root", 2);
    EXPECT_EQ(countOf("complete A"), 1);
    EXPECT_EQ(countOf("render A"), 3);
    EXPECT_EQ(countOf("render inner"), 3);
    EXPECT_EQ(m_h.instance<Dot>("first")->renders(), 3);
    EXPECT_EQ(m_h.instance<Dot>("second")->renders(), 3);
    EXPECT_EQ(m_h.instance<Panel>("root")->renders(), 3);

    events.clear();
    run(R"(root.title = "B")");
    m_h.renderFrame("root");
    ASSERT_GE(events.size(), 2u);
    EXPECT_EQ(events[0], "set B"); // synced before the render
    EXPECT_EQ(events[1], "render B");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ElementTest, HiddenChildrenAreNotRendered) {
    run(R"(
        s = state { show = false }
        root = Panel { Dot { id = "dot", visible = function() return s.show end } })");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(m_h.instance<Dot>("dot")->renders(), 0);
    run("s.show = true");
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.instance<Dot>("dot")->renders(), 1);
}

TEST_F(ElementTest, AnElementThatNeverRenderedIsSyncedBeforeItsFirstMethodCall) {
    run(R"(
        s = state { title = "lazy" }
        p = Panel { title = function() return s.title end }
        first = p:describe()
        s.title = "changed"
        second = p:describe()
        assert(first == "lazy after 0 frames", first)
        assert(second == "changed after 0 frames", second))");
    const std::vector<std::string> expected{"set lazy", "complete lazy", "set changed"};
    EXPECT_EQ(events, expected); // componentComplete once, after the first sync
}

// A method called from a binding reuses the element's sync of this frame (its bindings run once a frame, not once per
// call); called from a script, it syncs again, so it sees what the script changed just before.
TEST_F(ElementTest, MethodCallsFromBindingsReuseTheFramesSync) {
    run(R"(
        s = state { title = "a" }
        evals = 0
        root = Column {
            Panel { id = "p", title = function() evals = evals + 1; return s.title end },
            Text { text = function() return p:describe() .. p:describe() .. p:describe() end },
        })");
    m_h.renderFrame("root");
    run("evals = 0");
    m_h.renderFrames("root", 3);
    run("assert(evals == 3, 'the binding ran ' .. evals .. ' times in 3 frames')");
    run(R"(
        s.title = "b"
        local d = p:describe()
        assert(d:match("^b after"), d))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ElementTest, ABindingThatCallsAMethodOfItsOwnElementDoesNotRecurse) {
    run(R"(
        p = Panel { title = function(self) return "n" .. #self:describe() end }
        assert(p:describe():match("^n%d+ after 0 frames$"), p:describe()))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ElementTest, NilResetsPortedBuiltinsToTheirDefaults) {
    run(R"(
        s = state { value = 0.5, interval = 250, running = false }
        bar = ProgressBar { value = function() return s.value end }
        timer = Timer { interval = function() return s.interval end, running = function() return s.running end })");
    ProgressBar* bar = m_h.instance<ProgressBar>("bar");
    Timer* timer = m_h.instance<Timer>("timer");
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(timer, nullptr);
    EXPECT_DOUBLE_EQ(bar->value(), 0.5);
    EXPECT_DOUBLE_EQ(timer->interval(), 250.0);
    EXPECT_FALSE(timer->running());

    // A bound value that becomes nil: like the original handlers' n.number("value", 0) and
    // n.boolean("running", true), the property falls back to its default.
    run("s.value = nil; s.interval = nil; s.running = nil");
    EXPECT_DOUBLE_EQ(m_h.instance<ProgressBar>("bar")->value(), 0.0);
    EXPECT_DOUBLE_EQ(m_h.instance<Timer>("timer")->interval(), 1000.0);
    EXPECT_TRUE(m_h.instance<Timer>("timer")->running());
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ key injection

TEST_F(ElementTest, InjectKeyFeedsImGuiKeyEventsWithModifiers) {
    run(R"(root = Panel { })");
    m_h.renderFrame("root");
    run(R"(ui.native.injectKey("a", true, true, false, true))");
    m_h.renderFrame("root");
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_A));
    EXPECT_TRUE(ImGui::GetIO().KeyCtrl);
    EXPECT_FALSE(ImGui::GetIO().KeyShift);
    EXPECT_TRUE(ImGui::GetIO().KeyAlt);
    run(R"(ui.native.injectKey("A", false))");
    m_h.renderFrame("root");
    EXPECT_FALSE(ImGui::IsKeyDown(ImGuiKey_A));
    EXPECT_FALSE(ImGui::GetIO().KeyCtrl);

    for (const char* name : {"Escape", "Esc", "Enter", "Tab", "Equal", "Minus", "0", "F5", "LeftArrow", "Left"})
        EXPECT_EQ(m_h.run(std::string{"ui.native.injectKey('"} + name + "', false)"), "") << name;
    EXPECT_NE(m_h.run("ui.native.injectKey('NoSuchKey', true)").find("injectKey: unknown key 'NoSuchKey'"),
              std::string::npos);
    EXPECT_NE(m_h.run("ui.native.injectKey('MouseLeft', true)").find("unknown key"), std::string::npos);
    EXPECT_NE(m_h.run("ui.native.injectKey(27, true)").find("expects a key name"), std::string::npos);
}

TEST_F(ElementTest, EscapeClosesADialog) {
    run(R"(
        s = state { open = true }
        closed = 0
        root = Column {
            Dialog { title = "Confirm", open = bind(s, "open"), onClosed = function() closed = closed + 1 end,
                     Text { text = "Really?" } },
        })");
    m_h.renderFrames("root", 3);
    run("assert(s.open == true, 'the dialog closed by itself')");

    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 3);
    run("assert(s.open == false, 'Escape did not close the dialog'); assert(closed == 1, 'onClosed: ' .. closed)");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementTest, EscapeClosesAModalNavigationDrawer) {
    run(R"(
        s = state { open = true }
        closed = 0
        root = Column {
            ModalNavigationDrawer { open = bind(s, "open"), onClosed = function() closed = closed + 1 end,
                                    Text { text = "Destinations" } },
        })");
    m_h.renderFrames("root", 30); // the sheet slides in
    run("assert(s.open == true and closed == 0, 'the drawer closed by itself')");

    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 3);
    run("assert(s.open == false, 'Escape did not close the drawer'); assert(closed == 1, 'onClosed: ' .. closed)");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}
