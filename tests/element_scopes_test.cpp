// Element id scopes and parent links (src/resources/imlui.lua), with the built-in types registered and rendered
// through the headless frame harness: per-instance `ids` of components and delegates, app-level ids as free names,
// weak ids, duplicate reports, and the read-only, weak `parent` of every element.
#include <gtest/gtest.h>

import std;

import imlui.lua.errors;
import imlui.test.headless_frame;
import imlui.test.render_counter;

using namespace imlui;

namespace {

class ElementScopesTest : public ::testing::Test {
protected:
    void SetUp() override { m_h.registerType<test::RenderCounter>("Probe"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    /// The errors reported since the harness started that contain `text`.
    static int reportedWith(std::string_view text) {
        int n = 0;
        for (const std::string& e : luaui::errors())
            if (e.find(text) != std::string::npos) ++n;
        return n;
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// ------------------------------------------------------------------ scopes

TEST_F(ElementScopesTest, TwoComponentInstancesBindTheirOwnIds) {
    run(R"(
        Field = component(function(props, ids)
            return Column {
                TextField { id = "field", text = props.text },
                Text { id = "echo", text = function() return ids.field.text end },
            }
        end)
        a = Field { text = "a" }
        b = Field { text = "b" }
        root = Column { a, b })");
    m_h.renderFrames("root", 2);
    run(R"(
        local function echo(inst) local t = inst[2]; return t.text(t) end
        assert(echo(a) == "a" and echo(b) == "b", echo(a) .. " " .. echo(b))
        a[1].text = "changed"
        assert(echo(a) == "changed" and echo(b) == "b", echo(a) .. " " .. echo(b))
        assert(ui.ids.field == nil and ui.ids.echo == nil, "component ids leaked to app level")
        assert(ui.env.field == nil, "a component id is a free name"))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, RepeaterAndListViewDelegatesGetTheirOwnIds) {
    run(R"(
        seen = {}
        local function row(item, index, ids)
            seen[#seen + 1] = { item = item, index = index, ids = ids }
            return Row {
                TextField { id = "f", text = item },
                Text { text = function() return ids.f.text end },
            }
        end
        root = Column {
            Repeater { model = { "x", "y" }, delegate = row },
            ListView { height = 100, model = { "z" }, delegate = row },
        })");
    m_h.renderFrames("root", 2);
    run(R"(
        assert(#seen == 3, "delegates: " .. #seen)
        assert(seen[1].index == 1 and seen[2].index == 2 and seen[3].index == 1)
        for _, s in ipairs(seen) do
            assert(type(s.ids) == "table" and s.ids.f ~= nil and s.ids.f.text == s.item)
        end
        assert(seen[1].ids ~= seen[2].ids and seen[1].ids.f ~= seen[2].ids.f)
        assert(ui.ids.f == nil, "a delegate id leaked to app level"))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, NestedComponentIdsStayInTheirInstance) {
    run(R"(
        Inner = component(function(props, ids)
            innerIds = ids
            return Column { TextField { id = "inner" } }
        end)
        Outer = component(function(props, ids)
            outerIds = ids
            return Column { Inner { id = "child" }, Text { id = "own" } }
        end)
        root = Outer { id = "outer" })");
    run(R"(
        assert(innerIds.inner ~= nil and outerIds.inner == nil, "the inner instance's id went to the outer scope")
        assert(outerIds.own ~= nil and innerIds.own == nil)
        assert(outerIds.child == root[1], "an id on a component instance names its root in the creating scope")
        assert(innerIds.child == nil)
        assert(ui.ids.outer == root and ui.ids.inner == nil and ui.ids.own == nil and ui.ids.child == nil))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, AppIdsAreFreeNamesInsideComponentsAndComponentIdsAreNot) {
    run(R"(
        TextField { id = "appField", text = "app" }
        Show = component(function(props, ids)
            return Column {
                TextField { id = "privateField", text = "private" },
                Text { text = function() return appField.text end },
                Text { text = function() return privateField end },
            }
        end)
        root = Column { appField, Show {} })");
    m_h.renderFrames("root", 2);
    run(R"(
        local shown = root[2]
        assert(shown[2].text(shown[2]) == "app")
        assert(shown[3].text(shown[3]) == nil, "a component id resolved as a free name"))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, ComponentIdsArentCheckedForCollisionsWithGlobals) {
    run(R"(
        C = component(function(props, ids) return Column { Text { id = "io" }, Text { id = "state" } } end)
        root = C {})");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
    run(R"(Text { id = "io" })"); // at app level it is a free name, so it is checked
    EXPECT_EQ(reportedWith("element id 'io' collides with Lua global 'io'"), 1);
}

TEST_F(ElementScopesTest, AThrowingComponentLeavesTheScopeStackIntact) {
    run(R"(
        Bad = component(function(props, ids)
            Text { id = "partial" }
            error("boom")
        end)
        Good = component(function(props, ids) goodIds = ids; return Text { id = "inside" } end)
        local ok, err = pcall(Bad, {})
        assert(not ok and tostring(err):find("boom", 1, true), tostring(err))
        local ok2 = pcall(Bad, {})
        assert(not ok2)
        local g = Good {}
        assert(goodIds.inside == g, "the next instance registers in its own scope")
        appText = Text { id = "partial" }
        assert(ui.ids.partial == appText, "an id after the failed instance registers at app level")
        assert(ui.ids.inside == nil))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, ADelegateThatRaisesLeavesTheScopeStackIntact) {
    run(R"(
        root = Column { Repeater { model = 1, delegate = function(item, index, ids)
            Text { id = "half" }
            error("delegate boom")
        end } })");
    m_h.renderFrame("root");
    EXPECT_EQ(reportedWith("delegate boom"), 1);
    run(R"(
        after = Text { id = "afterDelegate" }
        assert(ui.ids.afterDelegate == after and ui.ids.half == nil))");
}

TEST_F(ElementScopesTest, ALoaderBuildsItsItemInTheScopeItWasCreatedIn) {
    run(R"(
        Page = component(function(props, ids)
            pageIds = ids
            local built
            return Column { Loader { item = function()
                built = built or Text { id = "lazy" }
                return built
            end } }
        end)
        root = Column { Page {} })");
    m_h.renderFrames("root", 2);
    run(R"(
        assert(pageIds.lazy ~= nil, "an element the Loader built at render time isn't in its component's scope")
        assert(ui.ids.lazy == nil))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

// ------------------------------------------------------------------ weak ids

TEST_F(ElementScopesTest, AnIdOfARemovedElementReadsNilAndBindingsUsingItReportAnError) {
    run(R"(
        s = state { show = true }
        local field
        root = Column {
            Loader { item = function()
                if s.show then field = field or TextField { id = "gone", text = "hi" } else field = nil end
                return field
            end },
            Text { id = "echo", text = function() return gone.text end },
        })");
    m_h.renderFrames("root", 3);
    run("assert(echo.text(echo) == 'hi')");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();

    run("s.show = false");
    m_h.renderFrames("root", 2);
    run("collectgarbage(); assert(ui.ids.gone == nil, 'an id kept its removed element alive')");
    luaui::clearErrors();
    m_h.renderFrame("root");
    EXPECT_GE(reportedWith("gone"), 1); // a normal binding error, no crash
    m_h.renderFrame("root");
}

TEST_F(ElementScopesTest, ElementsInTheTreeOrStillBeingBuiltAreKept) {
    run(R"(
        TextField { id = "later", text = "x" }    -- only the id refers to it until the root uses it
        collectgarbage()
        root = Column { later, TextField { id = "inTree" } }
        assert(root[1] ~= nil and root[1].text == "x", "an element built for the tree was collected"))");
    m_h.renderFrames("root", 3);
    run(R"(
        collectgarbage()
        assert(ui.ids.later == root[1] and ui.ids.inTree == root[2], "the tree's elements were collected"))");
    // Once a frame has passed, an element nothing but its id refers to goes.
    run(R"(Text { id = "loose" })");
    m_h.renderFrame("root");
    run("collectgarbage(); assert(ui.ids.loose == nil)");
}

// ------------------------------------------------------------------ duplicates

TEST_F(ElementScopesTest, DuplicateIdsAreReportedOnceAndTheFirstKeepsTheId) {
    run(R"(
        first = Text { id = "dup" }
        second = Text { id = "dup" }
        assert(ui.ids.dup == first)
        ui.registerId(first) -- the same element again isn't a duplicate)");
    EXPECT_EQ(reportedWith("duplicate element id 'dup' in the app scope; the first element keeps it"), 1);
    EXPECT_EQ(luaui::errors().size(), 1u);

    run(R"(
        Twice = component(function(props, ids)
            local a, b = Text { id = "x" }, Text { id = "x" }
            assert(ids.x == a)
            return Column { a, b }
        end)
        one = Twice {})");
    EXPECT_EQ(reportedWith("duplicate element id 'x' in component ("), 1);
    EXPECT_EQ(luaui::errors().size(), 2u);
}

// ------------------------------------------------------------------ parent

TEST_F(ElementScopesTest, ParentIsTheElementThatLaysTheChildOut) {
    run(R"(
        leaf = Text { "leaf" }
        inner = Column { leaf }
        menu = Menu { MenuItem { text = "m" } }
        button = Button { text = "b", menu = menu }
        made = {}
        page = Text { "page" }
        other = Text { "other" }
        root = Row {
            inner,
            button,
            If { condition = true,
                 Repeater { model = 2, delegate = function(item) made[item] = Text {}; return made[item] end },
                 otherwise = other },
            Loader { item = page },
            ListView { id = "list", height = 50, model = 1, delegate = function() listRow = Text {}; return listRow end },
        }
        assert(root.parent == nil, "the root has no parent")
        assert(leaf.parent == inner and inner.parent == root)
        assert(button.parent == root and menu.parent == button and menu[1].parent == menu)
        assert(root[3].parent == root, "an inline container's parent")
        assert(root[3][1].parent == root, "a Repeater in an If is laid out by the If's parent")
        assert(other.parent == root, "otherwise"))");
    m_h.renderFrames("root", 2);
    run(R"(
        assert(made[1].parent == root and made[2].parent == root, "Repeater delegates: the visual parent")
        assert(page.parent == root, "a Loader's item")
        assert(listRow.parent == list, "ListView delegates: the ListView"))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

// A nil among positional children (a variable that isn't set yet): the renderer walks to rawlen and renders the
// children after it, and they get their parent too (the Lua side walked with ipairs, which stops at the hole).
TEST_F(ElementScopesTest, ChildrenAfterANilHoleGetTheirParent) {
    run(R"(
        local missing
        after = Probe {}
        nested = Probe {}
        root = Column { Probe {}, missing, after, If { condition = true, Probe {}, Probe {}, missing, nested } }
        assert(rawlen(root) == 4 and rawlen(root[4]) == 4, "the test needs the holes inside the border")
        assert(after.parent == root, "after a hole")
        assert(nested.parent == root, "after a hole in an inline container"))");
    m_h.renderFrame("root");
    EXPECT_GT(m_h.instance<test::RenderCounter>("after")->renders(), 0);
    EXPECT_GT(m_h.instance<test::RenderCounter>("nested")->renders(), 0);
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, AnInlineRootHoldsItsChildren) {
    run(R"(
        made = {}
        rep = Repeater { model = 1, delegate = function() made[1] = Probe {}; return made[1] end }
        shown = Probe {}
        load = Loader { item = shown })");
    m_h.renderFrame("rep");
    m_h.renderFrame("load");
    run("assert(made[1].parent == rep and shown.parent == load and rep.parent == nil)");
}

// `parent` lives in a side table, read through the type's shared metatable: assigning it stores a raw field, which
// the next sync drops (debug builds report it), so the parent stays the container's.
TEST_F(ElementScopesTest, ParentIsReadOnly) {
    run(R"(
        child = Text { "child" }
        root = Column { child }
        child.parent = Column {})");
    m_h.renderFrame("root");
    run("assert(child.parent == root and rawget(child, 'parent') == nil)");
#ifdef NDEBUG
    EXPECT_EQ(reportedWith("'parent' is read-only"), 0);
#else
    EXPECT_EQ(reportedWith("Text: 'parent' is read-only (its container sets it)"), 1);
#endif
    run("bad = Text { parent = 1 }; assert(bad.parent == nil)");
    EXPECT_EQ(reportedWith("Text: 'parent' is read-only (the element's container sets it); the value is ignored"), 1);
}

// No per-element metatables: elements of a type share the type's, the Lua-side containers included (whose instance
// is still created at the first render, not by the constructor).
TEST_F(ElementScopesTest, ElementsOfATypeShareOneMetatable) {
    run(R"(
        local a, b = Button { "a" }, Button { "b" }
        assert(getmetatable(a) ~= nil and getmetatable(a) == getmetatable(b))
        assert(getmetatable(a) == require("imlui.types").meta("Button"))
        assert(getmetatable(a) ~= getmetatable(Text {}))
        local function rep() return Repeater { model = 1, delegate = function() return Text {} end } end
        r1, r2 = rep(), rep()
        assert(getmetatable(r1) ~= nil and getmetatable(r1) == getmetatable(r2))
        assert(rawget(r1, "__native") == nil, "a Repeater's instance is made at its first render")
        local l1, l2 = ListView { model = 0, delegate = function() end }, ListView {}
        assert(getmetatable(l1) == getmetatable(l2))
        assert(getmetatable(If {}) == getmetatable(If {}) and getmetatable(Loader {}) == getmetatable(Loader {}))
        assert(getmetatable(Responsive {}) == getmetatable(Responsive {}))
        root = Column { r1 })");
    m_h.renderFrame("root");
    run("assert(getmetatable(r1) == getmetatable(r2))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

// ------------------------------------------------------------------ functions run in their element's scope

TEST_F(ElementScopesTest, AHandlerRegistersTheElementsItBuildsInItsInstance) {
    run(R"(
        Clicker = component(function(props, ids)
            props.out.ids = ids
            return Button { "Add", onClicked = function() props.out.made = Text { id = "made" } end }
        end)
        one, two = {}, {}
        root1 = Clicker { out = one }
        root2 = Clicker { out = two })");
    m_h.renderFrame("root1");
    m_h.click("root1", 20, 20);
    run(R"(
        assert(one.made ~= nil and one.ids.made == one.made, "the handler's element isn't in its instance's ids")
        assert(two.made == nil and two.ids.made == nil)
        assert(ui.ids.made == nil, "the handler's element went to app level"))");
    m_h.renderFrame("root2");
    m_h.click("root2", 20, 20);
    run("assert(two.ids.made == two.made and one.ids.made == one.made and one.made ~= two.made)");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, BindingsTimersWatchersAndSignalsOfTypedElementsRunInTheirScope) {
    run(R"(
        s = state { n = 0 }
        keep = {}
        Busy = component(function(props, ids)
            busyIds = ids
            ui.setTimeout(function() keep.timeout = Text { id = "fromTimeout" } end, 0)
            ui.watch(s, "n", function() keep.watch = keep.watch or Text { id = "fromWatch" } end)
            return Column {
                Text { text = function() keep.binding = keep.binding or Text { id = "fromBinding" }; return "x" end },
                Timer { interval = 50, onTriggered = function() keep.timer = keep.timer or Text { id = "fromTimer" } end },
            }
        end)
        root = Busy {})");
    m_h.renderFrames("root", 12);
    run("s.n = 1");
    run(R"(
        for _, id in ipairs { "fromTimeout", "fromWatch", "fromBinding", "fromTimer" } do
            assert(busyIds[id] ~= nil and busyIds[id] == keep[({ fromTimeout = "timeout", fromWatch = "watch",
                   fromBinding = "binding", fromTimer = "timer" })[id]], id .. " isn't in the instance's ids")
            assert(ui.ids[id] == nil, id .. " went to app level")
        end)");
    // An app-level watcher and timer stay at app level.
    run(R"(
        ui.watch(s, "n", function() keep.appWatch = Text { id = "appWatch" } end)
        s.n = 2
        assert(ui.ids.appWatch == keep.appWatch))");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, AHandlerThatRaisesLeavesTheScopeStackIntact) {
    run(R"(
        Failing = component(function(props, ids)
            failingIds = ids
            return Button { "Fail", onClicked = function() keepHalf = Text { id = "half" }; error("handler boom") end }
        end)
        root = Failing {})");
    m_h.renderFrame("root");
    m_h.click("root", 20, 20);
    EXPECT_EQ(reportedWith("handler boom"), 1);
    run(R"(
        assert(failingIds.half == keepHalf)
        after = Text { id = "afterHandler" }
        assert(ui.ids.afterHandler == after, "the scope of the failed handler stayed active"))");
}

TEST_F(ElementScopesTest, ParentLinksKeepOtherFieldsAndMethodsWorking) {
    run(R"(
        t = Timer { interval = 100 }
        root = Column { t }
        assert(t.parent == root)
        t:restart()                       -- the registered type's methods are still reachable
        root.spacing = 6                  -- new fields are stored on the element itself
        assert(rawget(root, "spacing") == 6)
        assert(root.nothing == nil))");
    m_h.renderFrames("root", 2);
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(ElementScopesTest, ParentIsAWeakReference) {
    run(R"(
        child = Text {}
        do local holder = Column { child } end
        collectgarbage()
        assert(child.parent == nil, "the parent link kept a dropped parent alive"))");
}
