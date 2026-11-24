// Tests for imlui.lua.node: reading and writing element properties from C++.
#include <gtest/gtest.h>

import lua;
import imlui.color;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.value;

using namespace imlui;
using luaui::Node;
using luaui::Value;

namespace {

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

class NodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_state = lua::newstate();
        lua::openlibs(m_state);
        luaui::clearErrors();
    }

    void TearDown() override {
        lua::close(m_state);
        luaui::clearErrors();
    }

    /// Runs `code`, which returns an element table, and wraps it. Records the stack
    /// top so tests can check that Node calls leave the stack balanced.
    Node element(std::string_view code) {
        EXPECT_EQ(lua::loadbuffer(m_state, code, "=element"), lua::ok) << lua::tostring(m_state, -1);
        EXPECT_EQ(lua::pcall(m_state, 0, 1, 0), lua::ok) << lua::tostring(m_state, -1);
        m_top = lua::gettop(m_state);
        return Node{m_state, -1};
    }

    void expectBalanced() const { EXPECT_EQ(lua::gettop(m_state), m_top); }

    /// Reads `table.key` with plain Lua indexing, as a string ("nil" when absent).
    std::string luaField(int idx, const char* table, const char* key) const {
        lua::getfield(m_state, idx, table);
        lua::getfield(m_state, -1, key);
        std::string r = lua::toStringAny(m_state, -1);
        lua::pop(m_state, 2);
        return r;
    }

    lua::State* m_state = nullptr;
    int m_top = 0;
};

} // namespace

// ------------------------------------------------------------------ reading

TEST_F(NodeTest, ReadsPlainValues) {
    Node n = element(R"(return { __type = "Button", text = "OK", width = 120.5, count = 3, enabled = false })");
    EXPECT_EQ(n.type(), "Button");
    EXPECT_EQ(n.string("text"), "OK");
    EXPECT_DOUBLE_EQ(n.number("width", 0), 120.5);
    EXPECT_FLOAT_EQ(n.numf("count", 0), 3.0f);
    EXPECT_EQ(n.string("count"), "3");
    EXPECT_FALSE(n.boolean("enabled", true));
    EXPECT_TRUE(n.has("text"));
    EXPECT_FALSE(n.has("missing"));
    expectBalanced();
}

TEST_F(NodeTest, MissingValuesUseDefaults) {
    Node n = element("return {}");
    EXPECT_FALSE(n.number("width").has_value());
    EXPECT_DOUBLE_EQ(n.number("width", 7.0), 7.0);
    EXPECT_TRUE(n.boolean("visible", true));
    EXPECT_EQ(n.string("text", "fallback"), "fallback");
    EXPECT_FALSE(n.color("color").has_value());
    expectBalanced();
}

TEST_F(NodeTest, NumericStringIsANumber) {
    Node n = element(R"(return { width = "42" })");
    ASSERT_TRUE(n.number("width").has_value());
    EXPECT_DOUBLE_EQ(*n.number("width"), 42.0);
    expectBalanced();
}

TEST_F(NodeTest, ReadsColors) {
    Node n = element(R"(return { hex = "#FF0000", rgb = { 0, 0.5, 1 }, rgba = { 1, 1, 1, 0.25 } })");
    EXPECT_EQ(n.color("hex"), (Color{1, 0, 0, 1}));
    EXPECT_EQ(n.color("rgb"), (Color{0, 0.5f, 1, 1}));
    EXPECT_EQ(n.color("rgba"), (Color{1, 1, 1, 0.25f}));
    expectBalanced();
}

TEST_F(NodeTest, FunctionBindingIsCalledWithSelf) {
    Node n = element(R"(
        return {
            prefix = "Hello",
            text = function(self) return self.prefix .. ", world" end,
            value = function() return 41 + 1 end,
            checked = function(self) return self.prefix == "Hello" end,
        })");
    EXPECT_EQ(n.string("text"), "Hello, world");
    EXPECT_DOUBLE_EQ(n.number("value", 0), 42.0);
    EXPECT_TRUE(n.boolean("checked", false));
    EXPECT_EQ(n.push("value"), lua::Type::Number);
    lua::pop(m_state);
    expectBalanced();
}

TEST_F(NodeTest, FunctionBindingIsReEvaluated) {
    Node n = element(R"(
        local count = 0
        return { value = function() count = count + 1; return count end })");
    EXPECT_DOUBLE_EQ(n.number("value", 0), 1.0);
    EXPECT_DOUBLE_EQ(n.number("value", 0), 2.0);
    expectBalanced();
}

TEST_F(NodeTest, PushRawDoesNotResolve) {
    Node n = element(R"(return { text = function() return "x" end, value = { __bind = true, t = {}, k = "v" } })");
    EXPECT_EQ(n.pushRaw("text"), lua::Type::Function);
    lua::pop(m_state);
    EXPECT_EQ(n.pushRaw("value"), lua::Type::Table);
    lua::pop(m_state);
    expectBalanced();
}

TEST_F(NodeTest, TwoWayTableBindReadsSource) {
    Node n = element(R"(
        local s = { volume = 0.75, name = "Ada" }
        return { value = { __bind = true, t = s, k = "volume" }, label = { __bind = true, t = s, k = "name" } })");
    EXPECT_DOUBLE_EQ(n.number("value", 0), 0.75);
    EXPECT_EQ(n.string("label"), "Ada");
    expectBalanced();
}

TEST_F(NodeTest, GetterBindReadsThroughGetter) {
    Node n = element(R"(
        local box = { v = "from getter" }
        return { text = { __bind = true, get = function() return box.v end, set = function(x) box.v = x end } })");
    EXPECT_EQ(n.string("text"), "from getter");
    expectBalanced();
}

TEST_F(NodeTest, BindWithoutSourceOrGetterIsNil) {
    Node n = element(R"(return { text = { __bind = true } })");
    EXPECT_EQ(n.push("text"), lua::Type::Nil);
    lua::pop(m_state);
    EXPECT_EQ(n.string("text", "def"), "def");
    expectBalanced();
}

TEST_F(NodeTest, TextFallsBackToPositionalString) {
    Node labelled = element(R"(return { "Positional", text = "Named" })");
    EXPECT_EQ(labelled.text(), "Named");
    lua::pop(m_state);

    Node positional = element(R"(return { "Positional" })");
    EXPECT_EQ(positional.text(), "Positional");
    lua::pop(m_state);

    Node number = element(R"(return { 12 })");
    EXPECT_EQ(number.text(), "12");
    lua::pop(m_state);

    Node child = element(R"(return { { __type = "Child" } })");
    EXPECT_EQ(child.text(), "");
    lua::pop(m_state);

    Node otherKey = element(R"(return { "Positional", title = "Title" })");
    EXPECT_EQ(otherKey.text("title"), "Title");
    EXPECT_EQ(otherKey.text("subtitle"), "Positional");
    expectBalanced();
}

TEST_F(NodeTest, StringsAndFloats) {
    Node n = element(R"(
        return {
            model = { "a", 2, true },
            data = { 1, 2.5, -3 },
            bound = function() return { "x", "y" } end,
            notTable = "abc",
        })");
    EXPECT_EQ(n.strings("model"), (std::vector<std::string>{"a", "2", "true"}));
    EXPECT_EQ(n.floats("data"), (std::vector<float>{1.0f, 2.5f, -3.0f}));
    EXPECT_EQ(n.strings("bound"), (std::vector<std::string>{"x", "y"}));
    EXPECT_TRUE(n.strings("notTable").empty());
    EXPECT_TRUE(n.floats("missing").empty());
    expectBalanced();
}

// ------------------------------------------------------------------ writing

TEST_F(NodeTest, SetStoresPlainValueOnElement) {
    Node n = element(R"(return { text = "old" })");
    n.set("text", Value{std::string{"new"}});
    n.set("count", Value{5.0});
    n.set("on", Value{true});
    EXPECT_EQ(n.string("text"), "new");
    EXPECT_DOUBLE_EQ(n.number("count", 0), 5.0);
    EXPECT_TRUE(n.boolean("on", false));
    n.set("text", Value{});
    EXPECT_FALSE(n.has("text"));
    expectBalanced();
}

TEST_F(NodeTest, SetWritesThroughTableBind) {
    Node n = element(R"(
        local s = { volume = 1 }
        return { source = s, value = { __bind = true, t = s, k = "volume" } })");
    n.set("value", Value{0.25});
    EXPECT_EQ(luaField(n.index(), "source", "volume"), "0.25");
    // The bind object itself is kept.
    EXPECT_EQ(n.pushRaw("value"), lua::Type::Table);
    lua::pop(m_state);
    EXPECT_DOUBLE_EQ(n.number("value", 0), 0.25);
    expectBalanced();
}

TEST_F(NodeTest, SetWritesThroughSetter) {
    Node n = element(R"(
        local box = { v = "a" }
        return { box = box, text = { __bind = true, get = function() return box.v end, set = function(x) box.v = x end } })");
    n.set("text", Value{std::string{"b"}});
    EXPECT_EQ(luaField(n.index(), "box", "v"), "b");
    EXPECT_EQ(n.string("text"), "b");
    expectBalanced();
}

TEST_F(NodeTest, SetOnReadOnlyBindIsIgnored) {
    Node n = element(R"(return { text = { __bind = true, get = function() return "fixed" end } })");
    n.set("text", Value{std::string{"changed"}});
    EXPECT_EQ(n.string("text"), "fixed");
    EXPECT_TRUE(luaui::errors().empty());
    expectBalanced();
}

TEST_F(NodeTest, SetLeavesFunctionBindingAlone) {
    Node n = element(R"(return { text = function() return "computed" end })");
    n.set("text", Value{std::string{"overwritten"}});
    EXPECT_EQ(n.pushRaw("text"), lua::Type::Function);
    lua::pop(m_state);
    EXPECT_EQ(n.string("text"), "computed");
    expectBalanced();
}

// ------------------------------------------------------------------ signals

TEST_F(NodeTest, EmitCallsHandlerWithSelfAndArgs) {
    Node n = element(R"(
        local e = { __type = "Slider" }
        e.onMoved = function(self, value, label, flag)
            self.isSelf = (self == e)
            self.gotValue, self.gotLabel, self.gotFlag = value, label, flag
            self.argCount = select("#", value, label, flag)
        end
        return e)");
    EXPECT_TRUE(n.emit("onMoved", {Value{0.5}, Value{std::string{"half"}}, Value{true}}));
    EXPECT_TRUE(n.boolean("isSelf", false));
    EXPECT_DOUBLE_EQ(n.number("gotValue", 0), 0.5);
    EXPECT_EQ(n.string("gotLabel"), "half");
    EXPECT_TRUE(n.boolean("gotFlag", false));
    expectBalanced();
}

TEST_F(NodeTest, EmitWithoutArgsPassesOnlySelf) {
    Node n = element(R"(return { onClicked = function(self, ...) self.clicks = (self.clicks or 0) + 1; self.extra = select("#", ...) end })");
    EXPECT_TRUE(n.emit("onClicked"));
    EXPECT_TRUE(n.emit("onClicked"));
    EXPECT_DOUBLE_EQ(n.number("clicks", 0), 2.0);
    EXPECT_DOUBLE_EQ(n.number("extra", -1), 0.0);
    expectBalanced();
}

TEST_F(NodeTest, EmitWithoutHandlerReturnsFalse) {
    Node n = element(R"(return { onClicked = "not a function" })");
    EXPECT_FALSE(n.emit("onClicked"));
    EXPECT_FALSE(n.emit("onMissing"));
    EXPECT_TRUE(luaui::errors().empty());
    expectBalanced();
}

TEST_F(NodeTest, FailingHandlerIsReported) {
    Node n = element(R"(return { onClicked = function() error("handler exploded") end })");
    EXPECT_FALSE(n.emit("onClicked", {Value{1.0}}));
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_TRUE(contains(luaui::errors()[0], "handler exploded")) << luaui::errors()[0];
    expectBalanced();
}

TEST_F(NodeTest, FailingBindingIsReportedAndReadsAsNil) {
    Node n = element(R"(return { text = function() error("binding exploded") end })");
    EXPECT_EQ(n.string("text", "def"), "def");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_TRUE(contains(luaui::errors()[0], "binding exploded")) << luaui::errors()[0];
    expectBalanced();
}

TEST_F(NodeTest, FailingGetterAndSetterAreReported) {
    Node n = element(R"(
        return { value = { __bind = true, get = function() error("getter exploded") end,
                                          set = function() error("setter exploded") end } })");
    EXPECT_FALSE(n.number("value").has_value());
    n.set("value", Value{1.0});
    ASSERT_EQ(luaui::errors().size(), 2u);
    EXPECT_TRUE(contains(luaui::errors()[0], "getter exploded")) << luaui::errors()[0];
    EXPECT_TRUE(contains(luaui::errors()[1], "setter exploded")) << luaui::errors()[1];
    expectBalanced();
}

TEST_F(NodeTest, ColorFromTableDefaultsAlphaToOne) {
    element("return { 0.1, 0.2, 0.3 }");
    EXPECT_EQ(Node::colorFromTable(m_state, -1), (Color{0.1f, 0.2f, 0.3f, 1.0f}));
    expectBalanced();
}

TEST_F(NodeTest, ColorFromTableNeedsRedGreenAndBlue) {
    for (const char* code : {"return {}", "return { 'x' }", "return { 0.1, 0.2 }", "return { 0.1, 'g', 0.3 }"}) {
        element(code);
        EXPECT_FALSE(Node::colorFromTable(m_state, -1).has_value()) << code;
        expectBalanced();
        lua::pop(m_state);
    }
    Node n = element("return { color = { 1, 0 } }");
    EXPECT_FALSE(n.color("color").has_value());
    expectBalanced();
}

// ------------------------------------------------------------------ errors in bind sources

// A two-way bind's source is read and written with its metamethods (a ui.state proxy's __newindex runs the
// watchers). An error in one is reported, not raised: unprotected, it would reach Lua's panic handler (abort).
TEST_F(NodeTest, BindSourceMetamethodErrorsAreReportedNotRaised) {
    Node n = element(R"(
        local source = setmetatable({}, {
            __index = function() error("index exploded") end,
            __newindex = function() error("newindex exploded") end })
        return { value = { __bind = true, t = source, k = "v" } })");
    EXPECT_FALSE(n.number("value").has_value());
    expectBalanced();
    n.set("value", Value{1.0});
    expectBalanced();
    ASSERT_EQ(luaui::errors().size(), 2u);
    EXPECT_TRUE(contains(luaui::errors()[0], "index exploded")) << luaui::errors()[0];
    EXPECT_TRUE(contains(luaui::errors()[1], "newindex exploded")) << luaui::errors()[1];
}

// Like Lua's resolve(), any truthy `t` is a source, indexed with its metamethods (a string's are the string
// library's); one that can't be indexed reports an error.
TEST_F(NodeTest, BindSourcesNeedNotBeTables) {
    Node n = element(R"(return { len = { __bind = true, t = "abc", k = "len" },
                                 bad = { __bind = true, t = 5, k = "v" } })");
    EXPECT_EQ(n.push("len"), lua::Type::Function); // ("abc").len is string.len
    lua::pop(m_state);
    EXPECT_FALSE(n.number("bad").has_value());
    n.set("bad", Value{2.0});
    expectBalanced();
    ASSERT_EQ(luaui::errors().size(), 1u); // the read's and the write's errors are the same message, recorded once
    EXPECT_TRUE(contains(luaui::errors()[0], "attempt to index a number value")) << luaui::errors()[0];
}

// ------------------------------------------------------------------ pushValue

TEST_F(NodeTest, PushValueKeepsNonIntegralNumbersFloat) {
    const double inf = std::numeric_limits<double>::infinity();
    for (double d : {std::numeric_limits<double>::quiet_NaN(), inf, -inf, 1e300, -1e300, 9223372036854775808.0, 0.5}) {
        luaui::pushValue(m_state, Value{d});
        EXPECT_FALSE(lua::isinteger(m_state, -1)) << d;
        const double back = lua::tonumber(m_state, -1);
        if (d != d) {
            EXPECT_NE(back, back); // NaN
        } else {
            EXPECT_EQ(back, d);
        }
        lua::pop(m_state);
    }
    luaui::pushValue(m_state, Value{-9223372036854775808.0}); // the smallest Integer is exact
    EXPECT_TRUE(lua::isinteger(m_state, -1));
    lua::pop(m_state);
    luaui::pushValue(m_state, Value{42.0});
    EXPECT_TRUE(lua::isinteger(m_state, -1));
    EXPECT_EQ(lua::tointeger(m_state, -1), 42);
    lua::pop(m_state);
}

// A __tostring that raises is reported for that value; the read returns "" instead of raising.
TEST_F(NodeTest, ARaisingToStringIsReportedNotRaised) {
    Node n = element(R"(
        local bad = setmetatable({}, { __tostring = function() error("tostring exploded") end })
        local named = setmetatable({}, { __tostring = function() return "named" end })
        return { __type = "List", label = bad, title = named, items = { "a", bad, named, 4 } })");
    EXPECT_EQ(n.string("label", "def"), "");
    EXPECT_EQ(n.string("title"), "named");
    EXPECT_EQ(n.strings("items"), (std::vector<std::string>{"a", "", "named", "4"}));
    expectBalanced();
    ASSERT_FALSE(luaui::errors().empty());
    EXPECT_TRUE(contains(luaui::errors().front(), "tostring exploded"));
}

// A bind object is recognized by its raw fields: a table whose __index raises is a plain value.
TEST_F(NodeTest, BindDetectionDoesntRunIndexMetamethods) {
    Node n = element(R"(
        local raising = setmetatable({}, { __index = function() error("index exploded") end })
        return { __type = "Text", value = raising })");
    EXPECT_EQ(n.push("value"), lua::Type::Table);
    lua::pop(m_state);
    n.set("value", Value{2.0});
    expectBalanced();
    EXPECT_TRUE(luaui::errors().empty());
}
