// ListView scrolling from a script (imlui.ui.elements.list_view): `followBottom` (sticks to the end while the view is
// there, with a growing Lua list and a C++ model, fixed and measured heights; stops when scrolled up, resumes at the
// end; no frames while nothing changes), positionViewAtIndex() to items that were never built (fixed and variable
// heights, every mode, out-of-range indices, a bad mode, before the first render, inside a ScrollView),
// positionViewAtBeginning() / positionViewAtEnd() and `firstVisibleIndex`, and requests made while the list isn't
// rendered, and the chunked rendering staying finite: while a nav move scores items (which widens the view), and with
// a negative `spacing` that cancels `itemHeight`; the step tree (positions right as the model shrinks and grows) and
// long runs of hidden items probed, not walked, on every frame; a model longer than ListView::maxItems capped, one
// cleared by a delegate while the list renders, and measured heights scaled with a zoom. Renders headless ImGui frames
// (tests/support/headless_frame.cppm).
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.lua.errors;
import imlui.theme.dpi;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.types.vector_model;
import imlui.ui.elements.list_view;
import imlui.ui.frame_request;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

/// A box `h` px tall that records where it was placed.
class Bar final : public Element {
public:
    static void declare(TypeBuilder<Bar>& type) { type.property("h", &Bar::h, &Bar::setH); }
    void render(Frame&) override {
        m_y = ImGui::GetCursorScreenPos().y;
        ++m_renders;
        ImGui::Dummy({10.0f, m_h});
    }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }
    [[nodiscard]] float y() const { return m_y; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    float m_h = 20.0f;
    float m_y = -1.0f;
    int m_renders = 0;
};

/// A box `h` dp tall: its height in pixels follows the zoom, like real content.
class DpBar final : public Element {
public:
    static void declare(TypeBuilder<DpBar>& type) { type.property("h", &DpBar::h, &DpBar::setH); }
    void render(Frame& frame) override { ImGui::Dummy({10.0f, frame.dp(m_h)}); }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }

private:
    float m_h = 20.0f;
};

/// Emits onRender while it renders (Lua code running mid-frame, like a handler) and takes 20 px.
class Emitter final : public Element {
public:
    static void declare(TypeBuilder<Emitter>& type) { type.signal("onRender"); }
    void render(Frame&) override {
        emit("onRender");
        ImGui::Dummy({10.0f, 20.0f});
    }
};

/// Marks the frame as scoring a nav move from where it renders on, as ImGui does on the frame an arrow key moves the
/// focus (NavUpdate clears it on the next frame): cull::outOfView widens the view by a page then.
class NavScoring final : public Element {
public:
    static void declare(TypeBuilder<NavScoring>&) {}
    void render(Frame&) override {
        ImGui::GetCurrentContext()->NavMoveScoringItems = true;
        ImGui::Dummy({0.0f, 0.0f});
    }
};

/// Ends the process if the scope isn't left within `seconds`: a regression that loops forever then fails the suite
/// instead of hanging it.
class Watchdog {
public:
    explicit Watchdog(double seconds)
        : m_thread{[this, seconds] {
              std::unique_lock lock{m_mutex};
              if (!m_done.wait_for(lock, std::chrono::duration<double>{seconds}, [this] { return m_left; })) {
                  std::cerr << "Watchdog: still running after " << seconds << " s, a loop that never ends" << std::endl;
                  std::_Exit(3);
              }
          }} {}
    ~Watchdog() {
        {
            const std::lock_guard lock{m_mutex};
            m_left = true;
        }
        m_done.notify_one();
        m_thread.join();
    }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    std::mutex m_mutex;
    std::condition_variable m_done;
    bool m_left = false;
    std::thread m_thread; // last: it starts once the others are there
};

/// A singleton with a model that grows by appends, like a log.
class Feed final : public ScriptObject {
public:
    static void declare(TypeBuilder<Feed>& type) {
        type.doc("A test feed.")
            .property("rows", &Feed::rows)
            .doc("The rows.")
            .method("clear", &Feed::clear)
            .doc("Empties the rows.");
    }
    void clear() { model.clear(); }
    [[nodiscard]] const VectorModel<std::int64_t>& rows() const { return model; }
    void add(int count) {
        for (int i = 0; i < count; ++i)
            model.append(static_cast<std::int64_t>(model.size()) + 1);
    }

    VectorModel<std::int64_t> model;
};

/// Heights of the variable lists: 12..44 px, and three times that past index 1000, so the average of the first items
/// measured is far off for the items there.
constexpr const char* variableHeight = "local function vh(i) return (12 + (i % 5) * 8) * (i > 1000 and 3 or 1) end\n";

class ListViewScrollTest : public ::testing::Test {
protected:
    void SetUp() override {
        luaui::clearErrors();
        clearFrameRequests();
        m_h.registerType<Bar>("Bar");
        m_h.registerType<NavScoring>("NavScoring");
        m_h.registerType<Emitter>("Emitter");
        m_h.registerType<DpBar>("DpBar");
        m_h.types().registerSingleton("feed", m_feed);
    }
    void TearDown() override {
        setZoom(1.0f);
        clearFrameRequests();
        ImGui::GetCurrentContext()->NavMoveScoringItems = false;
    }

    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void check(std::string_view lua) { EXPECT_EQ(m_h.run("assert(" + std::string{lua} + ")"), "") << lua; }
    /// Renders the frames asked for (a scroll target, heights that settle, input), at least one.
    int settle(std::string_view root, int limit = 80) {
        m_h.renderFrame(root);
        int n = 1;
        for (; n < limit && secondsUntilFrame() == 0.0; ++n)
            m_h.renderFrame(root);
        return n;
    }
    /// The Bar delegate built for index `i` (`made[i]`); nullptr when it wasn't built.
    Bar* bar(std::int64_t i) {
        if (!m_h.run("b = made[" + std::to_string(i) + "]").empty()) return nullptr;
        return m_h.instance<Bar>("b");
    }
    ListView& list(std::string_view name = "list") {
        ListView* l = m_h.instance<ListView>(name);
        EXPECT_NE(l, nullptr) << name;
        static ListView none;
        return l ? *l : none;
    }
    /// The scroll range of `l` as it rendered: the content height minus the view's (no padding, `height` px).
    static float maxScroll(ListView& l, float height) { return std::max(0.0f, l.contentHeight() - height); }
    /// A wheel move over the list (positive scrolls up).
    void wheel(float amount) { run("native.injectMouse(100, 100, nil, " + std::to_string(amount) + ")"); }

    Feed m_feed; // outlives the harness, like a host's singleton
    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

// ------------------------------------------------------------ followBottom

// A growing Lua list: the view starts at the end and stays there as items come, with fixed and measured heights; the
// last item's bottom is the view's bottom. Settled, nothing asks for frames.
TEST_F(ListViewScrollTest, FollowBottomKeepsAGrowingLuaListAtTheEnd) {
    for (const char* heights : {"itemHeight = 20,", "", "variable"}) {
        SCOPED_TRACE(heights);
        const bool variable = std::string_view{heights} == "variable";
        run(std::string{variableHeight} + R"(
            made = {}
            items = {}
            for i = 1, 400 do items[i] = i end
            list = ListView { height = 200, spacing = 0, followBottom = true, )" +
            (variable ? std::string{} : std::string{heights}) + R"(
                              model = function() return items end,
                              delegate = function(i) made[i] = Bar { h = )" +
            (variable ? "vh(i)" : "20") + R"( }; return made[i] end })");
        m_h.renderFrame("list");
        const float top = bar(1)->y(); // the first frame is at the top
        settle("list");
        ListView& l = list();
        EXPECT_NEAR(l.scroll().y, maxScroll(l, 200.0f), 1.0f);
        ASSERT_NE(bar(400), nullptr);
        EXPECT_NEAR(bar(400)->y() + bar(400)->h(), top + 200.0f, 1.0f);
        EXPECT_EQ(settle("list"), 1); // nothing more to do

        run("for i = 401, 405 do items[i] = i end");
        settle("list");
        EXPECT_NEAR(l.scroll().y, maxScroll(l, 200.0f), 1.0f);
        ASSERT_NE(bar(405), nullptr);
        EXPECT_NEAR(bar(405)->y() + bar(405)->h(), top + 200.0f, 1.0f);
        check("list.__view.built < 60"); // virtualized: the top (the first frame) and the end
        EXPECT_EQ(settle("list"), 1);
        EXPECT_GT(secondsUntilFrame(), 0.0);
        EXPECT_TRUE(luaui::errors().empty());
    }
}

// Scrolling up stops following: new items leave the view where it is. Scrolling back to the end follows again.
TEST_F(ListViewScrollTest, FollowBottomStopsWhenScrolledUpAndResumesAtTheEnd) {
    run(R"(
        made = {}
        items = {}
        for i = 1, 100 do items[i] = i end
        list = ListView { height = 200, spacing = 0, followBottom = true, model = function() return items end,
                          delegate = function(i) made[i] = Bar { h = 20 }; return made[i] end })");
    settle("list");
    ListView& l = list();
    ASSERT_NEAR(l.scroll().y, 100.0f * 20.0f - 200.0f, 1.0f);

    wheel(3.0f); // up
    settle("list");
    const float up = l.scroll().y;
    ASSERT_LT(up, 100.0f * 20.0f - 210.0f);
    run("for i = 101, 110 do items[i] = i end");
    settle("list");
    EXPECT_FLOAT_EQ(l.scroll().y, up);
    EXPECT_FLOAT_EQ(l.contentHeight(), 110.0f * 20.0f);

    wheel(-100.0f); // back to the end
    settle("list");
    EXPECT_NEAR(l.scroll().y, 110.0f * 20.0f - 200.0f, 1.0f);
    run("for i = 111, 120 do items[i] = i end");
    settle("list");
    EXPECT_NEAR(l.scroll().y, 120.0f * 20.0f - 200.0f, 1.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// A C++ model that grows by appends (one notification each, each asking for a frame) is followed too.
TEST_F(ListViewScrollTest, FollowBottomFollowsAGrowingModel) {
    m_feed.add(30);
    run(R"(
        made = {}
        list = ListView { height = 200, spacing = 0, followBottom = true, model = feed.rows,
                          delegate = function(row) made[row] = Bar { h = 25 }; return made[row] end })");
    m_h.renderFrame("list");
    const float top = bar(1)->y();
    settle("list");
    ListView& l = list();
    EXPECT_NEAR(l.scroll().y, 30.0f * 25.0f - 200.0f, 1.0f);
    for (int round = 0; round < 3; ++round) {
        m_feed.add(7);
        EXPECT_EQ(secondsUntilFrame(), 0.0); // the model asked for a frame
        settle("list");
        const auto last = static_cast<std::int64_t>(m_feed.model.size());
        EXPECT_NEAR(l.scroll().y, static_cast<float>(last) * 25.0f - 200.0f, 1.0f);
        ASSERT_NE(bar(last), nullptr);
        EXPECT_NEAR(bar(last)->y() + 25.0f, top + 200.0f, 1.0f);
    }
    EXPECT_EQ(settle("list"), 1);
    EXPECT_TRUE(luaui::errors().empty());
}

// The default is off: a list at its end stays where it is when items come. A list shorter than its view stays at the
// top either way, and starts following once it is longer.
TEST_F(ListViewScrollTest, WithoutFollowBottomNewItemsLeaveTheViewWhereItIs) {
    run(R"(
        items = { 1, 2, 3 }
        plain = ListView { height = 200, spacing = 0, model = function() return items end,
                           delegate = function(i) return Bar { h = 20 } end }
        short = ListView { height = 200, spacing = 0, followBottom = true, model = function() return items end,
                           delegate = function(i) return Bar { h = 20 } end }
        root = Row { plain, short })");
    settle("root");
    ListView& plain = list("plain");
    ListView& shortList = list("short");
    EXPECT_EQ(shortList.scroll().y, 0.0f);
    run("for i = 4, 50 do items[i] = i end");
    settle("root");
    EXPECT_EQ(plain.scroll().y, 0.0f);
    EXPECT_NEAR(shortList.scroll().y, 50.0f * 20.0f - 200.0f, 1.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// A view that gets smaller while it follows stays at the end.
TEST_F(ListViewScrollTest, FollowBottomSurvivesASmallerView) {
    run(R"(
        s = state { h = 300 }
        list = ListView { height = function() return s.h end, spacing = 0, followBottom = true, model = 100,
                          delegate = function(i) return Bar { h = 20 } end })");
    settle("list");
    ListView& l = list();
    EXPECT_NEAR(l.scroll().y, 2000.0f - 300.0f, 1.0f);
    run("s.h = 150");
    settle("list");
    EXPECT_NEAR(l.scroll().y, 2000.0f - 150.0f, 1.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ positionViewAtIndex

// Fixed heights: a far item that was never built lands where each mode puts it, in one frame, and building it builds
// only what is around it.
TEST_F(ListViewScrollTest, PositionViewAtIndexWithFixedHeights) {
    run(R"(
        made = {}
        list = ListView { height = 200, spacing = 4, itemHeight = 20, model = 10000,
                          delegate = function(i) made[i] = Bar { h = 20 }; return made[i] end })");
    settle("list");
    const float top = bar(1)->y();
    ListView& l = list();

    check("made[5000] == nil");
    run("list:positionViewAtIndex(5000)");
    EXPECT_LE(settle("list"), 3);
    ASSERT_NE(bar(5000), nullptr);
    EXPECT_NEAR(bar(5000)->y(), top, 1.0f);
    EXPECT_NEAR(l.scroll().y, 4999.0f * 24.0f, 1.0f);
    check("made[4000] == nil and list.__view.built < 60");
    check("list.firstVisibleIndex == 5000");

    run("list:positionViewAtIndex(7000, 'center')");
    settle("list");
    EXPECT_NEAR(bar(7000)->y() + 10.0f, top + 100.0f, 1.0f);
    run("list:positionViewAtIndex(3000, 'end')");
    settle("list");
    EXPECT_NEAR(bar(3000)->y() + 20.0f, top + 200.0f, 1.0f);

    // contain: below the view it ends at the bottom, above at the top, inside it doesn't move.
    run("list:positionViewAtIndex(3020, 'contain')");
    settle("list");
    EXPECT_NEAR(bar(3020)->y() + 20.0f, top + 200.0f, 1.0f);
    const float scroll = l.scroll().y;
    run("list:positionViewAtIndex(3015, 'contain')");
    EXPECT_EQ(settle("list"), 1);
    EXPECT_FLOAT_EQ(l.scroll().y, scroll);
    run("list:positionViewAtIndex(3000, 'contain')");
    settle("list");
    EXPECT_NEAR(bar(3000)->y(), top, 1.0f);

    run("list:positionViewAtEnd()");
    settle("list");
    EXPECT_NEAR(l.scroll().y, maxScroll(l, 200.0f), 1.0f);
    EXPECT_NEAR(bar(10000)->y() + 20.0f, top + 200.0f, 1.0f);
    run("list:positionViewAtBeginning()");
    settle("list");
    EXPECT_EQ(l.scroll().y, 0.0f);
    EXPECT_NEAR(bar(1)->y(), top, 1.0f);
    EXPECT_EQ(settle("list"), 1);
    EXPECT_TRUE(luaui::errors().empty());
}

// Measured heights, three times the estimate where the target is: the first scroll is by the estimate, the next
// frames correct it as the items there are measured, and the item ends where it was asked to be, and stays.
TEST_F(ListViewScrollTest, PositionViewAtIndexWithVariableHeightsLandsAfterCorrecting) {
    run(std::string{variableHeight} + R"(
        made = {}
        list = ListView { height = 200, spacing = 2, model = 2000,
                          delegate = function(i) made[i] = Bar { h = vh(i) }; return made[i] end })");
    settle("list");
    const float top = bar(1)->y();
    ListView& l = list();
    struct Case {
        std::int64_t index;
        const char* mode;
    };
    for (const Case c : {Case{1500, "beginning"}, Case{1800, "center"}, Case{1200, "end"}, Case{400, "beginning"},
                         Case{1999, "end"}}) {
        SCOPED_TRACE(std::to_string(c.index) + " " + c.mode);
        run("list:positionViewAtIndex(" + std::to_string(c.index) + ", '" + c.mode + "')");
        const int frames = settle("list");
        EXPECT_LT(frames, 25);
        Bar* b = bar(c.index);
        ASSERT_NE(b, nullptr);
        const float where = std::string_view{c.mode} == "beginning" ? b->y()
                            : std::string_view{c.mode} == "center"  ? b->y() + b->h() * 0.5f - 100.0f
                                                                    : b->y() + b->h() - 200.0f;
        EXPECT_NEAR(where, top, 1.0f);
        const float scroll = l.scroll().y;
        const float y = b->y();
        m_h.renderFrames("list", 2); // stays
        EXPECT_FLOAT_EQ(l.scroll().y, scroll);
        EXPECT_FLOAT_EQ(b->y(), y);
        check("list.__view.built < 200");
    }
    EXPECT_GT(secondsUntilFrame(), 0.0);
    EXPECT_TRUE(luaui::errors().empty());
}

// Indices are clamped to 1..#model; a mode that isn't one raises; an empty model or a list without a delegate does
// nothing.
TEST_F(ListViewScrollTest, PositionViewAtIndexClampsAndChecksItsArguments) {
    run(R"(
        made = {}
        items = {}
        for i = 1, 300 do items[i] = i end
        list = ListView { height = 200, spacing = 0, model = function() return items end,
                          delegate = function(i) made[i] = Bar { h = 20 }; return made[i] end }
        plain = ListView { height = 100, Bar {} }
        root = Column { list, plain })");
    settle("root");
    ListView& l = list();
    run("list:positionViewAtIndex(1000000)"); // the last item, as far up as the end lets it
    settle("root");
    EXPECT_NEAR(l.scroll().y, 300.0f * 20.0f - 200.0f, 1.0f);
    run("list:positionViewAtIndex(-5, 'end')"); // the first
    settle("root");
    EXPECT_EQ(l.scroll().y, 0.0f);
    run("list:positionViewAtIndex(0, 'center')");
    settle("root");
    EXPECT_EQ(l.scroll().y, 0.0f);

    const std::string bad = m_h.run("list:positionViewAtIndex(3, 'middle')");
    EXPECT_NE(bad.find("bad argument #2"), std::string::npos) << bad;
    EXPECT_NE(bad.find("'beginning'"), std::string::npos) << bad;

    run("items = {}; list:positionViewAtIndex(5)");
    settle("root");
    EXPECT_EQ(l.scroll().y, 0.0f);
    run("plain:positionViewAtIndex(1); plain:positionViewAtEnd()");
    settle("root");
    check("plain.firstVisibleIndex == 0");
    EXPECT_TRUE(luaui::errors().empty());
}

// Called before the list ever rendered: the first frames place it.
TEST_F(ListViewScrollTest, PositionViewAtIndexBeforeTheFirstRender) {
    for (const bool variable : {false, true}) {
        SCOPED_TRACE(variable ? "variable" : "fixed");
        run(std::string{variableHeight} + "made = {}\nlist = ListView { height = 200, spacing = 0, " +
            (variable ? "" : "itemHeight = 20, ") + R"(model = 3000,
                delegate = function(i) made[i] = Bar { h = )" +
            (variable ? "vh(i)" : "20") + R"( }; return made[i] end }
            list:positionViewAtIndex(2500, 'center'))");
        check("list.firstVisibleIndex == 0");
        m_h.renderFrame("list");
        const float top = bar(1)->y(); // the first frame is still at the top
        settle("list");
        Bar* b = bar(2500);
        ASSERT_NE(b, nullptr);
        EXPECT_NEAR(b->y() + b->h() * 0.5f, top + 100.0f, 1.0f);
        check("list.__view.built < 120");
        EXPECT_TRUE(luaui::errors().empty());
    }
}

// Inside a ScrollView (which scrolls too): the list scrolls its own view; the position is relative to it.
TEST_F(ListViewScrollTest, PositionViewAtIndexInsideAScrollView) {
    run(std::string{variableHeight} + R"(
        made = {}
        list = ListView { height = 200, spacing = 0, model = 2000,
                          delegate = function(i) made[i] = Bar { h = vh(i) }; return made[i] end }
        root = ScrollView { height = 400, Column { spacing = 0, Bar { h = 150 }, list, Bar { h = 600 } } })");
    settle("root");
    const float top = bar(1)->y();
    run("list:positionViewAtIndex(1700)");
    settle("root");
    Bar* b = bar(1700);
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->y(), top, 1.0f);
    check("list.firstVisibleIndex == 1700");

    run("native.injectMouse(100, 60, nil, -2)"); // the outer view scrolls (the pointer is above the list)
    settle("root");
    const float outerMoved = top - bar(1700)->y();
    ASSERT_GT(outerMoved, 0.0f);
    run("list:positionViewAtIndex(600, 'end')");
    settle("root");
    ASSERT_NE(bar(600), nullptr);
    EXPECT_NEAR(bar(600)->y() + bar(600)->h(), top - outerMoved + 200.0f, 1.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// firstVisibleIndex follows the scroll and calls its change signal.
TEST_F(ListViewScrollTest, FirstVisibleIndexFollowsTheScroll) {
    run(R"(
        seen = {}
        list = ListView { height = 200, spacing = 0, itemHeight = 20, model = 500,
                          delegate = function(i) return Bar { h = 20 } end,
                          onFirstVisibleIndexChanged = function(self, i) seen[#seen + 1] = i end })");
    settle("list");
    check("list.firstVisibleIndex == 1 and seen[1] == 1");
    run("list:positionViewAtIndex(250)");
    settle("list");
    check("list.firstVisibleIndex == 250 and seen[#seen] == 250");
    EXPECT_EQ(list().firstVisibleIndex(), 250);
    run("list:positionViewAtIndex(100, 'end')"); // 100's bottom at the view's bottom: 91 starts the view
    settle("list");
    check("list.firstVisibleIndex == 91");
    EXPECT_TRUE(luaui::errors().empty());
}

// A list that isn't rendered (another page shown, or clipped inside an outer ScrollView) applies nothing: a position
// asked for meanwhile, and items that came while it followed the end, take effect when it renders again.
TEST_F(ListViewScrollTest, RequestsWaitWhileTheListIsNotRendered) {
    run(R"(
        made = {}
        items = {}
        for i = 1, 100 do items[i] = i end
        list = ListView { height = 200, spacing = 0, itemHeight = 20, followBottom = true,
                          model = function() return items end,
                          delegate = function(i) made[i] = Bar { h = 20 }; return made[i] end }
        other = Bar { h = 50 })");
    settle("list");
    ListView& l = list();
    ASSERT_NEAR(l.scroll().y, 100.0f * 20.0f - 200.0f, 1.0f);

    // Another page: items come, the list doesn't render; back on it, it is at the new end.
    run("for i = 101, 150 do items[i] = i end");
    settle("other");
    check("made[150] == nil");
    settle("list");
    EXPECT_NEAR(l.scroll().y, 150.0f * 20.0f - 200.0f, 1.0f);

    // A position asked for while another page shows: kept, and applied once the list renders.
    run("list:positionViewAtIndex(40)");
    settle("other");
    check("list.firstVisibleIndex ~= 40");
    settle("list");
    EXPECT_NEAR(l.scroll().y, 39.0f * 20.0f, 1.0f);
    check("list.firstVisibleIndex == 40");

    // Clipped inside an outer ScrollView (below its view): the same.
    run(R"(
        root = ScrollView { height = 200, Column { spacing = 0, Bar { h = 1000 }, list } }
        list:positionViewAtIndex(70))");
    settle("root");
    check("list.firstVisibleIndex == 40");
    run("native.injectMouse(100, 100, nil, -100)"); // the outer view scrolls to its end: the list shows
    settle("root");
    EXPECT_NEAR(l.scroll().y, 69.0f * 20.0f, 1.0f);
    check("list.firstVisibleIndex == 70");
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ finite rendering

// While a nav move scores items, cull::outOfView widens the view by a page, so the list renders past its own limit.
// Each chunk of delegates asked for past it was sized by the (negative) distance left to the limit, cast to size_t:
// an empty chunk, asked for again and again, and the frame never ended. Now it renders the page the nav move can
// reach, and stops there.
TEST_F(ListViewScrollTest, ANavMoveRendersTheWidenedViewAndEnds) {
    for (const char* heights : {"itemHeight = 20,", ""}) {
        SCOPED_TRACE(heights);
        run(std::string{R"(
            made = {}
            list = ListView { height = 200, spacing = 0, )"} +
            heights + R"( model = 10000,
                              delegate = function(i) made[i] = Bar { h = 20 }; return made[i] end }
            root = Column { NavScoring {}, list })");
        const Watchdog watchdog{30.0};
        m_h.renderFrame("root");
        // The view (10 items), half a view below it, and a page more for the nav move: past index 20, not to the end.
        check("made[21] ~= nil and made[2000] == nil");
        ImGui::GetCurrentContext()->NavMoveScoringItems = false;
        settle("root");
        run("made = {}");
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// A negative `spacing` that cancels `itemHeight` (or more) put every item at one place: the step between them was
// zero or negative, so no number of items reached the end of the view and every delegate was built, and the index in
// view (the scroll divided by the step) was a division by zero. The step is at least a pixel.
TEST_F(ListViewScrollTest, ASpacingThatCancelsTheItemHeightStepsAPixel) {
    for (const char* spacing : {"-20", "-30"}) {
        SCOPED_TRACE(spacing);
        run(std::string{R"(
            made = {}
            list = ListView { height = 200, itemHeight = 20, model = 10000, spacing = )"} +
            spacing + R"(,
                              delegate = function(i) made[i] = Bar { h = 20 }; return made[i] end })");
        settle("list");
        check("made[1] ~= nil and made[2000] == nil");
        ListView& l = list();
        EXPECT_FLOAT_EQ(l.contentHeight(), 9999.0f + 20.0f); // the last item's bottom, a pixel per item before it
        run("list:positionViewAtIndex(5000)");
        settle("list");
        EXPECT_NEAR(l.scroll().y, 4999.0f, 1.0f);
        check("list.firstVisibleIndex == 5000 and made[5000] ~= nil and made[7000] == nil");
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ the step tree

// Where an item starts comes from the step tree (prefix sums of the heights, the unmeasured ones at the estimate), not
// a walk from the first item: it stays right as the model shrinks (the heights past the end dropped), grows (new
// items unmeasured) and changes back, with positions landing on their item each time.
TEST_F(ListViewScrollTest, PositionsStayRightAsTheModelShrinksAndGrows) {
    run(std::string{variableHeight} + R"(
        made = {}
        n = 3000
        list = ListView { height = 200, spacing = 2, model = function() return n end,
                          delegate = function(i) made[i] = Bar { h = vh(i) }; return made[i] end })");
    settle("list");
    const float top = bar(1)->y();
    ListView& l = list();
    struct Case {
        std::int64_t count;
        std::int64_t index;
    };
    for (const Case c : {Case{3000, 2500}, Case{1200, 1100}, Case{5000, 4800}, Case{5001, 1800}, Case{10, 1}}) {
        SCOPED_TRACE(std::to_string(c.count) + " " + std::to_string(c.index));
        run("n = " + std::to_string(c.count) + "; list:positionViewAtIndex(" + std::to_string(c.index) + ")");
        EXPECT_LT(settle("list"), 25);
        Bar* b = bar(c.index);
        ASSERT_NE(b, nullptr);
        EXPECT_NEAR(b->y(), top, 1.0f);
        check("list.firstVisibleIndex == " + std::to_string(c.index));
        EXPECT_LE(l.contentHeight(), static_cast<float>(c.count) * (44.0f * 3.0f + 2.0f));
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// Items that take no space (hidden here; a delegate that returns nil alike) in a run longer than 256 aren't all visited
// on every frame, which cost as much as the run is long: a probe of 32 of them is, at an offset that moves each frame.
// One that is shown again appears once the probe has passed over it, at its place.
TEST_F(ListViewScrollTest, ALongRunOfHiddenItemsIsProbedNotWalked) {
    Watchdog watchdog{120.0};
    run(R"(
        made = {}
        reads = 0
        show = { [1] = true, [3000] = true }
        list = ListView { height = 200, spacing = 0, model = 3000,
                          delegate = function(i)
                              made[i] = Bar { h = 20, visible = function() reads = reads + 1; return show[i] == true end }
                              return made[i]
                          end })");
    settle("list");
    m_h.renderFrames("list", 2);
    ASSERT_NE(bar(3000), nullptr);
    const float top = bar(1)->y();
    EXPECT_FLOAT_EQ(bar(3000)->y(), top + 20.0f); // the 2998 between take no space
    run("reads = 0");
    m_h.renderFrame("list");
    check("reads > 0 and reads < 100"); // the visible two, the items of the first chunk, the probe: not 3000

    run("show[1500] = true");
    int frames = 0;
    for (; frames < 200 && (bar(1500) == nullptr || bar(1500)->renders() == 0); ++frames)
        m_h.renderFrame("list");
    EXPECT_LE(frames, 3000 / 32 + 2);
    settle("list");
    ASSERT_NE(bar(1500), nullptr);
    EXPECT_FLOAT_EQ(bar(1500)->y(), top + 20.0f);
    EXPECT_FLOAT_EQ(bar(3000)->y(), top + 40.0f);
    EXPECT_FLOAT_EQ(list().contentHeight(), 60.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// A model longer than ListView::maxItems (a numeric `math.huge`, a count gone wrong) shows its first maxItems items and
// is reported once: its measured heights would take gigabytes (a bad_alloc on every frame), and the step tree's counts
// would wrap past 2^32. With a fixed `itemHeight` too.
TEST_F(ListViewScrollTest, AModelLongerThanTheLimitIsCapped) {
    Watchdog watchdog{120.0};
    for (const char* heights : {"itemHeight = 20,", ""}) {
        SCOPED_TRACE(heights);
        luaui::clearErrors();
        run(std::string{R"(
            made = {}
            list = ListView { height = 200, spacing = 0, )"} +
            heights + R"( model = math.huge,
                              delegate = function(i) made[i] = Bar { h = 20 }; return made[i] end })");
        settle("list");
        ListView& l = list();
        EXPECT_EQ(l.renderedRange().first, 1);
        EXPECT_LT(l.renderedRange().second, 30);
        const auto cap = static_cast<double>(ListView::maxItems);
        EXPECT_NEAR(static_cast<double>(l.contentHeight()), cap * 20.0, cap * 20.0 * 1e-3);
        m_h.renderFrames("list", 2);
        const auto& errors = luaui::errors();
        ASSERT_EQ(errors.size(), 1u);
        EXPECT_NE(errors[0].find("more than " + std::to_string(ListView::maxItems) + " items"), std::string::npos)
            << errors[0];
        run("list.model = 1e9");
        m_h.renderFrame("list");
        EXPECT_EQ(luaui::errors().size(), 1u); // once per list
    }
    luaui::clearErrors();
}

// A delegate's handler that empties the model while the list renders makes __range return fewer delegates than asked
// for: that is the end of the model, not a run of items that take no space to walk, a chunk at a time, up to the count
// the render started with. The next frame shows the empty model, and items added after it show again.
TEST_F(ListViewScrollTest, AModelClearedWhileRenderingEndsTheRender) {
    Watchdog watchdog{120.0};
    m_feed.add(20000);
    run(R"(
        ranges = 0
        cleared = false
        list = ListView { height = 200, spacing = 0, model = feed.rows,
                          delegate = function(row)
                              if row == 3 then
                                  return Emitter { onRender = function()
                                      if not cleared then cleared = true; feed:clear() end
                                  end }
                              end
                              return Bar { h = 20 }
                          end }
        local range = list.__range
        list.__range = function(...) ranges = ranges + 1; return range(...) end)");
    m_h.renderFrame("list");
    check("cleared");
    check("ranges < 10");
    ListView& l = list();
    EXPECT_LT(l.renderedRange().second, 30); // the chunk asked for before the model was cleared, not 20000
    settle("list");
    EXPECT_EQ(l.renderedRange().second, -1);
    EXPECT_FLOAT_EQ(l.contentHeight(), 0.0f);
    m_feed.add(5);
    settle("list");
    EXPECT_EQ(l.renderedRange().first, 1);
    EXPECT_EQ(l.renderedRange().second, 5);
    EXPECT_TRUE(luaui::errors().empty());
}

// Measured heights are pixels: after a zoom they are scaled with it, so the content height (the scroll range) is right
// at once instead of mixing heights of two scales until every item rendered again.
TEST_F(ListViewScrollTest, MeasuredHeightsFollowAZoom) {
    run(R"(
        list = ListView { height = 200, spacing = 0, model = 1000,
                          delegate = function(i) return DpBar { h = 20 } end })");
    settle("list");
    run("list:positionViewAtEnd()");
    settle("list"); // every item from the top to the end was assumed 20px, those near the end measured
    ListView& l = list();
    EXPECT_NEAR(l.contentHeight(), 1000.0f * 20.0f, 1.0f);
    setZoom(2.0f);
    m_h.renderFrame("list");
    EXPECT_NEAR(l.contentHeight(), 1000.0f * 40.0f, 1.0f) << "the heights scaled with the zoom";
    setZoom(1.0f);
    m_h.renderFrame("list");
    EXPECT_NEAR(l.contentHeight(), 1000.0f * 20.0f, 1.0f);
    settle("list");
    EXPECT_NEAR(l.contentHeight(), 1000.0f * 20.0f, 1.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

} // namespace
