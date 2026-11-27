// Box: children layered over each other, each placed by its `anchor` and margins. Positions are measured with a probe
// element that records where it was placed; later layers render in child windows of their own, so the tests also
// check that a layer is clicked before what is under it (a Card included) and that a layer's size reaches the layout
// cache under the key the box reads (its placement settles). Renders headless ImGui frames
// (tests/support/headless_frame.cppm).
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.test.headless_frame;
import imlui.test.last_item_probe;
import imlui.ui.frame_request;

using namespace imlui;

namespace {

/// A w x h item that records where it was placed. With `intrinsic = true` it reports its size before rendering.
class Probe final : public Element {
public:
    static void declare(TypeBuilder<Probe>& type) {
        type.property("w", &Probe::w, &Probe::setW)
            .property("h", &Probe::h, &Probe::setH)
            .property("intrinsic", &Probe::intrinsic, &Probe::setIntrinsic);
    }
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override {
        if (!m_intrinsic) return std::nullopt;
        return ImVec2{m_w, m_h};
    }
    void render(Frame&) override {
        m_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy({m_w, m_h});
    }
    [[nodiscard]] float w() const { return m_w; }
    void setW(float w) { m_w = w; }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }
    [[nodiscard]] bool intrinsic() const { return m_intrinsic; }
    void setIntrinsic(bool intrinsic) { m_intrinsic = intrinsic; }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }

private:
    float m_w = 10.0f;
    float m_h = 10.0f;
    bool m_intrinsic = false;
    ImVec2 m_pos{-1.0f, -1.0f};
};

class BoxTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_h.registerType<Probe>("Probe");
        m_h.registerType<test::LastItemProbe>("Last");
        luaui::clearErrors();
    }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    Probe& probe(std::string_view name) {
        Probe* p = m_h.instance<Probe>(name);
        EXPECT_NE(p, nullptr) << name;
        static Probe none;
        return p ? *p : none;
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
    /// On-demand rendering, like the Engine: a first frame, then frames while one is due now (requested), at most `limit`.
    /// Returns how many it rendered.
    int renderWhileRequested(std::string_view root, int limit = 30) {
        m_h.renderFrame(root); // the first frame, which nothing requested
        int n = 1;
        for (; n < limit && secondsUntilFrame() == 0.0; ++n)
            m_h.renderFrame(root);
        return n;
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

TEST_F(BoxTest, EachAnchorPlacesItsChildInTheBox) {
    // Nine children, one per anchor, in a 300 x 200 box: the corners, the edges' middles and the center.
    run(R"(
        local function p(id, anchor) return Probe { id = id, w = 40, h = 20, intrinsic = true, anchor = anchor } end
        root = Last { id = "box", Box { width = 300, height = 200,
            p("tl", "topLeft"), p("t", "top"), p("tr", "topRight"),
            p("l", "left"), p("c", "center"), p("r", "right"),
            p("bl", "bottomLeft"), p("b", "bottom"), p("br", "bottomRight") } })");
    renderWhileRequested("root");
    const ImVec2 o = lastMin("box");
    EXPECT_FLOAT_EQ(lastSize("box").x, 300.0f);
    EXPECT_FLOAT_EQ(lastSize("box").y, 200.0f);
    const auto at = [&](const char* id, float x, float y) {
        EXPECT_FLOAT_EQ(probe(id).pos().x - o.x, x) << id;
        EXPECT_FLOAT_EQ(probe(id).pos().y - o.y, y) << id;
    };
    at("tl", 0, 0);
    at("t", 130, 0);
    at("tr", 260, 0);
    at("l", 0, 90);
    at("c", 130, 90);
    at("r", 260, 90);
    at("bl", 0, 180);
    at("b", 130, 180);
    at("br", 260, 180);
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(BoxTest, MarginsInsetTheAnchorAndASideOverridesMargin) {
    run(R"(
        root = Last { id = "box", Box { width = 300, height = 200,
            Probe { id = "tl", intrinsic = true, margin = 10 },
            Probe { id = "br", w = 40, h = 20, intrinsic = true, anchor = "bottomRight", margin = 10, marginRight = 30 },
            Probe { id = "c", w = 40, h = 20, intrinsic = true, anchor = "center", marginLeft = 100 } } })");
    renderWhileRequested("root");
    const ImVec2 o = lastMin("box");
    EXPECT_FLOAT_EQ(probe("tl").pos().x - o.x, 10.0f);
    EXPECT_FLOAT_EQ(probe("tl").pos().y - o.y, 10.0f);
    EXPECT_FLOAT_EQ(probe("br").pos().x - o.x, 300.0f - 30.0f - 40.0f);
    EXPECT_FLOAT_EQ(probe("br").pos().y - o.y, 200.0f - 10.0f - 20.0f);
    // Centered in what the left margin leaves: 100 + (200 - 40) / 2.
    EXPECT_FLOAT_EQ(probe("c").pos().x - o.x, 180.0f);
}

TEST_F(BoxTest, WithoutAHeightItIsTheTallestChildWithItsMargins) {
    run(R"(
        after = Probe { id = "after" }
        root = Column { spacing = 0,
            Last { id = "box", Box { width = 200,
                Probe { w = 50, h = 30, intrinsic = true },
                Probe { w = 50, h = 40, intrinsic = true, anchor = "bottom", marginTop = 15 } } },
            after })");
    renderWhileRequested("root");
    EXPECT_FLOAT_EQ(lastSize("box").y, 55.0f);
    EXPECT_FLOAT_EQ(probe("after").pos().y - lastMin("box").y, 55.0f); // what follows starts below it
}

TEST_F(BoxTest, AFillHeightTakesTheHeightLeftAndCentersInIt) {
    // The hello-world case: a box filling what is left of the window, a child in its center.
    run(R"(
        root = Column { spacing = 0, Probe { id = "top", w = 10, h = 100 },
            Last { id = "box", Box { height = -1, Probe { id = "c", w = 80, h = 20, intrinsic = true, anchor = "center" } } } })");
    renderWhileRequested("root");
    const ImVec2 box = lastSize("box"), o = lastMin("box");
    EXPECT_GT(box.y, 100.0f);
    EXPECT_FLOAT_EQ(probe("c").pos().x - o.x, std::floor((box.x - 80.0f) * 0.5f));
    EXPECT_FLOAT_EQ(probe("c").pos().y - o.y, std::floor((box.y - 20.0f) * 0.5f));
}

TEST_F(BoxTest, AChildWithoutAnIntrinsicSizeSettlesOnTheLastFramesSize) {
    // Not intrinsic: its first frame renders unseen, then it is placed by last frame's size. A later layer (in its own
    // child window) records its size under the key the box reads, so placement settles and the frames stop.
    run(R"(
        root = Last { id = "box", Box { width = 300, height = 200,
            Probe { id = "under", w = 300, h = 200 },
            Probe { id = "c", w = 60, h = 40, anchor = "center" } } })");
    const int frames = renderWhileRequested("root");
    EXPECT_LT(frames, 30) << "the placement never settled";
    const ImVec2 o = lastMin("box");
    EXPECT_FLOAT_EQ(probe("c").pos().x - o.x, 120.0f);
    EXPECT_FLOAT_EQ(probe("c").pos().y - o.y, 80.0f);
}

TEST_F(BoxTest, ALaterLayerIsClickedBeforeTheCardUnderIt) {
    // The card is a child window: an inline item after it would draw (and be hovered) under it. The badge's layer
    // has its own child window, begun after the card's, so the click reaches the badge.
    run(R"(
        clicks = 0
        root = Last { id = "box", Box { width = 300, height = 160,
            Card { height = 160, Text "A card under the badge" },
            Button { id = "badge", "Badge", anchor = "topRight", margin = 8,
                     onClicked = function() clicks = clicks + 1 end } } })");
    renderWhileRequested("root");
    test::LastItemProbe* box = m_h.instance<test::LastItemProbe>("box");
    ASSERT_NE(box, nullptr);
    // A point inside the badge: 8 dp in from the box's top-right corner, a few pixels further in.
    const ImVec2 o = box->min();
    m_h.click("root", o.x + 300.0f - 8.0f - 10.0f, o.y + 8.0f + 10.0f, 2);
    EXPECT_EQ(m_h.run("assert(clicks == 1, 'clicks: ' .. clicks)"), "");
}

TEST_F(BoxTest, ItsIntrinsicSizeIsTheOneItRendersAt) {
    run(R"(
        root = Column { spacing = 0,
            Row { spacing = 0, Last { id = "box", Box { width = 120,
                Probe { w = 50, h = 30, intrinsic = true },
                Probe { w = 20, h = 45, intrinsic = true, anchor = "right", margin = 5 } } }, Probe { id = "after" } } })");
    m_h.renderFrame("root"); // one frame: the row places `after` by the box's intrinsic size, not a guess
    EXPECT_FLOAT_EQ(lastSize("box").x, 120.0f);
    EXPECT_FLOAT_EQ(lastSize("box").y, 55.0f);
    EXPECT_GE(probe("after").pos().x - lastMin("box").x, 120.0f);
}

TEST_F(BoxTest, AnUnknownAnchorIsReported) {
    run(R"(root = Box { width = 100, height = 50, Probe { anchor = "middle" } })");
    m_h.renderFrame("root");
    ASSERT_FALSE(luaui::errors().empty());
    EXPECT_NE(luaui::errors().front().find("unknown anchor 'middle'"), std::string::npos) << luaui::errors().front();
}

// ------------------------------------------------------------ Column: height, justify, fill heights, side padding

TEST_F(BoxTest, ColumnJustifyPlacesTheChildrenInItsHeight) {
    run(R"(
        local function col(id, justify)
            return Last { id = id, Column { height = 200, spacing = 10, justify = justify,
                Probe { id = id .. "1", w = 20, h = 30, intrinsic = true },
                Probe { id = id .. "2", w = 20, h = 50, intrinsic = true } } }
        end
        root = Row { spacing = 0, col("start", "start"), col("center", "center"), col("end", "end"),
                     col("between", "spaceBetween") })");
    renderWhileRequested("root");
    for (const char* id : {"start", "center", "end", "between"})
        EXPECT_FLOAT_EQ(lastSize(id).y, 200.0f) << id; // as tall as its height, whatever it holds
    // 30 + 10 + 50 = 90 of content in 200: 110 free.
    const auto y = [&](const std::string& id) { return probe(id).pos().y - lastMin(id.substr(0, id.size() - 1)).y; };
    EXPECT_FLOAT_EQ(y("start1"), 0.0f);
    EXPECT_FLOAT_EQ(y("start2"), 40.0f);
    EXPECT_FLOAT_EQ(y("center1"), 55.0f);
    EXPECT_FLOAT_EQ(y("center2"), 95.0f);
    EXPECT_FLOAT_EQ(y("end1"), 110.0f);
    EXPECT_FLOAT_EQ(y("end2"), 150.0f);
    EXPECT_FLOAT_EQ(y("between1"), 0.0f);
    EXPECT_FLOAT_EQ(y("between2"), 150.0f); // the free space all in the one gap: at the bottom
}

TEST_F(BoxTest, ColumnFillHeightsShareWhatTheOthersLeave) {
    run(R"(
        root = Last { id = "col", Column { height = 300, spacing = 0,
            Probe { id = "top", w = 20, h = 60, intrinsic = true },
            Spacer { height = -1 },
            Probe { id = "mid", w = 20, h = 40, intrinsic = true },
            Spacer { height = -1 },
            Probe { id = "bottom", w = 20, h = 20, intrinsic = true } } })");
    renderWhileRequested("root");
    // 300 - 60 - 40 - 20 = 180 for the two fills: 90 each, so the last child ends at the column's bottom.
    const float y0 = lastMin("col").y;
    EXPECT_FLOAT_EQ(probe("top").pos().y - y0, 0.0f);
    EXPECT_FLOAT_EQ(probe("mid").pos().y - y0, 150.0f);
    EXPECT_FLOAT_EQ(probe("bottom").pos().y - y0, 280.0f);
    EXPECT_FLOAT_EQ(lastSize("col").y, 300.0f);
}

TEST_F(BoxTest, AFillHeightColumnCentersInWhatIsLeft) {
    run(R"(
        root = Column { spacing = 0, Probe { id = "above", w = 10, h = 100, intrinsic = true },
            Last { id = "col", Column { height = -1, justify = "center", align = "center",
                Probe { id = "c", w = 80, h = 20, intrinsic = true } } } })");
    renderWhileRequested("root");
    const ImVec2 col = lastSize("col"), o = lastMin("col");
    EXPECT_GT(col.y, 100.0f);
    EXPECT_NEAR(probe("c").pos().y - o.y, (col.y - 20.0f) * 0.5f, 1.0f);
    // `align` centers in the available width (the window's content), not in the column's own (auto) width.
    const float avail = 800.0f - 2.0f * ImGui::GetStyle().WindowPadding.x;
    EXPECT_NEAR(probe("c").pos().x - o.x, (avail - 80.0f) * 0.5f, 1.0f);
}

TEST_F(BoxTest, ColumnSidePaddingOverridesPadding) {
    run(R"(
        root = Last { id = "col", Column { padding = 4, paddingTop = 80, paddingLeft = 30,
            Probe { id = "p", w = 50, h = 20, intrinsic = true } } }
        m = Column { padding = 4, paddingTop = 80, paddingLeft = 30, Probe { w = 50, h = 20, intrinsic = true } })");
    renderWhileRequested("root");
    const ImVec2 o = lastMin("col");
    EXPECT_FLOAT_EQ(probe("p").pos().x - o.x, 30.0f);
    EXPECT_FLOAT_EQ(probe("p").pos().y - o.y, 80.0f);
    // 30 + 50 + 4 wide, 80 + 20 + 4 tall.
    EXPECT_FLOAT_EQ(lastSize("col").x, 84.0f);
    EXPECT_FLOAT_EQ(lastSize("col").y, 104.0f);
}

// ------------------------------------------------------------ hovered / onHoverChanged

TEST_F(BoxTest, HoveredFollowsTheMouseOnTheElementsThatAsk) {
    // `hovered = false` or an onHoverChanged handler asks for it; a card and the button inside are both hovered; a
    // bind() gets the write; leaving clears them, and each change calls the handler once.
    run(R"(
        s = state { cardHover = false }
        changes = {}
        root = Column { spacing = 0,
            Last { id = "card", Card { width = 300, hovered = bind(s, "cardHover"),
                Button { id = "btn", "Inside", hovered = false,
                    onHoverChanged = function(self, h) changes[#changes + 1] = h end } } },
            Probe { id = "below", w = 300, h = 200, intrinsic = true } })");
    renderWhileRequested("root");
    const ImVec2 card = lastMin("card");
    const auto moveTo = [&](float x, float y) {
        ASSERT_EQ(m_h.run("native.injectMouse(" + std::to_string(x) + ", " + std::to_string(y) + ")"), "");
        m_h.renderFrames("root", 3);
    };
    moveTo(card.x + 30.0f, card.y + 30.0f); // over the button, inside the card's padding
    EXPECT_EQ(m_h.run("assert(s.cardHover == true, 'card') assert(btn.hovered == true, 'btn')"), "");
    moveTo(card.x + 150.0f, probe("below").pos().y + 100.0f); // below the card
    EXPECT_EQ(m_h.run("assert(s.cardHover == false, 'card') assert(btn.hovered == false, 'btn')"), "");
    EXPECT_EQ(m_h.run("assert(#changes == 2 and changes[1] == true and changes[2] == false, #changes)"), "");
}

TEST_F(BoxTest, AnElementThatDoesntAskGetsNoHoveredField) {
    run(R"(root = Column { Button { id = "plain", "Plain" } })");
    m_h.renderFrame("root");
    ASSERT_EQ(m_h.run("native.injectMouse(20, 20)"), "");
    m_h.renderFrames("root", 3);
    EXPECT_EQ(m_h.run("assert(rawget(plain, 'hovered') == nil)"), "");
}
