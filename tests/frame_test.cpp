// Tests for Frame (imlui.types.frame) as typed elements use it: child handles (visibility, non-visual
// children, last-frame sizes, as<T>() with synced properties), element-valued props, type aliases, subclass
// declarations (TypeBuilder::extends) and typed types replacing handlers of the same name. Renders headless
// ImGui frames (no window, no backend) over lua/imlui.lua with a stub `imlui.native`.
#include <gtest/gtest.h>

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.ui.frame_request;
import imlui.ui.renderer;
import imlui.types.script_object;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.types.registry;

using namespace imlui;
using luaui::Node;

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
        luaui::clearErrors();
        m_context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // no backend: fonts needn't be prebuilt

        // A built-in-style handler of the same name, registered first, like the Engine does: the type replaces it.
        m_renderer.registerElement("Leaf", [this](Renderer&, const Node&) { ++m_lambdaRenders; });
        m_types.registerType<Leaf>("Leaf");
        m_types.registerType<Leaf>("Block"); // alias
        m_types.registerType<Tick>("Tick");
        m_types.registerType<Row>("Row");
        m_types.registerType<Field>("Field");
        m_types.registerType<Area>("Area");

        m_state = lua::newstate();
        lua::openlibs(m_state);
        m_types.install(m_state);
        run(std::string{"package.path = '"} + IMLUI_LUA_DIR + "/?.lua;' .. package.path\n" +
            R"(package.preload["imlui.native"] = function()
                   return { time = function() return 0 end, log = function() end }
               end
               ui = require("imlui")
               for _, n in ipairs({ "Leaf", "Block", "Tick", "Row", "Field", "Area" }) do _G[n] = ui.env[n] end)");
    }

    void TearDown() override {
        if (m_state) lua::close(m_state);
        ImGui::DestroyContext(m_context);
        luaui::clearErrors();
        clearFrameRequests();
    }

    void run(const std::string& code) {
        ASSERT_EQ(lua::loadbuffer(m_state, code, "=test"), lua::ok) << lua::tostring(m_state, -1);
        ASSERT_EQ(lua::pcall(m_state, 0, 0, 0), lua::ok) << lua::tostring(m_state, -1);
    }

    /// Renders the element in global `name` in one ImGui frame.
    void renderFrame(const char* name) {
        ImGui::NewFrame();
        ImGui::Begin("test");
        lua::getglobal(m_state, name);
        m_renderer.render(m_state, lua::gettop(m_state));
        lua::pop(m_state);
        ImGui::End();
        ImGui::Render();
    }

    template <class T>
    T* instance(const char* name) {
        lua::getglobal(m_state, name);
        Element* e = m_types.sync(Node{m_state, -1});
        lua::pop(m_state);
        return dynamic_cast<T*>(e);
    }

    ImGuiContext* m_context = nullptr;
    Renderer m_renderer;
    TypeRegistry m_types{m_renderer};
    lua::State* m_state = nullptr;
    int m_lambdaRenders = 0;
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

TEST_F(FrameTest, TypesReplaceHandlersOfTheSameName) {
    run("root = Row { Leaf { w = 7 } }");
    renderFrame("root");
    renderFrame("root");
    EXPECT_EQ(m_lambdaRenders, 0);
    EXPECT_EQ(instance<Row>("root")->seen[0].lastWidth, 7.0f); // the typed Leaf rendered
}

TEST_F(FrameTest, AliasesAreSeparateNamesForOneClass) {
    run(R"(
        a = Leaf { w = 3 }
        b = Block { w = 4 }
        names = table.concat(ui.types(), ","))");
    Leaf* a = instance<Leaf>("a");
    Leaf* b = instance<Leaf>("b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_EQ(a->w(), 3.0f);
    EXPECT_EQ(b->w(), 4.0f);
    lua::getglobal(m_state, "names");
    EXPECT_EQ(std::string{lua::tostring(m_state, -1)}, "Area,Block,Field,Leaf,Row,Tick");
    lua::pop(m_state);
    ASSERT_NE(m_types.find("Block"), nullptr);
    EXPECT_EQ(m_types.find("Block")->name(), "Block");
    EXPECT_NE(m_types.find("Block"), m_types.find("Leaf"));
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
    EXPECT_EQ(m_types.find("Field")->findProperty("rows"), nullptr);
    run("a:clear()"); // the base's method, called on the subclass
    EXPECT_EQ(a->text(), "");
    lua::getglobal(m_state, "a");
    lua::getfield(m_state, -1, "text");
    EXPECT_EQ(std::string{lua::tostring(m_state, -1)}, ""); // notify wrote it back
    lua::pop(m_state, 2);
}
