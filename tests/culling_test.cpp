// Culling (imlui.ui.culling): stacks and Grid skip the children out of view and lay out only their cached sizes.
// Culled children don't run their bindings; positions and the content height are the same with culling on and off;
// scrolling brings culled children back in place on the first frame; a Timer, a Dialog, a focused TextField and
// nested ScrollViews keep working; scrolling a settled list asks for no frames; `cull = false` and ui.setCulling turn
// it off; a zoom measures the culled children again; old pins are dropped. Renders headless ImGui frames
// (tests/support/headless_frame.cppm).
#include <gtest/gtest.h>

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.test.headless_frame;
import imlui.theme.dpi;
import imlui.ui.culling;
import imlui.ui.frame_request;
import imlui.ui.renderer;

using namespace imlui;

namespace {

/// A w x h box that records where it rendered and how often. `scaled`: h is dp, so its height follows the zoom.
class Box final : public Element {
public:
    static void declare(TypeBuilder<Box>& type) {
        type.property("w", &Box::w, &Box::setW)
            .property("h", &Box::h, &Box::setH)
            .property("scaled", &Box::scaled, &Box::setScaled);
    }
    void render(Frame& frame) override {
        m_pos = ImGui::GetCursorScreenPos();
        ++m_renders;
        ImGui::Dummy({m_w, m_scaled ? frame.dp(m_h) : m_h});
    }
    [[nodiscard]] bool scaled() const { return m_scaled; }
    void setScaled(bool scaled) { m_scaled = scaled; }
    [[nodiscard]] float w() const { return m_w; }
    void setW(float w) { m_w = w; }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    float m_w = 10.0f;
    float m_h = 10.0f;
    bool m_scaled = false;
    ImVec2 m_pos{-1.0f, -1.0f};
    int m_renders = 0;
};

/// Scrolls the window it renders in to `y` (ImGui applies it on the next frame) when `y` >= 0. Non-visual, so it
/// renders wherever it is (and is never culled).
class Scroller final : public Element {
public:
    static void declare(TypeBuilder<Scroller>& type) { type.property("y", &Scroller::y, &Scroller::setY).nonVisual(); }
    void render(Frame&) override {
        if (m_y >= 0.0f) ImGui::SetScrollY(m_y);
        m_maxY = ImGui::GetScrollMaxY();
        m_scrollY = ImGui::GetScrollY();
    }
    [[nodiscard]] float y() const { return m_y; }
    void setY(float y) { m_y = y; }
    [[nodiscard]] float maxY() const { return m_maxY; }
    [[nodiscard]] float scrollY() const { return m_scrollY; }

private:
    float m_y = -1.0f;
    float m_maxY = 0.0f;
    float m_scrollY = 0.0f;
};

/// A tree of every stack kind around boxes of mixed heights, 400 dp tall: `n` items in a ScrollView, each a Column,
/// Card, Row or Grid of boxes. `reads` counts the evaluations of the boxes' `h` bindings.
constexpr const char* mixedTree = R"(
    reads = 0
    scroll = state { y = -1 }
    local function h(i) return function() reads = reads + 1; return 12 + (i % 5) * 7 end end
    local function item(i)
        local k = i % 4
        if k == 0 then return Column { spacing = 3, named("b" .. i, Box { h = h(i) }), Box { h = 9 } } end
        if k == 1 then return Card { padding = 6, named("b" .. i, Box { h = h(i), w = 40 }) } end
        if k == 2 then return Row { named("b" .. i, Box { h = h(i) }), Box { h = 30 } } end
        return Grid { columns = 3, named("b" .. i, Box { h = h(i) }), Box { h = 5 }, Box { h = 25 }, Box { h = 8 } }
    end
    root = Column {
        Box { id = "top", h = 20 },
        ScrollView { id = "sv", height = 400, spacing = 5, align = "center",
            Scroller { id = "scroller", y = function() return scroll.y end },
            Repeater { model = N, delegate = function(i) return item(i) end } },
    })";

/// A Grid of `N` boxes of mixed heights, 4 columns, one hidden (its cell stays empty), and a Timer among them, in a
/// ScrollView 400 dp tall.
constexpr const char* gridTree = R"(
    reads = 0
    scroll = state { y = -1 }
    local function h(i) return function() reads = reads + 1; return 12 + (i * 13 % 9) * 5 end end
    root = ScrollView { id = "sv", height = 400,
        Scroller { id = "scroller", y = function() return scroll.y end },
        Grid { columns = 4, spacing = 6,
            Timer { interval = 1000 },
            Repeater { model = N, delegate = function(i)
                return named("b" .. i, Box { h = h(i), visible = i ~= 7 }) end } } })";

struct Snapshot {
    std::vector<ImVec2> positions; ///< of the boxes b1..bN that rendered on the last frame (-1, -1: not rendered)
    float maxScroll = 0.0f;
};

class CullingTest : public ::testing::Test {
protected:
    void SetUp() override { start(); }
    void TearDown() override { setZoom(1.0f); }
    void start() {
        m_h.emplace(test::HeadlessFrame::Options{.builtins = true});
        m_h->registerType<Box>("Box");
        m_h->registerType<Scroller>("Scroller");
        // Delegates have id scopes of their own: name their boxes in the environment instead (box(name)).
        run("function named(name, e) _ENV[name] = e; return e end");
    }
    void run(std::string_view code) { ASSERT_EQ(m_h->run(code), ""); }
    Box& box(std::string_view name) {
        Box* b = m_h->instance<Box>(name);
        EXPECT_NE(b, nullptr) << name;
        static Box none;
        return b ? *b : none;
    }
    /// The number held by the free name `name` in the script environment.
    int readNumber(std::string_view name) {
        EXPECT_EQ(m_h->run("rawset(_G, '__number', " + std::string{name} + ")"), "");
        lua::State* s = m_h->state();
        lua::getglobal(s, "__number");
        const int n = lua::isnumber(s, -1) ? static_cast<int>(lua::tonumber(s, -1)) : -1;
        lua::pop(s);
        return n;
    }
    /// Renders `frames` frames scrolled to `y` (applied on the first) and returns where the boxes were on the last.
    Snapshot snapshot(int n, float y, int frames) {
        std::vector<int> before(static_cast<std::size_t>(n) + 1);
        run("scroll.y = " + std::to_string(y));
        for (int f = 0; f < frames; ++f) {
            for (int i = 1; i <= n; ++i)
                before[static_cast<std::size_t>(i)] = box("b" + std::to_string(i)).renders();
            m_h->renderFrame("root");
            run("scroll.y = -1");
        }
        Snapshot s;
        for (int i = 1; i <= n; ++i) {
            Box& b = box("b" + std::to_string(i));
            s.positions.push_back(b.renders() > before[static_cast<std::size_t>(i)] ? b.pos() : ImVec2{-1.0f, -1.0f});
        }
        s.maxScroll = m_h->instance<Scroller>("scroller")->maxY();
        return s;
    }

    std::optional<test::HeadlessFrame> m_h;
};

constexpr int count = 120;

} // namespace

TEST_F(CullingTest, CulledChildrenDontRunTheirBindings) {
    run(std::string{"N = 200\n"} + mixedTree);
    m_h->renderFrames("root", 4);
    const int all = readNumber("reads");
    run("reads = 0");
    m_h->renderFrame("root");
    const int visible = readNumber("reads");
    EXPECT_GT(visible, 0);
    EXPECT_LT(visible, 60) << all; // a ScrollView 400 dp tall shows a few dozen of the 200 items
    const int far = box("b150").renders();
    m_h->renderFrames("root", 3);
    EXPECT_EQ(box("b150").renders(), far); // out of view: not rendered
    EXPECT_TRUE(luaui::errors().empty());
}

// Positions and the content height are the same with culling on and off, at the top, after scrolling (on the very
// first frame the culled children come back) and after scrolling back.
TEST_F(CullingTest, PositionsAndContentHeightAreTheSameOnAndOff) {
    for (const char* tree : {mixedTree, gridTree}) {
        const auto sequence = [&](bool culling) {
            start();
            run(std::string{"N = "} + std::to_string(count) + "\n" + tree);
            if (!culling) run("ui.setCulling(false)");
            m_h->renderFrame("root"); // builds the delegates
            std::vector<Snapshot> shots;
            shots.push_back(snapshot(count, -1.0f, 4));
            shots.push_back(snapshot(count, 1500.0f, 2)); // the scroll applies on the 2nd frame: its first in view
            shots.push_back(snapshot(count, 1e6f, 2));    // the bottom
            shots.push_back(snapshot(count, 700.0f, 2));
            shots.push_back(snapshot(count, 0.0f, 2));
            return shots;
        };
        const std::vector<Snapshot> on = sequence(true);
        const std::vector<Snapshot> off = sequence(false);
        ASSERT_EQ(on.size(), off.size());
        for (std::size_t s = 0; s < on.size(); ++s) {
            EXPECT_FLOAT_EQ(on[s].maxScroll, off[s].maxScroll) << s;
            int seen = 0;
            for (std::size_t i = 0; i < on[s].positions.size(); ++i) {
                const ImVec2 a = on[s].positions[i];
                // Culled. (Without culling, a box in a Card out of view doesn't render either: ImGui clips the card's
                // window.)
                if (a.y < 0.0f || off[s].positions[i].y < 0.0f) continue;
                ++seen;
                EXPECT_FLOAT_EQ(a.x, off[s].positions[i].x) << "shot " << s << " b" << i + 1;
                EXPECT_FLOAT_EQ(a.y, off[s].positions[i].y) << "shot " << s << " b" << i + 1;
            }
            EXPECT_GT(seen, 5) << s;
            int culled = 0;
            for (const ImVec2 a : on[s].positions)
                culled += a.y < 0.0f ? 1 : 0;
            EXPECT_GT(culled, count / 2) << s;
        }
        EXPECT_GT(on[2].maxScroll, 1000.0f);
        EXPECT_TRUE(luaui::errors().empty());
    }
}

TEST_F(CullingTest, ATimerOutOfViewStillFires) {
    run(R"(
        fired = 0
        root = ScrollView { height = 200,
            Repeater { model = 100, delegate = function(i) return Box { h = 30 } end },
            Column { Box { id = "last" }, Timer { interval = 50, ["repeat"] = true,
                                                  onTriggered = function() fired = fired + 1 end } } })");
    m_h->renderFrames("root", 4);
    const int renders = box("last").renders();
    m_h->renderFrames("root", 30);             // half a second
    EXPECT_EQ(box("last").renders(), renders); // its column is culled
    EXPECT_GE(readNumber("fired"), 5);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(CullingTest, ADialogDeclaredOutOfViewStillOpens) {
    run(R"(
        s = state { open = false }
        root = ScrollView { height = 200,
            Repeater { model = 100, delegate = function(i) return Box { h = 30 } end },
            Column { Box { id = "last" },
                     Dialog { open = bind(s, "open"), title = "T", Box { id = "inDialog" } } } })");
    m_h->renderFrames("root", 4);
    EXPECT_EQ(box("inDialog").renders(), 0);
    run("s.open = true");
    m_h->renderFrames("root", 3);
    EXPECT_GT(box("inDialog").renders(), 0);
    EXPECT_TRUE(luaui::errors().empty());
}

// A page longer than Renderer::convergeRenders: on its first frame, the children below the view that were never
// measured are laid out at an estimate instead of rendering, except one whose subtree holds a non-visual element (a
// Dialog), which renders, so the Dialog opens right away, in a stack and in a Grid cell alike.
TEST_F(CullingTest, AFirstFrameEstimatesWhatIsBelowTheViewButRendersADialogInIt) {
    for (const char* layout : {"Column", "Grid"}) {
        start();
        run(std::string{"Layout = "} + layout + R"(
            s = state { open = false }
            renders = 0
            root = ScrollView { height = 200,
                Layout { columns = 2,
                    Repeater { model = 1000, delegate = function(i)
                        return Box { h = function() renders = renders + 1; return 30 end } end },
                    Column { Box { id = "last" },
                             Dialog { open = bind(s, "open"), title = "T", Box { id = "inDialog" } } } } })");
        m_h->renderFrame("root");
        EXPECT_LT(readNumber("renders"), 500) << layout; // not every box of the 1000 on the first frame
        EXPECT_EQ(box("inDialog").renders(), 0) << layout;
        run("s.open = true");
        m_h->renderFrames("root", 2);
        EXPECT_GT(box("inDialog").renders(), 0) << layout;
        EXPECT_TRUE(luaui::errors().empty()) << layout;
    }
}

TEST_F(CullingTest, AFocusedTextFieldScrolledOutKeepsItsEdit) {
    run(R"(
        s = state { t = "" }
        scroll = state { y = -1 }
        root = ScrollView { height = 300,
            Scroller { id = "scroller", y = function() return scroll.y end },
            Column { TextField { id = "field", text = bind(s, "t"), width = 300 } },
            Repeater { model = 200, delegate = function(i) return Box { h = 30 } end } })");
    m_h->renderFrames("root", 4);
    // Click the field (its column is the window's first item).
    const float x = ImGui::GetStyle().WindowPadding.x + 150.0f;
    m_h->click("root", x, 40.0f, 2);
    ImGui::GetIO().AddInputCharacter('a');
    m_h->renderFrames("root", 2);
    ASSERT_EQ(m_h->run(R"(assert(s.t == "a", s.t))"), "");
    run("scroll.y = 3000");
    m_h->renderFrames("root", 3);
    run("scroll.y = -1");
    EXPECT_GT(m_h->instance<Scroller>("scroller")->scrollY(), 2000.0f); // the field is far out of view
    ImGui::GetIO().AddInputCharacter('b');
    m_h->renderFrames("root", 2);
    EXPECT_EQ(m_h->run(R"(assert(s.t == "ab", s.t))"), "");
    EXPECT_TRUE(ImGui::IsAnyItemActive());
    EXPECT_TRUE(luaui::errors().empty());
}

// A long page renders every child on its first frame only. On the next ones (where the ScrollView's scrollbar
// appears and the width its children get changes) the children below the view are laid out by their last sizes,
// except for up to Renderer::convergeRenders a frame that render so their sizes settle: the scroll range converges,
// over frames it asks for, to the one with culling off, and then the page asks for nothing.
TEST_F(CullingTest, ALongPageConvergesWithoutRenderingEveryChildAgain) {
    constexpr int n = 2000;
    const auto renderedOnNextFrame = [&] {
        std::vector<int> before(n + 1);
        for (int i = 1; i <= n; ++i)
            before[static_cast<std::size_t>(i)] = box("b" + std::to_string(i)).renders();
        m_h->renderFrame("root");
        int rendered = 0;
        for (int i = 1; i <= n; ++i)
            rendered += box("b" + std::to_string(i)).renders() > before[static_cast<std::size_t>(i)] ? 1 : 0;
        return rendered;
    };
    run(std::string{"N = "} + std::to_string(n) + "\n" + mixedTree);
    m_h->renderFrame("root");
    EXPECT_LT(renderedOnNextFrame(), Renderer::convergeRenders + 100); // not all n again
    int frames = 1;
    for (; frames < 100 && !std::isinf(secondsUntilFrame()); ++frames)
        m_h->renderFrame("root");
    EXPECT_GT(frames, 2);   // it converged over frames it asked for ...
    EXPECT_LT(frames, 100); // ... and then asks for nothing
    const float converged = m_h->instance<Scroller>("scroller")->maxY();

    start();
    run(std::string{"N = "} + std::to_string(n) + "\n" + mixedTree);
    run("ui.setCulling(false)");
    m_h->renderFrames("root", 6);
    EXPECT_FLOAT_EQ(converged, m_h->instance<Scroller>("scroller")->maxY());
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(CullingTest, ScrollingASettledListAsksForNoFrames) {
    run(std::string{"N = 300\n"} + mixedTree);
    m_h->renderFrames("root", 6);
    for (const int y : {500, 1500, 4000, 2500, 0}) {
        run("scroll.y = " + std::to_string(y));
        m_h->renderFrame("root");
        run("scroll.y = -1");
        m_h->renderFrame("root"); // the scroll applies here: culled children come into view
        m_h->renderFrame("root");
        EXPECT_TRUE(std::isinf(secondsUntilFrame())) << y;
    }
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(CullingTest, NestedScrollViews) {
    const auto sequence = [&](bool culling) {
        start();
        run(R"(
            outer = state { y = -1 } inner = state { y = -1 }
            root = ScrollView { height = 400, spacing = 4,
                Scroller { y = function() return outer.y end },
                Repeater { model = 30, delegate = function(i) return named("o" .. i, Box { h = 20 + i % 3 }) end },
                ScrollView { id = "in", height = 150, spacing = 2,
                    Scroller { id = "innerScroller", y = function() return inner.y end },
                    Repeater { model = 60, delegate = function(i) return named("i" .. i, Box { h = 15 + i % 4 }) end } },
                Repeater { model = 30, delegate = function(i) return named("p" .. i, Box { h = 25 }) end } })");
        if (!culling) run("ui.setCulling(false)");
        std::vector<ImVec2> out;
        const auto shot = [&] {
            for (const char* prefix : {"o", "i", "p"})
                for (int i = 1; i <= 30; ++i) {
                    Box& b = box(std::string{prefix} + std::to_string(i));
                    out.push_back(b.pos());
                }
            out.push_back({m_h->instance<Scroller>("innerScroller")->maxY(), 0.0f});
        };
        m_h->renderFrames("root", 4);
        run("outer.y = 500");
        m_h->renderFrames("root", 2);
        run("outer.y = -1 inner.y = 400");
        m_h->renderFrames("root", 2);
        run("inner.y = -1");
        shot();
        return out;
    };
    const std::vector<ImVec2> on = sequence(true);
    const std::vector<ImVec2> off = sequence(false);
    ASSERT_EQ(on.size(), off.size());
    // Every box rendered at some point; a culled one keeps the position it last rendered at. Those that rendered on
    // the last frame with culling on are the same.
    int same = 0;
    for (std::size_t i = 0; i < on.size(); ++i)
        if (on[i].x == off[i].x && on[i].y == off[i].y) ++same;
    EXPECT_FLOAT_EQ(on.back().x, off.back().x); // the inner content height
    EXPECT_GT(same, 10);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(CullingTest, CullFalseAndSetCullingRenderEveryChild) {
    run(R"(
        root = Column {
            ScrollView { height = 100, cull = false,
                Repeater { model = 50, delegate = function(i) return named("a" .. i, Box { h = 30 }) end } },
            ScrollView { height = 100,
                Repeater { model = 50, delegate = function(i) return named("b" .. i, Box { h = 30 }) end } } })");
    m_h->renderFrames("root", 4);
    const int a = box("a50").renders(), b = box("b50").renders();
    m_h->renderFrames("root", 2);
    EXPECT_EQ(box("a50").renders(), a + 2); // cull = false: rendered every frame
    EXPECT_EQ(box("b50").renders(), b);     // culled
    run("assert(ui.culling() == true) ui.setCulling(false) assert(ui.culling() == false)");
    m_h->renderFrames("root", 2);
    EXPECT_EQ(box("b50").renders(), b + 2);
    EXPECT_TRUE(luaui::errors().empty());
}

// The sizes culled children are laid out by are pixels: on a frame the DPI scale changed (a zoom, a monitor with
// another scale), nothing is culled, as on a resize, so the children above the view don't keep their old sizes (which
// had settled) and move what is in view. Positions and the content height are the same as with culling off.
TEST_F(CullingTest, AZoomMeasuresCulledChildrenAgain) {
    constexpr int n = 120;
    const std::string tree = std::string{"N = "} + std::to_string(n) + R"(
        scroll = state { y = -1 }
        root = ScrollView { id = "sv", height = 300, spacing = 0,
            Scroller { id = "scroller", y = function() return scroll.y end },
            Repeater { model = N, delegate = function(i)
                return named("b" .. i, Box { h = 10 + i % 3 * 5, scaled = true }) end } })";
    const auto sequence = [&](bool culling) {
        start();
        run(tree);
        if (!culling) run("ui.setCulling(false)");
        m_h->renderFrame("root");
        (void)snapshot(n, -1.0f, 4);
        (void)snapshot(n, 800.0f, 4); // the boxes above the view are culled, at their settled sizes
        setZoom(2.0f);
        Snapshot shot = snapshot(n, -1.0f, 4);
        setZoom(1.0f);
        return shot;
    };
    const Snapshot on = sequence(true);
    const Snapshot off = sequence(false);
    EXPECT_FLOAT_EQ(on.maxScroll, off.maxScroll);
    int seen = 0;
    for (std::size_t i = 0; i < on.positions.size(); ++i) {
        if (on.positions[i].y < 0.0f || off.positions[i].y < 0.0f) continue;
        ++seen;
        EXPECT_FLOAT_EQ(on.positions[i].y, off.positions[i].y) << "b" << i + 1;
    }
    EXPECT_GT(seen, 5);
    EXPECT_TRUE(luaui::errors().empty());
}

// Pins are remembered for the frame they are made on and the next; the older ones are dropped in sweeps that come
// when the pins have doubled since the last one, so many pins on every frame cost no walk over all of them each.
TEST_F(CullingTest, OldPinsAreDropped) {
    run("root = Column {}");
    m_h->renderFrame("root");
    constexpr ImGuiID perFrame = 3000;
    ImGuiID next = 1;
    for (int frame = 0; frame < 12; ++frame) {
        for (ImGuiID i = 0; i < perFrame; ++i)
            cull::pin(next++);
        EXPECT_TRUE(cull::pinned(next - 1));
        // At most the pins of this frame and the last, and as many again since the last sweep.
        EXPECT_LE(cull::pinCount(), 4 * perFrame) << frame;
        m_h->renderFrame("root");
    }
    EXPECT_TRUE(cull::pinned(next - 1));                 // pinned on the last frame
    EXPECT_FALSE(cull::pinned(next - 2 * perFrame - 1)); // two frames ago
}
