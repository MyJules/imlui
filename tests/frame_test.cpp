// Tests for Frame (imlui.types.frame) as typed elements use it: child handles (visibility, non-visual
// children, last-frame sizes, as<T>() with synced properties), element-valued props, type aliases, subclass
// declarations (TypeBuilder::extends), re-registration under a name, and unknown element types. Renders headless
// ImGui frames (no window, no backend) through the test harness (tests/support/headless_frame.cppm).
#include <gtest/gtest.h>

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.ui.frame_request;
import imlui.types.script_object;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.types.registry;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

/// A fixed-size box; registered as "Leaf" and, as an alias, "Block".
class Leaf final : public Element {
public:
    static void declare(TypeBuilder<Leaf>& type) {
        type.property("w", &Leaf::w, &Leaf::setW).property("h", &Leaf::h, &Leaf::setH);
    }
    void render(Frame&) override { ImGui::Dummy({m_w, m_h}); }

    [[nodiscard]] float w() const { return m_w; }
    void setW(float w) { m_w = w; }
    [[nodiscard]] float h() const { return m_h; }
    void setH(float h) { m_h = h; }

private:
    float m_w = 1.0f;
    float m_h = 1.0f;
};

/// A fixed-size box that reports its size before rendering (Element::intrinsicSize).
class Sized final : public Element {
public:
    static void declare(TypeBuilder<Sized>&) {}
    void render(Frame&) override { ImGui::Dummy({12.0f, 8.0f}); }
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{12.0f, 8.0f}; }
};

/// A type that fills the width by default (TypeBuilder::fillsWidth): records what its Frame makes of `width`.
class Filler final : public Element {
public:
    static void declare(TypeBuilder<Filler>& type) { type.fillsWidth(); }
    void render(Frame& frame) override {
        size = frame.size("width");
        resolved = frame.resolvedWidth(7.0f);
        available = frame.available().x;
        ImGui::Dummy({resolved, 1.0f});
    }
    float size = 0.0f;
    float resolved = 0.0f;
    float available = 0.0f;
};

/// Records what its children's handles expect of their sizes before rendering them (Frame::Child::expectedSize).
class Expect final : public Element {
public:
    static void declare(TypeBuilder<Expect>&) {}
    void render(Frame& frame) override {
        expected.clear();
        frame.forEachChild([&](Frame::Child& child) {
            expected.push_back(child.expectedSize(100.0f));
            child.render();
        });
    }
    std::vector<std::optional<ImVec2>> expected;
};

/// Walks its children twice a frame and runs its `onBetween` handler between the passes (Lua that may change
/// anything): what each pass saw (type and whether it fills), then renders them.
class TwoPass final : public Element {
public:
    static void declare(TypeBuilder<TwoPass>&) {}
    void render(Frame& frame) override {
        passes.clear();
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<std::pair<std::string, bool>> seen;
            frame.forEachChild([&](Frame::Child& c) { seen.emplace_back(c.type(), c.fillsWidth()); });
            passes.push_back(std::move(seen));
            if (pass == 0) frame.node().emit("onBetween");
        }
        frame.renderChildren();
    }
    std::vector<std::vector<std::pair<std::string, bool>>> passes;
};

/// A non-visual element that counts its renders.
class Tick final : public Element {
public:
    static void declare(TypeBuilder<Tick>& type) { type.nonVisual(); }
    void render(Frame&) override { ++m_renders; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    int m_renders = 0;
};

/// What Row sees of one child.
struct Seen {
    std::string type;
    bool visual = false;
    float lastWidth = 0.0f;
    float lastHeight = 0.0f;
    float syncedWidth = -1.0f; ///< as<Leaf>()->w() before the child rendered, -1 if not a Leaf
};

/// A horizontal layout written against Frame only: places visible visual children side by side.
class Row final : public Element {
public:
    static void declare(TypeBuilder<Row>& type) { type.property("gap", &Row::gap, &Row::setGap); }

    void render(Frame& frame) override {
        seen.clear();
        headerRendered = frame.renderElementProp("header");
        const ImVec2 p = ImGui::GetCursorScreenPos();
        float x = p.x;
        frame.forEachChild([&](Frame::Child& child) {
            Seen s{child.type(), child.isVisual(), child.lastWidth(-1.0f), child.lastHeight(-1.0f)};
            if (const Leaf* leaf = child.as<Leaf>()) s.syncedWidth = leaf->w();
            seen.push_back(s);
            if (!child.isVisual()) {
                child.render();
                return;
            }
            ImGui::SetCursorScreenPos({x, p.y});
            child.render();
            x += ImGui::GetItemRectSize().x + m_gap;
        });
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy({std::max(1.0f, x - p.x), 1.0f});
    }

    [[nodiscard]] float gap() const { return m_gap; }
    void setGap(float gap) { m_gap = gap; }

    std::vector<Seen> seen;
    bool headerRendered = false;

private:
    float m_gap = 0.0f;
};

/// A base element and a subclass that extends its declaration (like TextField / TextArea).
class Field : public Element {
public:
    static void declare(TypeBuilder<Field>& type) {
        type.property("text", &Field::text, &Field::setText).method("clear", &Field::clear);
    }
    void render(Frame&) override {}

    [[nodiscard]] const std::string& text() const { return m_text; }
    void setText(std::string text) { m_text = std::move(text); }
    void clear() {
        m_text.clear();
        notify("text");
    }
    [[nodiscard]] bool multiline() const { return m_multiline; }

protected:
    bool m_multiline = false;

private:
    std::string m_text;
};

class Area final : public Field {
public:
    static void declare(TypeBuilder<Area>& type) {
        type.extends<Field>().property("rows", &Area::rows, &Area::setRows);
    }
    Area() { m_multiline = true; }

    [[nodiscard]] int rows() const { return m_rows; }
    void setRows(int rows) { m_rows = rows; }

private:
    int m_rows = 3;
};

class FrameTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Registered under "Leaf" first, like a built-in an app replaces: the later registration wins.
        m_h.registerType<Tick>("Leaf");
        m_h.registerType<Leaf>("Leaf");
        m_h.registerType<Leaf>("Block"); // alias
        m_h.registerType<Tick>("Tick");
        m_h.registerType<Row>("Row");
        m_h.registerType<Field>("Field");
        m_h.registerType<Area>("Area");
        m_h.registerType<Sized>("Sized");
        m_h.registerType<Filler>("Filler");
        m_h.registerType<Expect>("Expect");
        m_h.registerType<TwoPass>("TwoPass");
    }

    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    /// Renders the element held by `name` in one ImGui frame.
    void renderFrame(const char* name) { m_h.renderFrame(name); }

    template <class T>
    T* instance(const char* name) {
        return m_h.instance<T>(name);
    }

    test::HeadlessFrame m_h;
};

} // namespace

TEST_F(FrameTest, ChildHandlesSeeVisibleChildrenWithLastFrameSizes) {
    run(R"(
        root = Row { gap = 2, header = Leaf { w = 5, h = 5 },
            Leaf { w = 10, h = 20 },
            Block { w = 30, h = 4 },
            Leaf { w = 99, h = 99, visible = false },
            Tick { },
        })");
    renderFrame("root");
    Row* row = instance<Row>("root");
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->headerRendered);
    ASSERT_EQ(row->seen.size(), 3u); // the hidden Leaf is skipped
    EXPECT_EQ(row->seen[0].type, "Leaf");
    EXPECT_EQ(row->seen[1].type, "Block");
    EXPECT_EQ(row->seen[2].type, "Tick");
    EXPECT_TRUE(row->seen[0].visual);
    EXPECT_TRUE(row->seen[1].visual);
    EXPECT_FALSE(row->seen[2].visual);
    // First frame: no stored sizes yet, but as<T>() already synced the Lua values (the alias is a Leaf too).
    EXPECT_EQ(row->seen[0].lastWidth, -1.0f);
    EXPECT_EQ(row->seen[0].syncedWidth, 10.0f);
    EXPECT_EQ(row->seen[1].syncedWidth, 30.0f);
    EXPECT_EQ(row->seen[2].syncedWidth, -1.0f);

    renderFrame("root");
    ASSERT_EQ(row->seen.size(), 3u);
    EXPECT_EQ(row->seen[0].lastWidth, 10.0f);
    EXPECT_EQ(row->seen[0].lastHeight, 20.0f);
    EXPECT_EQ(row->seen[1].lastWidth, 30.0f);
    EXPECT_EQ(row->seen[1].lastHeight, 4.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FrameTest, NonVisualChildrenAreRendered) {
    run(R"(
        t = Tick { }
        root = Row { t })");
    renderFrame("root");
    renderFrame("root");
    EXPECT_EQ(instance<Tick>("t")->renders(), 2);
}

TEST_F(FrameTest, AsSyncsChangedPropertiesBeforeTheParentReadsThem) {
    run(R"(
        s = { w = 12 }
        root = Row { Leaf { w = function() return s.w end } })");
    renderFrame("root");
    EXPECT_EQ(instance<Row>("root")->seen[0].syncedWidth, 12.0f);
    run("s.w = 40");
    renderFrame("root");
    EXPECT_EQ(instance<Row>("root")->seen[0].syncedWidth, 40.0f);
    renderFrame("root");
    EXPECT_EQ(instance<Row>("root")->seen[0].lastWidth, 40.0f);
}

TEST_F(FrameTest, ALaterRegistrationReplacesATypeOfTheSameName) {
    run("root = Row { Leaf { w = 7 } }");
    renderFrame("root");
    renderFrame("root");
    ASSERT_EQ(instance<Row>("root")->seen.size(), 1u);
    EXPECT_TRUE(instance<Row>("root")->seen[0].visual);         // a Leaf, not the non-visual Tick
    EXPECT_EQ(instance<Row>("root")->seen[0].lastWidth, 7.0f); // the Leaf rendered
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FrameTest, UnknownElementTypesAreReported) {
    run("root = Row { Leaf { w = 7 }, Nope { } }");
    renderFrame("root");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("unknown element type 'Nope'"), std::string::npos);
    EXPECT_EQ(instance<Row>("root")->seen.size(), 2u); // still a child: it just doesn't render
}

TEST_F(FrameTest, AliasesAreSeparateNamesForOneClass) {
    run(R"(
        a = Leaf { w = 3 }
        b = Block { w = 4 }
        names = table.concat(ui.types(), ",")
        assert(names == "Area,Block,Expect,Field,Filler,Leaf,Row,Sized,Tick,TwoPass", names))");
    Leaf* a = instance<Leaf>("a");
    Leaf* b = instance<Leaf>("b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_EQ(a->w(), 3.0f);
    EXPECT_EQ(b->w(), 4.0f);
    ASSERT_NE(m_h.types().find("Block"), nullptr);
    EXPECT_EQ(m_h.types().find("Block")->name(), "Block");
    EXPECT_NE(m_h.types().find("Block"), m_h.types().find("Leaf"));
}

TEST_F(FrameTest, SubclassExtendsItsBaseDeclaration) {
    run(R"(
        f = Field { text = "one" }
        a = Area { text = "two", rows = 5 })");
    Field* f = instance<Field>("f");
    Area* a = instance<Area>("a");
    ASSERT_NE(f, nullptr);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(f->text(), "one");
    EXPECT_FALSE(f->multiline());
    EXPECT_EQ(a->text(), "two"); // the base's property
    EXPECT_EQ(a->rows(), 5);     // the subclass's own
    EXPECT_TRUE(a->multiline());
    EXPECT_EQ(m_h.types().find("Field")->findProperty("rows"), nullptr);
    run("a:clear()"); // the base's method, called on the subclass
    EXPECT_EQ(a->text(), "");
    run(R"(assert(a.text == "", "notify didn't write the value back"))");
}

// A child's expected size is its intrinsic size from the first frame; without one, its size on the last frame.
TEST_F(FrameTest, ExpectedSizesAreIntrinsicElseLastFrames) {
    run("root = Expect { Sized {}, Leaf { w = 30, h = 4 } }");
    renderFrame("root");
    Expect* e = instance<Expect>("root");
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(e->expected.size(), 2u);
    ASSERT_TRUE(e->expected[0]);
    EXPECT_EQ(e->expected[0]->x, 12.0f);
    EXPECT_EQ(e->expected[0]->y, 8.0f);
    EXPECT_FALSE(e->expected[1]); // its first frame
    renderFrame("root");
    ASSERT_TRUE(e->expected[1]);
    EXPECT_EQ(e->expected[1]->x, 30.0f);
    EXPECT_EQ(e->expected[1]->y, 4.0f);
}

// The passes over a container's children share what they learn of them on a frame (visible, kind, width, whether
// they fill), while nothing that may change tables runs: a handler between two passes makes the second see the
// children as they are now, a hidden child, a new width and a changed child list (also of a Repeater spliced in).
TEST_F(FrameTest, PassesOverChildrenSeeWhatAHandlerBetweenThemChanged) {
    using Seen = std::vector<std::pair<std::string, bool>>;
    run(R"(
        change = nil
        n = 1
        root = TwoPass {
            onBetween = function(self) if change then change(self) end end,
            Leaf { w = 1 }, Filler {}, Leaf { w = 2 },
            Repeater { model = function() return n end, delegate = function(i) return Block { w = i } end },
        })");
    renderFrame("root");
    TwoPass* t = instance<TwoPass>("root");
    ASSERT_NE(t, nullptr);
    const Seen before{{"Leaf", false}, {"Filler", true}, {"Leaf", false}, {"Block", false}};
    ASSERT_EQ(t->passes.size(), 2u);
    EXPECT_EQ(t->passes[0], before);
    EXPECT_EQ(t->passes[1], before);

    run(R"(change = function(self)
        change = nil
        self[1].visible = false
        self[2].width = 10
        self[3].fillWidth = true
        table.insert(self, 1, Block { w = 5 })
        n = 2
    end)");
    renderFrame("root");
    ASSERT_EQ(t->passes.size(), 2u);
    EXPECT_EQ(t->passes[0], before);
    const Seen after{{"Block", false}, {"Filler", false}, {"Leaf", true}, {"Block", false}, {"Block", false}};
    EXPECT_EQ(t->passes[1], after);

    renderFrame("root"); // and on the next frame
    EXPECT_EQ(t->passes[0], after);
    EXPECT_EQ(t->passes[1], after);
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

// The size policy lives in the type: an unset width of a type declared fillsWidth() is a fill (-1) for
// Frame::size() and resolvedWidth(); a width set on the element wins.
TEST_F(FrameTest, ATypeThatFillsByDefaultSeesAnUnsetWidthAsAFill) {
    run("root = Filler {}");
    renderFrame("root");
    Filler* f = instance<Filler>("root");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->size, -1.0f);
    EXPECT_GT(f->available, 100.0f);
    EXPECT_EQ(f->resolved, f->available);
    run("root = Filler { width = 40 }");
    renderFrame("root");
    f = instance<Filler>("root");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->size, 40.0f);
    EXPECT_EQ(f->resolved, 40.0f);
}
