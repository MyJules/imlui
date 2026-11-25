// The window elements as typed elements (ApplicationWindow, Window, Dialog), through the headless frame
// harness: props reach the instances, the children and the `menuBar` render, two-way `open` with onClosed,
// the window title, the defaults, and the width of auto-sized windows (stable with a fill inside, shrinking back).
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.ui.renderer;
import imlui.ui.elements.application_window;
import imlui.ui.elements.window;
import imlui.ui.elements.dialog;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.test.headless_frame;
import imlui.test.render_counter;
import imlui.test.last_item_probe;

using namespace imlui;

namespace {

using Probe = test::RenderCounter; // a visual element that counts its renders

/// A visual element 20px wide unless its `width` says otherwise (a fill); records its width and the width of the
/// ImGui window it renders in.
class WidthProbe final : public Element {
public:
    static void declare(TypeBuilder<WidthProbe>&) {}

    void render(Frame& frame) override {
        m_windowWidth = ImGui::GetWindowWidth();
        m_width = frame.resolvedWidth(frame.dp(20));
        ImGui::Dummy({m_width, frame.dp(10)});
    }

    [[nodiscard]] float windowWidth() const { return m_windowWidth; }
    [[nodiscard]] float width() const { return m_width; }

private:
    float m_windowWidth = 0.0f;
    float m_width = 0.0f;
};

/// Wider than an auto-resized Window's fill and a Dialog's minimum, narrower than a Dialog's maximum.
constexpr std::string_view longLabel = "A label long enough to widen a dialog past its minimum width";

class WindowsTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_h.registerType<Probe>("Probe");
        m_h.registerType<WidthProbe>("WidthProbe");
        m_h.registerType<test::LastItemProbe>("Last");
    }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    int renders(const char* name) { return m_h.instance<Probe>(name)->renders(); }
    /// A press and a release at (x, y), over two frames, then a frame to react.
    void click(const char* root, float x, float y) { m_h.click(root, x, y, 2); }

    /// Renders `frames` frames and returns the width of the window WidthProbe `probe` rendered in, per frame.
    std::vector<float> windowWidths(const char* probe, int frames) {
        std::vector<float> widths;
        for (int i = 0; i < frames; ++i) {
            m_h.renderFrame("root");
            widths.push_back(m_h.instance<WidthProbe>(probe)->windowWidth());
        }
        return widths;
    }
    /// The width the window settles at: the same on the last 10 of 14 frames (an auto-resized window follows the
    /// last frame's content, a popup's first frame is laid out hidden, a dialog replacing another waits a frame).
    float settledWidth(const char* probe, std::string_view what) {
        const std::vector<float> widths = windowWidths(probe, 14);
        for (std::size_t i = 5; i < widths.size(); ++i)
            EXPECT_EQ(widths[i], widths[4]) << what << ", frame " << i;
        return widths.back();
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// ------------------------------------------------------------------ ApplicationWindow

TEST_F(WindowsTest, ApplicationWindowDefaults) {
    run("root = ApplicationWindow { }");
    auto* w = m_h.instance<ApplicationWindow>("root");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->title(), "imlui");
    EXPECT_FALSE(w->color().has_value());
    EXPECT_FLOAT_EQ(w->padding(), 24.0f);
    EXPECT_TRUE(w->scrollable());
}

TEST_F(WindowsTest, ApplicationWindowPropsReachTheInstanceAndChildrenRender) {
    run(R"(
        s = state { pad = 8 }
        root = ApplicationWindow { title = "App", color = "#FF0000", padding = function() return s.pad end,
                                   scrollable = false, Probe { id = "a" }, Probe { id = "b" } })");
    m_h.renderFrames("root", 2);
    auto* w = m_h.instance<ApplicationWindow>("root");
    EXPECT_EQ(w->title(), "App");
    ASSERT_TRUE(w->color().has_value());
    EXPECT_EQ(*w->color(), (Color{1.0f, 0.0f, 0.0f, 1.0f}));
    EXPECT_FLOAT_EQ(w->padding(), 8.0f);
    EXPECT_FALSE(w->scrollable());
    EXPECT_EQ(renders("a"), 2);
    EXPECT_EQ(renders("b"), 2);
    run("s.pad = 30");
    m_h.renderFrame("root");
    EXPECT_FLOAT_EQ(w->padding(), 30.0f);

    // nil resets to the defaults.
    run("root.color = nil; root.padding = nil; root.scrollable = nil");
    m_h.renderFrame("root");
    EXPECT_FALSE(w->color().has_value());
    EXPECT_FLOAT_EQ(w->padding(), 24.0f);
    EXPECT_TRUE(w->scrollable());
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(WindowsTest, ApplicationWindowSetsTheWindowTitleOnlyWithATitleProp) {
    m_h.renderer().setWindowTitle("from setTitle");
    run("root = ApplicationWindow { }");
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.renderer().windowTitle(), "from setTitle"); // no title prop: ui.setTitle() sticks

    run(R"(
        s = state { title = "First" }
        root = ApplicationWindow { title = function() return s.title end })");
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.renderer().windowTitle(), "First");
    run(R"(s.title = "Second")");
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.renderer().windowTitle(), "Second");
    run("s.title = nil"); // the prop exists but is nil: the default, like n.string("title", "imlui")
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.renderer().windowTitle(), "imlui");
}

TEST_F(WindowsTest, ApplicationWindowRendersItsMenuBar) {
    run(R"(
        s = state { show = true }
        root = ApplicationWindow { menuBar = Probe { id = "menu", visible = function() return s.show end },
                                   Probe { id = "content" } })");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(renders("menu"), 2);
    EXPECT_EQ(renders("content"), 2);
    run("s.show = false");
    m_h.renderFrame("root");
    EXPECT_EQ(renders("menu"), 2); // a hidden menu bar isn't rendered
    EXPECT_EQ(renders("content"), 3);
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ Window

TEST_F(WindowsTest, WindowDefaults) {
    run("root = Window { }");
    auto* w = m_h.instance<Window>("root");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->open());
    EXPECT_EQ(w->title(), "Window");
    EXPECT_FLOAT_EQ(w->x(), 60.0f);
    EXPECT_FLOAT_EQ(w->y(), 60.0f);
    EXPECT_FLOAT_EQ(w->width(), 0.0f);
    EXPECT_FLOAT_EQ(w->height(), 0.0f);
    EXPECT_FALSE(w->autoResize().has_value());
    EXPECT_TRUE(w->titleBar());
    EXPECT_TRUE(w->resizable());
    EXPECT_TRUE(w->movable());
}

TEST_F(WindowsTest, WindowPropsReachTheInstance) {
    run(R"(root = Window { title = "Tools", x = 10, y = 20, width = 300, height = 200, autoResize = false,
                           titleBar = false, resizable = false, movable = false, Probe { id = "p" } })");
    m_h.renderFrames("root", 2);
    auto* w = m_h.instance<Window>("root");
    EXPECT_EQ(w->title(), "Tools");
    EXPECT_FLOAT_EQ(w->x(), 10.0f);
    EXPECT_FLOAT_EQ(w->y(), 20.0f);
    EXPECT_FLOAT_EQ(w->width(), 300.0f);
    EXPECT_FLOAT_EQ(w->height(), 200.0f);
    ASSERT_TRUE(w->autoResize().has_value());
    EXPECT_FALSE(*w->autoResize());
    EXPECT_FALSE(w->titleBar());
    EXPECT_FALSE(w->resizable());
    EXPECT_FALSE(w->movable());
    EXPECT_EQ(renders("p"), 2);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(WindowsTest, AClosedWindowRendersNothing) {
    run(R"(
        s = state { open = true }
        root = Column { Window { open = bind(s, "open"), menuBar = Probe { id = "menu" }, Probe { id = "p" } } })");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(renders("p"), 2);
    EXPECT_EQ(renders("menu"), 2);
    run("s.open = false");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(renders("p"), 2);
    EXPECT_EQ(renders("menu"), 2);
}

TEST_F(WindowsTest, TheCloseButtonWritesOpenBackAndEmitsOnClosed) {
    // 200 x 100 at (60, 60) (dp = px in the harness): the close button sits at the right end of the title bar.
    run(R"(
        s = state { open = true }
        closed = 0
        root = Column { Window { title = "W", width = 200, height = 100, open = bind(s, "open"),
                                 onClosed = function() closed = closed + 1 end, Probe { id = "p" } } })");
    m_h.renderFrames("root", 2);
    const float fontSize = ImGui::GetFontSize();
    const float barHeight = fontSize + ImGui::GetStyle().FramePadding.y * 2.0f;
    click("root", 60.0f + 200.0f - ImGui::GetStyle().FramePadding.x - fontSize * 0.5f, 60.0f + barHeight * 0.5f);
    run("assert(s.open == false, 'the close button did not write open = false')");
    run("assert(closed == 1, 'onClosed: ' .. closed)");
    const int before = renders("p");
    m_h.renderFrame("root");
    EXPECT_EQ(renders("p"), before);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(WindowsTest, AnAutoResizedWindowKeepsItsWidthWithAFillAndShrinksBack) {
    // A fill used to take the window's width, so the window kept the widest width it ever had.
    run(R"(
        s = state { label = "Short" }
        root = Column { Window { title = "W", WidthProbe { id = "fill", width = -1 },
                                 Text { text = function() return s.label end } } })");
    const float narrow = settledWidth("fill", "short label");
    const float pad = ImGui::GetStyle().WindowPadding.x;
    EXPECT_FLOAT_EQ(m_h.instance<WidthProbe>("fill")->width(), Window::defaultFillWidth); // dp = px here
    EXPECT_FLOAT_EQ(narrow, Window::defaultFillWidth + 2.0f * pad);

    run(std::format(R"(s.label = "{}")", longLabel));
    const float wide = settledWidth("fill", "long label");
    EXPECT_GT(wide, narrow + 10.0f);
    EXPECT_FLOAT_EQ(m_h.instance<WidthProbe>("fill")->width(), Window::defaultFillWidth); // not the window's width

    run(R"(s.label = "Short")");
    EXPECT_FLOAT_EQ(settledWidth("fill", "short again"), narrow);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(WindowsTest, AnAutoResizedWindowsWidthIsItsMinimumAndItsFill) {
    run(R"(root = Column { Window { title = "W", width = 300, autoResize = true,
                                    WidthProbe { id = "fill", width = -1 } } })");
    EXPECT_FLOAT_EQ(settledWidth("fill", "width 300"), 300.0f);
    EXPECT_FLOAT_EQ(m_h.instance<WidthProbe>("fill")->width(), 300.0f - 2.0f * ImGui::GetStyle().WindowPadding.x);
}

// ------------------------------------------------------------------ Dialog

TEST_F(WindowsTest, ADialogKeepsItsWidthWithAFillAndShrinksBack) {
    run(R"(
        s = state { label = "Short" }
        root = Column { Dialog { open = true, title = "T", WidthProbe { id = "fill", width = -1 },
                                 Text { text = function() return s.label end } } })");
    EXPECT_FLOAT_EQ(settledWidth("fill", "short label"), 280.0f); // the minimum (dp = px here)
    EXPECT_FLOAT_EQ(m_h.instance<WidthProbe>("fill")->width(), 280.0f - 48.0f);

    run(std::format(R"(s.label = "{}")", longLabel));
    const float wide = settledWidth("fill", "long label");
    EXPECT_GT(wide, 290.0f);
    EXPECT_LT(wide, 560.0f);
    EXPECT_FLOAT_EQ(m_h.instance<WidthProbe>("fill")->width(), 280.0f - 48.0f);

    run(R"(s.label = "Short")");
    EXPECT_FLOAT_EQ(settledWidth("fill", "short again"), 280.0f);

    // Wrapping text wraps at the maximum width (its longest line may be a word shorter).
    run(R"(s.label = string.rep("wrapping words ", 40))");
    run("root = nil; collectgarbage()"); // frees the id `fill` (the first element registered keeps an id)
    run(R"(root = Column { Dialog { open = true, title = "T", WidthProbe { id = "fill", width = -1 },
                                    Text { wrap = true, text = function() return s.label end } } })");
    const float wrapped = settledWidth("fill", "wrapping text");
    EXPECT_LE(wrapped, 560.0f);
    EXPECT_GT(wrapped, 480.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(WindowsTest, ADialogsFillIsAtLeastItsTitle) {
    run(R"(root = Column { Dialog { open = true, title = "A title wider than the minimum",
                                    WidthProbe { id = "fill", width = -1 } } })");
    const float w = settledWidth("fill", "long title");
    EXPECT_GT(w, 280.0f);
    // The fill is as wide as the title, which is what the dialog is as wide as.
    EXPECT_NEAR(m_h.instance<WidthProbe>("fill")->width(), w - 48.0f, 1.0f); // ImGui truncates window sizes
}

TEST_F(WindowsTest, DialogDefaults) {
    run("root = Dialog { }");
    auto* d = m_h.instance<Dialog>("root");
    ASSERT_NE(d, nullptr);
    EXPECT_FALSE(d->open());
    EXPECT_EQ(d->title(), "");
    EXPECT_EQ(d->icon(), "");
}

TEST_F(WindowsTest, DialogShowsItsChildrenWhileOpen) {
    run(R"(
        s = state { open = false }
        closed = 0
        root = Column { Dialog { id = "dlg", title = "Delete?", icon = "delete", width = 320,
                                 open = bind(s, "open"), onClosed = function() closed = closed + 1 end,
                                 Probe { id = "p" } } })");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(renders("p"), 0);
    auto* d = m_h.instance<Dialog>("dlg");
    EXPECT_EQ(d->title(), "Delete?");
    EXPECT_EQ(d->icon(), "delete");

    run("s.open = true");
    m_h.renderFrames("root", 3);
    EXPECT_TRUE(d->open());
    EXPECT_GE(renders("p"), 2);

    // Closing from Lua is not a close by the dialog: no onClosed.
    run("s.open = false");
    m_h.renderFrames("root", 2);
    const int shown = renders("p");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(renders("p"), shown);
    run("assert(closed == 0, 'onClosed: ' .. closed)");

    // Escape closes it: open = false is written back first, then onClosed runs.
    run(R"(
        s.open = true
        seen = nil
        dlg.onClosed = function(self) closed = closed + 1; seen = s.open end)");
    m_h.renderFrames("root", 3);
    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 3);
    run("assert(s.open == false and closed == 1 and seen == false, 'closed ' .. closed .. ' ' .. tostring(seen))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(WindowsTest, DialogOnOpenChangedFollowsTheWriteBack) {
    run(R"(
        changes = {}
        root = Column { Dialog { open = true, onOpenChanged = function(self, v) changes[#changes + 1] = v end } })");
    m_h.renderFrames("root", 2);
    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 3);
    run("assert(#changes == 1 and changes[1] == false, 'changes: ' .. #changes)");
}

// ------------------------------------------------------------------ distinct ImGui windows

TEST_F(WindowsTest, TwoWindowsAreSeparateImGuiWindows) {
    // Top-level ImGui windows are named without the ID stack: each Window element needs a name of its own, even
    // with the same title.
    run(R"(
        s = state { title = "Same" }
        root = Column { Window { title = function() return s.title end, Probe { id = "a" } },
                        Window { title = "Same", Probe { id = "b" } } })");
    m_h.renderFrames("root", 2);
    auto* a = m_h.instance<Probe>("a");
    auto* b = m_h.instance<Probe>("b");
    EXPECT_EQ(a->renders(), 2);
    EXPECT_EQ(b->renders(), 2);
    ASSERT_NE(a->drawList(), nullptr);
    EXPECT_NE(a->drawList(), b->drawList());

    // A new title keeps the same ImGui window (and so its position and size).
    const ImDrawList* before = a->drawList();
    run(R"(s.title = "Renamed")");
    m_h.renderFrame("root");
    EXPECT_EQ(a->drawList(), before);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(WindowsTest, ANestedDialogIsItsOwnPopupAndEscapeClosesTheTopOne) {
    run(R"(
        s = state { outer = true, inner = false }
        root = Column { Dialog { title = "Outer", open = bind(s, "outer"), Probe { id = "a" },
                                 Dialog { title = "Inner", open = bind(s, "inner"), Probe { id = "b" } } } })");
    m_h.renderFrames("root", 3);
    EXPECT_GE(renders("a"), 2);
    EXPECT_EQ(renders("b"), 0);

    run("s.inner = true"); // opened from inside the outer dialog
    m_h.renderFrames("root", 3);
    EXPECT_GE(renders("b"), 2);
    EXPECT_NE(m_h.instance<Probe>("a")->drawList(), m_h.instance<Probe>("b")->drawList());

    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 3);
    run("assert(s.inner == false and s.outer == true, tostring(s.inner) .. ' ' .. tostring(s.outer))");
    const int outerRenders = renders("a");
    m_h.renderFrame("root");
    EXPECT_EQ(renders("a"), outerRenders + 1); // the outer dialog is still shown

    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 3);
    run("assert(s.outer == false, 'the outer dialog is still open')");
    EXPECT_TRUE(luaui::errors().empty());
}

// ImGui's keyboard nav closes an open menu on Escape in NewFrame, before the Dialog or drawer under it looks at the
// key: they must not take the same press as their own (one Escape, one popup).
TEST_F(WindowsTest, EscapeClosesAMenuButNotTheDialogOrDrawerUnderIt) {
    for (const std::string container : {"Dialog { title = 'D', ", "ModalNavigationDrawer { "}) {
        // The previous tree is collected first: its ids would stay with it.
        run("root = nil; collectgarbage(); s = state { open = true }; root = Column { " + container +
            R"(open = bind(s, "open"),
                Last { id = "last", Button { "More", menu = Menu { MenuItem { "One" }, Probe { id = "m" } } } } } })");
        m_h.renderFrames("root", 30); // the dialog settles in the middle, the drawer slides in
        const test::LastItemProbe* last = m_h.instance<test::LastItemProbe>("last");
        ASSERT_NE(last, nullptr) << container;
        click("root", last->center().x, last->center().y);
        m_h.renderFrames("root", 4);
        ASSERT_GE(renders("m"), 2) << container; // the menu is open

        run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
        m_h.renderFrames("root", 3);
        EXPECT_EQ(m_h.run("assert(s.open == true, 'closed too')"), "") << container;
        const int afterEscape = renders("m");
        m_h.renderFrames("root", 2);
        EXPECT_EQ(renders("m"), afterEscape) << container; // the menu is closed

        run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
        m_h.renderFrames("root", 3);
        EXPECT_EQ(m_h.run("assert(s.open == false, 'still open')"), "") << container;
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// ImGui keeps one popup per level: two sibling dialogs open at once would close each other every frame. The second
// waits until the first closes, then opens (a frame later, so the Escape that closed the first doesn't close it).
TEST_F(WindowsTest, SiblingDialogsOpenOneAtATime) {
    run(R"(
        s = state { a = true, b = false }
        root = Column { Dialog { title = "A", open = bind(s, "a"), Probe { id = "a" } },
                        Dialog { title = "B", open = bind(s, "b"), Probe { id = "b" } } })");
    m_h.renderFrames("root", 2);
    run("s.b = true");
    m_h.renderFrames("root", 4);
    EXPECT_EQ(renders("a"), 6); // A stays up, every frame
    EXPECT_EQ(renders("b"), 0); // B waits
    const ImDrawList* aList = m_h.instance<Probe>("a")->drawList();
    m_h.renderFrames("root", 2);
    EXPECT_EQ(m_h.instance<Probe>("a")->drawList(), aList);
    run("assert(s.a == true and s.b == true, tostring(s.a) .. ' ' .. tostring(s.b))");

    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 4);
    run("assert(s.a == false and s.b == true, tostring(s.a) .. ' ' .. tostring(s.b))"); // one Escape, one dialog
    EXPECT_GE(renders("b"), 2);

    // The same with the waiting dialog first in the tree (once the first tree is collected, freeing its ids).
    run(R"(
        root = nil; collectgarbage()
        s = state { a = false, b = true }
        root = Column { Dialog { title = "A", open = bind(s, "a"), Probe { id = "a" } },
                        Dialog { title = "B", open = bind(s, "b"), Probe { id = "b" } } })");
    m_h.renderFrames("root", 4);
    run("s.a = true");
    m_h.renderFrames("root", 3);
    EXPECT_EQ(renders("a"), 0);
    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 4);
    run("assert(s.a == true and s.b == false, tostring(s.a) .. ' ' .. tostring(s.b))");
    EXPECT_GE(renders("a"), 2);
    EXPECT_TRUE(luaui::errors().empty());
}
