// Tests for the C++ type system (imlui.types.*): LuaConvert, TypeBuilder dispatch, property sync,
// notify/emit, instance lifetime, singletons and reinstalling into a new Lua state (hot reload).
// Runs lua/imlui.lua in a plain Lua state with a stub `imlui.native`; no window or ImGui frame.
#include <gtest/gtest.h>

import lua;
import imlui.color;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.ui.frame_request;
import imlui.ui.renderer;
import imlui.types.lua_convert;
import imlui.types.object;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_info;
import imlui.types.type_builder;
import imlui.types.registry;

using namespace imlui;
using luaui::Node;

namespace {

struct Counters {
    int constructed = 0;
    int destructed = 0;
    int destroyedHook = 0;
    int setValueCalls = 0;
};
Counters counters;

class Probe final : public Element {
public:
    static void declare(TypeBuilder<Probe>& type) {
        type.property("value", &Probe::value, &Probe::setValue)
            .property("name", &Probe::name, &Probe::setName)
            .property("tint", &Probe::tint, &Probe::setTint)
            .property("limit", &Probe::limit, &Probe::setLimit)
            .property("items", &Probe::items, &Probe::setItems)
            .property("setCount", &Probe::setCount)
            .signal("onFired")
            .method("add", &Probe::add)
            .method("greet", &Probe::greet)
            .method("fire", &Probe::fire)
            .method("bump", &Probe::bump)
            .defaultProperty("value");
    }

    Probe() { ++counters.constructed; }
    ~Probe() override { ++counters.destructed; }
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    Probe(Probe&&) = delete;
    Probe& operator=(Probe&&) = delete;

    void render(Frame&) override {}
    void destroyed() override { ++counters.destroyedHook; }

    [[nodiscard]] double value() const { return m_value; }
    void setValue(double v) {
        m_value = v;
        ++counters.setValueCalls;
    }
    [[nodiscard]] const std::string& name() const { return m_name; }
    void setName(std::string n) { m_name = std::move(n); }
    [[nodiscard]] Color tint() const { return m_tint; }
    void setTint(Color c) { m_tint = c; }
    [[nodiscard]] std::optional<int> limit() const { return m_limit; }
    void setLimit(std::optional<int> l) { m_limit = l; }
    [[nodiscard]] const std::vector<std::string>& items() const { return m_items; }
    void setItems(std::vector<std::string> i) { m_items = std::move(i); }
    [[nodiscard]] int setCount() const { return counters.setValueCalls; }

    [[nodiscard]] double add(double a, int b) const { return m_value + a + b; }
    [[nodiscard]] std::string greet(const std::string& who) const { return "hello " + who + " from " + m_name; }
    void fire() { emit("onFired", 7, "seven"); }
    void bump() {
        m_value += 1;
        notify("value");
    }

private:
    double m_value = 0;
    std::string m_name = "probe";
    Color m_tint{};
    std::optional<int> m_limit;
    std::vector<std::string> m_items;
};

class Backend final : public Object {
public:
    static void declare(TypeBuilder<Backend>& type) {
        type.property("level", &Backend::level, &Backend::setLevel)
            .property("label", &Backend::label)
            .signal("onPing")
            .method("ping", &Backend::ping);
    }

    [[nodiscard]] double level() const { return m_level; }
    void setLevel(double v) {
        m_level = v;
        notify("level");
    }
    [[nodiscard]] std::string label() const { return "backend"; }
    int ping(int n) {
        emit("onPing", n, "pong");
        return n * 2;
    }

private:
    double m_level = 0.5;
};

lua::State* newUiState(TypeRegistry& types) {
    lua::State* state = lua::newstate();
    lua::openlibs(state);
    types.install(state);
    const std::string boot = std::string{"package.path = '"} + IMLUI_LUA_DIR + "/?.lua;' .. package.path\n" +
                             R"(package.preload["imlui.native"] = function()
                                    return { time = function() return 0 end, log = function() end }
                                end
                                ui = require("imlui")
                                Probe = ui.env.Probe)";
    EXPECT_EQ(lua::loadbuffer(state, boot, "=boot"), lua::ok) << lua::tostring(state, -1);
    EXPECT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
    return state;
}

class TypesTest : public ::testing::Test {
protected:
    void SetUp() override {
        counters = {};
        luaui::clearErrors();
        m_types.registerType<Probe>("Probe");
        m_types.registerSingleton("backend", m_backend);
        m_state = newUiState(m_types);
    }

    void TearDown() override {
        if (m_state) lua::close(m_state);
        luaui::clearErrors();
        clearFrameRequests();
    }

    /// Runs `code` in `state`; fails the test on any Lua error.
    static void run(lua::State* state, std::string_view code) {
        ASSERT_EQ(lua::loadbuffer(state, code, "=test"), lua::ok) << lua::tostring(state, -1);
        ASSERT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
    }
    void run(std::string_view code) { run(m_state, code); }

    /// Runs `code` and returns the error message it raised ("" if none).
    std::string runError(std::string_view code) {
        EXPECT_EQ(lua::loadbuffer(m_state, code, "=test"), lua::ok);
        if (lua::pcall(m_state, 0, 0, 0) == lua::ok) return {};
        std::string msg{lua::tostring(m_state, -1)};
        lua::pop(m_state);
        return msg;
    }

    /// Syncs the element in global `name` and returns its instance.
    Probe* sync(const char* name) {
        lua::getglobal(m_state, name);
        Element* e = m_types.sync(Node{m_state, -1});
        lua::pop(m_state);
        return dynamic_cast<Probe*>(e);
    }

    Renderer m_renderer;
    TypeRegistry m_types{m_renderer};
    Backend m_backend;
    lua::State* m_state = nullptr;
};

/// Pushes v, reads it back through LuaConvert<T>, and pops it.
template <class T>
T roundTrip(lua::State* state, const T& v) {
    LuaConvert<T>::push(state, v);
    EXPECT_TRUE(LuaConvert<T>::check(state, -1));
    T out = LuaConvert<T>::get(state, -1);
    lua::pop(state);
    return out;
}

} // namespace

// ------------------------------------------------------------------ LuaConvert

TEST(LuaConvertTest, RoundTrips) {
    lua::State* s = lua::newstate();
    EXPECT_EQ(roundTrip(s, true), true);
    EXPECT_EQ(roundTrip(s, false), false);
    EXPECT_EQ(roundTrip(s, -42), -42);
    EXPECT_EQ(roundTrip<unsigned char>(s, 200), 200);
    EXPECT_EQ(roundTrip<long long>(s, 1LL << 40), 1LL << 40);
    EXPECT_DOUBLE_EQ(roundTrip(s, 0.25), 0.25);
    EXPECT_FLOAT_EQ(roundTrip(s, 1.5f), 1.5f);
    EXPECT_EQ(roundTrip<std::string>(s, "hi"), "hi");
    EXPECT_EQ(roundTrip(s, Color{1, 0, 0, 1}), (Color{1, 0, 0, 1}));
    EXPECT_EQ(roundTrip(s, std::optional<int>{}), std::nullopt);
    EXPECT_EQ(roundTrip(s, std::optional<int>{3}), 3);
    EXPECT_EQ(roundTrip(s, std::vector<int>{1, 2, 3}), (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(roundTrip(s, std::vector<std::string>{"a", "b"}), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(lua::gettop(s), 0);
    lua::close(s);
}

TEST(LuaConvertTest, ChecksAndCoerces) {
    lua::State* s = lua::newstate();
    lua::pushstring(s, "12");
    EXPECT_TRUE(LuaConvert<int>::check(s, -1)); // numeric strings convert, like Node::number
    EXPECT_EQ(LuaConvert<int>::get(s, -1), 12);
    lua::pushnumber(s, 2.9);
    EXPECT_EQ(LuaConvert<int>::get(s, -1), 2);
    lua::pushnumber(s, 1e12);
    EXPECT_EQ(LuaConvert<int>::get(s, -1), std::numeric_limits<int>::max()); // clamped
    lua::pushinteger(s, -5);
    EXPECT_EQ(LuaConvert<unsigned>::get(s, -1), 0u);
    lua::pushinteger(s, 7);
    EXPECT_TRUE(LuaConvert<std::string>::check(s, -1));
    EXPECT_EQ(LuaConvert<std::string>::get(s, -1), "7");
    lua::newtable(s);
    EXPECT_FALSE(LuaConvert<double>::check(s, -1));
    EXPECT_FALSE(LuaConvert<std::string>::check(s, -1));
    lua::pushstring(s, "not a color");
    EXPECT_FALSE(LuaConvert<Color>::check(s, -1));
    lua::pushstring(s, "#00FF00");
    EXPECT_EQ(LuaConvert<Color>::get(s, -1), (Color{0, 1, 0, 1}));
    lua::settop(s, 0);
    LuaConvert<std::string_view>::push(s, "view");
    EXPECT_EQ(lua::tostring(s, -1), "view");
    lua::close(s);
}

// ------------------------------------------------------------------ construction & methods

TEST_F(TypesTest, ConstructorCreatesTheInstanceImmediately) {
    run(R"(
        p = Probe { name = "first" }
        assert(p.__type == "Probe")
        assert(type(p.__native) == "userdata", "no __native")
        assert(ui.env.Probe == Probe)
        local names = ui.types()
        assert(#names == 1 and names[1] == "Probe", table.concat(names, ","))
    )");
    EXPECT_EQ(counters.constructed, 1);
}

TEST_F(TypesTest, MethodsConvertArgumentsAndResults) {
    run(R"(
        p = Probe { name = "P" }
        assert(p:add(1.5, 2) == 3.5)
        assert(p:greet("you") == "hello you from probe") -- not synced yet: C++ default name
    )");
    Probe* p = sync("p");
    ASSERT_NE(p, nullptr);
    run(R"(assert(p:greet("you") == "hello you from P"))");
    EXPECT_NE(runError(R"(p:add("x", 1))").find("bad argument #1 to 'Probe:add' (number expected, got string)"),
              std::string::npos);
    EXPECT_NE(runError(R"(p.add({}, 1, 2))").find("method called without its object"), std::string::npos);
    EXPECT_NE(runError(R"(p:missing())").find("attempt to call a nil value (method 'missing')"), std::string::npos);
}

// ------------------------------------------------------------------ property sync

TEST_F(TypesTest, SyncReadsPlainFunctionAndBindValues) {
    run(R"(
        s = ui.state { v = 3, name = "bound" }
        plain = Probe { value = 1.5, name = "plain", tint = "#FF0000", limit = 4, items = { "a", "b" } }
        fn = Probe { value = function() return s.v * 2 end }
        bound = Probe { value = ui.bind(s, "v"), name = ui.bind(s, "name") }
        positional = Probe { 0.75 }
    )");
    Probe* plain = sync("plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_DOUBLE_EQ(plain->value(), 1.5);
    EXPECT_EQ(plain->name(), "plain");
    EXPECT_EQ(plain->tint(), (Color{1, 0, 0, 1}));
    EXPECT_EQ(plain->limit(), 4);
    EXPECT_EQ(plain->items(), (std::vector<std::string>{"a", "b"}));

    Probe* fn = sync("fn");
    EXPECT_DOUBLE_EQ(fn->value(), 6);
    EXPECT_EQ(fn->name(), "probe"); // unset: the C++ default stays

    Probe* bound = sync("bound");
    EXPECT_DOUBLE_EQ(bound->value(), 3);
    EXPECT_EQ(bound->name(), "bound");

    EXPECT_DOUBLE_EQ(sync("positional")->value(), 0.75);

    run("s.v = 10; plain.limit = nil");
    EXPECT_DOUBLE_EQ(sync("fn")->value(), 20);
    EXPECT_DOUBLE_EQ(sync("bound")->value(), 10);
    EXPECT_EQ(sync("plain")->limit(), std::nullopt); // optional: nil is a value
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(TypesTest, SyncCallsSettersOnlyOnChange) {
    run("p = Probe { value = 2 }");
    sync("p");
    const int after = counters.setValueCalls;
    EXPECT_EQ(after, 1);
    sync("p");
    sync("p");
    EXPECT_EQ(counters.setValueCalls, after);
    run("p.value = 3");
    sync("p");
    EXPECT_EQ(counters.setValueCalls, after + 1);
}

TEST_F(TypesTest, SyncReportsTypeMismatches) {
    run(R"(p = Probe { value = "lots", name = "ok" })");
    Probe* p = sync("p");
    EXPECT_DOUBLE_EQ(p->value(), 0);
    EXPECT_EQ(p->name(), "ok");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("Probe.value: number expected, got string"), std::string::npos);
}

// ------------------------------------------------------------------ notify & emit

TEST_F(TypesTest, NotifyWritesThroughBindsAndEmitsChanged) {
    run(R"(
        s = ui.state { v = 1 }
        changed = {}
        bound = Probe { value = ui.bind(s, "v"),
                        onValueChanged = function(self, v) changed[#changed + 1] = v; assert(self == bound) end }
        plain = Probe { value = 5 }
        fn = Probe { value = function() return 9 end }
    )");
    sync("bound");
    sync("plain");
    sync("fn");
    run(R"(
        bound:bump()
        assert(s.v == 2, "bind source not written: " .. tostring(s.v))
        assert(#changed == 1 and changed[1] == 2)
        plain:bump()
        assert(plain.value == 6)
        fn:bump()
        assert(type(fn.value) == "function", "a function binding must be left alone")
    )");
    EXPECT_DOUBLE_EQ(sync("fn")->value(), 9); // the binding wins again on the next sync
    EXPECT_LE(secondsUntilFrame(), 0.0);       // notify requested a frame
}

TEST_F(TypesTest, NotifyWritesThroughGetterSetterBinds) {
    run(R"(
        local stored = 0
        b = Probe { value = ui.bind(function() return stored end, function(v) stored = v end) }
        function storedValue() return stored end
    )");
    sync("b");
    run("b:bump(); assert(storedValue() == 1)");
}

TEST_F(TypesTest, EmitCallsHandlersWithArguments) {
    run(R"(
        got = nil
        p = Probe { onFired = function(self, n, s) got = { self = self, n = n, s = s } end }
        p:fire()
        assert(got and got.self == p and got.n == 7 and got.s == "seven")
        q = Probe {}
        q:fire() -- no handler: nothing happens
    )");
}

TEST_F(TypesTest, HandlerErrorsAreReportedNotRaised) {
    run(R"(
        p = Probe { onFired = function() error("boom") end }
        p:fire()
    )");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("boom"), std::string::npos);
}

// ------------------------------------------------------------------ lifetime

TEST_F(TypesTest, CollectingTheElementDestroysTheInstance) {
    run(R"(
        do local a, b = Probe {}, Probe { value = 1 } end
        keep = Probe {}
        collectgarbage(); collectgarbage()
    )");
    EXPECT_EQ(counters.constructed, 3);
    EXPECT_EQ(counters.destructed, 2);
    EXPECT_EQ(counters.destroyedHook, 2);
    lua::close(m_state);
    m_state = nullptr;
    EXPECT_EQ(counters.destructed, 3); // closing the state destroys the rest
    EXPECT_EQ(counters.destroyedHook, 3);
}

TEST_F(TypesTest, RawTablesGetAnInstanceOnFirstSync) {
    run(R"(raw = { __type = "Probe", value = 4 })");
    Probe* p = sync("raw");
    ASSERT_NE(p, nullptr);
    EXPECT_DOUBLE_EQ(p->value(), 4);
    run(R"(assert(type(raw.__native) == "userdata"); assert(raw:add(0, 0) == 4))");
}

// ------------------------------------------------------------------ singletons

TEST_F(TypesTest, SingletonPropertiesMethodsAndSignals) {
    run(R"(
        assert(backend.level == 0.5)
        assert(backend.label == "backend")
        assert(tostring(backend) == "singleton backend")
        levels, pings = {}, {}
        backend.onLevelChanged = function(v) levels[#levels + 1] = v end
        backend.onPing = function(n, s) pings[#pings + 1] = n .. s end
        backend.level = 0.8
        assert(#levels == 1 and levels[1] == 0.8)
        assert(backend:ping(21) == 42)
        assert(pings[1] == "21pong")
        backend.level = 0.8 -- unchanged: the setter isn't called
        assert(#levels == 1)
    )");
    EXPECT_DOUBLE_EQ(m_backend.level(), 0.8);
    m_backend.setLevel(0.1); // from C++, outside Lua
    run("assert(levels[2] == 0.1)");
    EXPECT_NE(runError("backend.label = 'x'").find("backend.label is read-only"), std::string::npos);
    EXPECT_NE(runError("backend.level = {}").find("backend.level: number expected, got table"), std::string::npos);
    EXPECT_NE(runError("backend.nope = 1").find("has no property 'nope'"), std::string::npos);
    EXPECT_NE(runError("backend.onPing = 5").find("signal handlers must be functions"), std::string::npos);
}

TEST_F(TypesTest, BindingsReadSingletonsLive) {
    run(R"(t = Probe { value = function() return backend.level * 10 end })");
    EXPECT_DOUBLE_EQ(sync("t")->value(), 5);
    m_backend.setLevel(0.2);
    EXPECT_DOUBLE_EQ(sync("t")->value(), 2);
}

// ------------------------------------------------------------------ hot reload

TEST_F(TypesTest, ReinstallingIntoANewStateMovesTheSingletons) {
    run("backend.onLevelChanged = function(v) old = v end");
    lua::State* fresh = newUiState(m_types);
    run(fresh, R"(
        p = Probe { name = "again" }
        assert(p:add(1, 1) == 2)
        backend.onLevelChanged = function(v) new = v end
    )");
    EXPECT_EQ(m_backend.luaState(), fresh);

    // The engine closes the old state after a successful reload: the singleton stays with the new one.
    lua::close(m_state);
    m_state = fresh;
    EXPECT_EQ(m_backend.luaState(), fresh);
    m_backend.setLevel(0.3);
    run("assert(new == 0.3)");
    EXPECT_EQ(counters.constructed, 1);
}

TEST_F(TypesTest, FailedReloadReactivatesTheOldState) {
    run("backend.onLevelChanged = function(v) old = v end");
    lua::State* failed = newUiState(m_types);
    lua::close(failed); // the new script failed: its state closes and detaches the singletons
    EXPECT_EQ(m_backend.luaState(), nullptr);
    m_types.activate(m_state);
    m_backend.setLevel(0.9);
    run("assert(old == 0.9)");
}

TEST_F(TypesTest, ClosingTheStateDetachesSingletons) {
    lua::close(m_state);
    m_state = nullptr;
    EXPECT_EQ(m_backend.luaState(), nullptr);
    m_backend.setLevel(0.4); // no Lua link: must not crash
    EXPECT_FALSE(m_backend.pushSelf());
}
