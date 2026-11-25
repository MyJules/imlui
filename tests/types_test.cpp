// Tests for the C++ type system (imlui.types.*): LuaConvert, TypeBuilder dispatch, property sync,
// notify/emit, instance lifetime, singletons and reinstalling into a new Lua state (hot reload).
// Runs src/resources/imlui.lua in a plain Lua state with a stub `imlui.native`; no window or ImGui frame.
#include <gtest/gtest.h>

import std;

import lua;
import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.theme.theme;
import imlui.ui.frame_request;
import imlui.types.lua_convert;
import imlui.types.script_object;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_info;
import imlui.types.type_builder;
import imlui.types.registry;
import imlui.types.overload;

using namespace imlui;
using luaui::Node;

namespace {

struct Counters {
    int constructed = 0;
    int destructed = 0;
    int destroyedHook = 0;
    int setValueCalls = 0;
    int nameReads = 0; ///< Probe::name() calls: a sync that converts `name` compares with it
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
            .property("scale", &Probe::scale, &Probe::setScale, 2.0) // an explicit nil default
            .property("setCount", &Probe::setCount)
            .signal("onFired")
            .method("add", &Probe::add)
            .method("greet", &Probe::greet)
            .method("fire", &Probe::fire)
            .method("bump", &Probe::bump)
            .method("limitOr", &Probe::limitOr)
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
    [[nodiscard]] const std::string& name() const {
        ++counters.nameReads;
        return m_name;
    }
    void setName(std::string n) { m_name = std::move(n); }
    [[nodiscard]] Color tint() const { return m_tint; }
    void setTint(Color c) { m_tint = c; }
    [[nodiscard]] std::optional<int> limit() const { return m_limit; }
    void setLimit(std::optional<int> l) { m_limit = l; }
    [[nodiscard]] const std::vector<std::string>& items() const { return m_items; }
    void setItems(std::vector<std::string> i) { m_items = std::move(i); }
    [[nodiscard]] int setCount() const { return counters.setValueCalls; }
    [[nodiscard]] double scale() const { return m_scale; }
    void setScale(double s) { m_scale = s; }

    [[nodiscard]] double add(double a, int b) const { return m_value + a + b; }
    [[nodiscard]] std::string greet(const std::string& who) const { return "hello " + who + " from " + m_name; }
    void fire() { emit("onFired", 7, "seven"); }
    void bump() {
        m_value += 1;
        notify("value");
    }
    /// Changes `value` in C++ without notify(): the table isn't written.
    void setQuietly(double v) { m_value = v; }
    /// `limit`, or `fallback` for nil: an optional argument that may also be left out.
    [[nodiscard]] int limitOr(int fallback, std::optional<int> limit) const { return limit.value_or(fallback); }

private:
    double m_value = 0;
    std::string m_name = "probe";
    Color m_tint{};
    std::optional<int> m_limit;
    std::vector<std::string> m_items;
    double m_scale = 1.0;
};

class Backend final : public ScriptObject {
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

/// Overloaded members, picked with imlui::overload / constOverload.
class Shape final : public ScriptObject {
public:
    static void declare(TypeBuilder<Shape>& type) {
        type.property("size", constOverload<>(&Shape::size), overload<double>(&Shape::setSize))
            .method("grow", overload<double>(&Shape::grow))
            .method("growBoth", overload<double, double>(&Shape::grow))
            .method("area", constOverload<>(&Shape::area))
            .method("scaledArea", constOverload<double>(&Shape::area))
            .method("named", overload<const std::string&>(&Shape::setSize));
    }

    [[nodiscard]] double size() const { return m_w; }
    [[nodiscard]] double size(int axis) const { return axis == 0 ? m_w : m_h; }
    void setSize(double s) { m_w = m_h = s; }
    std::string setSize(const std::string& preset) {
        setSize(preset == "large" ? 10.0 : 1.0);
        return preset;
    }
    double grow(double by) noexcept {
        m_w += by;
        m_h += by;
        return m_w;
    }
    double grow(double w, double h) {
        m_w += w;
        m_h += h;
        return m_w * m_h;
    }
    [[nodiscard]] double area() const { return m_w * m_h; }
    [[nodiscard]] double area(double k) const { return m_w * m_h * k; }

private:
    double m_w = 1.0;
    double m_h = 1.0;
};

static_assert(std::is_same_v<decltype(overload<double, double>(&Shape::grow)), double (Shape::*)(double, double)>);
static_assert(std::is_same_v<decltype(constOverload<int>(&Shape::size)), double (Shape::*)(int) const>);
static_assert(overload<double>(&Shape::setSize) == static_cast<void (Shape::*)(double)>(&Shape::setSize));

/// Throws from its property hooks: a negative value from the setter, and the getter of `broken` always.
class Thrower final : public Element {
public:
    static void declare(TypeBuilder<Thrower>& type) {
        type.property("value", &Thrower::value, &Thrower::setValue).property("broken", &Thrower::broken);
    }

    void render(Frame&) override {}

    [[nodiscard]] double value() const { return m_value; }
    void setValue(double v) {
        if (v < 0) throw std::invalid_argument("negative value");
        m_value = v;
    }
    [[nodiscard]] double broken() const { throw std::runtime_error("broken getter"); }

private:
    double m_value = 0;
};

/// Its constructor throws something that isn't a std::exception.
class IntThrower final : public Element {
public:
    static void declare(TypeBuilder<IntThrower>&) {}
    IntThrower() { throw 42; }
    void render(Frame&) override {}
};

lua::State* newUiState(TypeRegistry& types) {
    lua::State* state = lua::newstate();
    lua::openlibs(state);
    types.install(state);
    const std::string boot = std::string{"package.path = '"} + IMLUI_RESOURCES_DIR + "/?.lua;' .. package.path\n" +
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

    TypeRegistry m_types;
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
    EXPECT_TRUE(LuaConvert<std::optional<int>>::check(s, 5)); // an index past the top: an omitted argument
    EXPECT_EQ(LuaConvert<std::optional<int>>::get(s, 5), std::nullopt);
    lua::close(s);
}

// Narrowing to float is checked: out-of-range numbers clamp (the cast is undefined behaviour), NaN reads as 0 like an
// integer's, infinities stay.
TEST(LuaConvertTest, FloatsAreNarrowedSafely) {
    lua::State* s = lua::newstate();
    constexpr float max = std::numeric_limits<float>::max();
    const auto get = [&](double v) {
        lua::pushnumber(s, v);
        const float f = LuaConvert<float>::get(s, -1);
        lua::pop(s);
        return f;
    };
    EXPECT_EQ(get(1e300), max);
    EXPECT_EQ(get(-1e300), -max);
    EXPECT_EQ(get(std::numeric_limits<double>::quiet_NaN()), 0.0f);
    EXPECT_EQ(get(std::numeric_limits<double>::infinity()), std::numeric_limits<float>::infinity());
    EXPECT_EQ(get(-std::numeric_limits<double>::infinity()), -std::numeric_limits<float>::infinity());
    EXPECT_EQ(get(0.5), 0.5f);
    lua::pushnumber(s, std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(LuaConvert<int>::get(s, -1), 0); // the integral conversion's NaN, for comparison
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
        assert(p:greet("you") == "hello you from P") -- never rendered: synced before the call
        p.name = "Q"
        assert(p:greet("you") == "hello you from Q") -- changed since: synced again
    )");
    Probe* p = sync("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name(), "Q");
    EXPECT_NE(runError(R"(p:add("x", 1))").find("bad argument #1 to 'Probe:add' (number expected, got string)"),
              std::string::npos);
    EXPECT_NE(runError(R"(p.add({}, 1, 2))").find("method called without its object"), std::string::npos);
    EXPECT_NE(runError(R"(p:missing())").find("attempt to call a nil value (method 'missing')"), std::string::npos);
}

// An optional parameter takes nil and an argument that is left out alike (the omitted one was "got no value").
TEST_F(TypesTest, AnOptionalArgumentMayBeLeftOut) {
    run(R"(
        p = Probe {}
        assert(p:limitOr(-1, 4) == 4)
        assert(p:limitOr(-1, nil) == -1)
        assert(p:limitOr(-1) == -1)
    )");
    EXPECT_NE(runError(R"(p:limitOr(-1, "x"))").find("bad argument #2 to 'Probe:limitOr' (integer expected, got string)"),
              std::string::npos);
    EXPECT_NE(runError(R"(p:limitOr())").find("bad argument #1 to 'Probe:limitOr' (integer expected, got no value)"),
              std::string::npos);
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

TEST_F(TypesTest, NilResetsPropertiesToTheirDefaults) {
    run(R"(
        s = ui.state { v = 3, name = "bound" }
        p = Probe { value = 1.5, name = "named", scale = 4, limit = 2 }
        fn = Probe { value = function() return s.v end, name = ui.bind(s, "name") }
    )");
    Probe* p = sync("p");
    EXPECT_DOUBLE_EQ(p->value(), 1.5);
    EXPECT_DOUBLE_EQ(p->scale(), 4);
    run("p.value = nil; p.name = nil; p.scale = nil; p.limit = nil");
    sync("p");
    EXPECT_DOUBLE_EQ(p->value(), 0);     // the getter's value right after construction
    EXPECT_EQ(p->name(), "probe");       // likewise
    EXPECT_DOUBLE_EQ(p->scale(), 2);     // the declared default (not the constructed 1.0)
    EXPECT_EQ(p->limit(), std::nullopt); // optional: nil is nullopt

    Probe* fn = sync("fn");
    EXPECT_DOUBLE_EQ(fn->value(), 3);
    EXPECT_EQ(fn->name(), "bound");
    run("s.v = nil; s.name = nil"); // a binding and a two-way bind that now give nil
    sync("fn");
    EXPECT_DOUBLE_EQ(fn->value(), 0);
    EXPECT_EQ(fn->name(), "probe");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(TypesTest, DefaultsAreCapturedPerInstanceBeforeAnySetter) {
    run(R"(
        a = Probe { value = 7 }
        b = Probe { }
    )");
    sync("a");
    run("a.value = nil");
    EXPECT_DOUBLE_EQ(sync("a")->value(), 0);
    EXPECT_DOUBLE_EQ(sync("b")->value(), 0);
    EXPECT_DOUBLE_EQ(sync("b")->scale(), 2); // never set: the explicit default applies from the first sync
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

// A sync converts a static property (a nil, boolean, number or string in the element table) only while its field
// changed since the last sync: the instance holds the rest already. Bindings are still read at every sync.
TEST_F(TypesTest, StaticPropertiesArentConvertedAgain) {
    run(R"(
        s = ui.state { v = 1 }
        p = Probe { name = "static", value = function() return s.v end }
    )");
    Probe* p = sync("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name(), "static");
    const int reads = counters.nameReads;
    const int sets = counters.setValueCalls;
    for (int i = 2; i <= 4; ++i) {
        run("s.v = " + std::to_string(i));
        sync("p");
        EXPECT_DOUBLE_EQ(p->value(), i); // the binding: read every sync
    }
    EXPECT_EQ(counters.nameReads, reads); // `name` wasn't converted (its getter compares) again
    EXPECT_EQ(counters.setValueCalls, sets + 3);
    sync("p");
    EXPECT_EQ(counters.setValueCalls, sets + 3); // the binding's value didn't change: no setter
    EXPECT_TRUE(luaui::errors().empty());
}

// A write to the element table (a plain Lua assignment, a raw write) changes its fingerprint, so the next sync reads
// every property: a new value, nil (back to the default), a value that becomes a binding and back.
TEST_F(TypesTest, WritesNilResetsAndTypeChangesArePickedUp) {
    run(R"(
        s = ui.state { v = 1 }
        p = Probe { name = "a", scale = 4 }
    )");
    Probe* p = sync("p");
    run("p.name = 'b'");
    EXPECT_EQ(sync("p")->name(), "b");
    run("rawset(p, 'name', 'raw')");
    EXPECT_EQ(sync("p")->name(), "raw");
    run("p.name = nil; p.scale = nil");
    sync("p");
    EXPECT_EQ(p->name(), "probe");
    EXPECT_DOUBLE_EQ(p->scale(), 2);
    run("p.name = function() return 'fn' .. s.v end"); // static -> binding
    EXPECT_EQ(sync("p")->name(), "fn1");
    run("s.v = 2");
    EXPECT_EQ(sync("p")->name(), "fn2");
    run("p.name = 'plain'"); // binding -> static
    EXPECT_EQ(sync("p")->name(), "plain");
    run("s.v = 3");
    EXPECT_EQ(sync("p")->name(), "plain");
    run("p.value = ui.bind(s, 'v')"); // a bind: read every sync
    EXPECT_DOUBLE_EQ(sync("p")->value(), 3);
    run("s.v = 7");
    EXPECT_DOUBLE_EQ(sync("p")->value(), 7);
    run("p[1] = 0.25; p.value = nil"); // the default property's positional value
    EXPECT_DOUBLE_EQ(sync("p")->value(), 0.25);
    EXPECT_TRUE(luaui::errors().empty());
}

// A table value can change in place, which no fingerprint sees: it is converted again at every sync.
TEST_F(TypesTest, TablesChangedInPlaceAreSynced) {
    run(R"(p = Probe { items = { "a" }, tint = { 1, 0, 0 } })");
    Probe* p = sync("p");
    EXPECT_EQ(p->items(), (std::vector<std::string>{"a"}));
    run("table.insert(p.items, 'b'); p.tint[2] = 1");
    sync("p");
    EXPECT_EQ(p->items(), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(p->tint(), (Color{1, 1, 0, 1}));
}

// Two-way: notify() writes the table (or a bind's source), so the next sync sees it. A property the C++ side changed
// without notify() keeps its value until the table's field changes.
TEST_F(TypesTest, WriteBacksAndQuietChangesFromCxx) {
    run(R"(
        s = ui.state { v = 1 }
        plain = Probe { value = 5 }
        bound = Probe { value = ui.bind(s, "v") }
    )");
    Probe* plain = sync("plain");
    Probe* bound = sync("bound");
    run("plain:bump(); bound:bump()");
    EXPECT_DOUBLE_EQ(sync("plain")->value(), 6);
    EXPECT_DOUBLE_EQ(sync("bound")->value(), 2);
    run("assert(plain.value == 6 and s.v == 2)");
    run("s.v = 10");
    EXPECT_DOUBLE_EQ(sync("bound")->value(), 10);

    plain->setQuietly(42);
    EXPECT_DOUBLE_EQ(sync("plain")->value(), 42); // the table's value didn't change: not converted again
    run("plain.value = 7");
    EXPECT_DOUBLE_EQ(sync("plain")->value(), 7);
    bound->setQuietly(42);
    EXPECT_DOUBLE_EQ(sync("bound")->value(), 10); // a bind is read every sync
}

// An element table with a metatable of its own reads the fields it doesn't hold through its __index (inherited
// properties): those are read every sync, and setting the metatable after a sync is seen too.
TEST_F(TypesTest, InheritedPropertiesAreSynced) {
    run(R"(
        base = { name = "base" }
        p = Probe { value = 1 }
    )");
    Probe* p = sync("p");
    EXPECT_EQ(p->name(), "probe");
    run("setmetatable(p, { __index = base })");
    EXPECT_EQ(sync("p")->name(), "base");
    run("base.name = 'changed'");
    EXPECT_EQ(sync("p")->name(), "changed");
    EXPECT_DOUBLE_EQ(sync("p")->value(), 1);
}

// A color role resolves through the theme: a theme change converts static colors again.
TEST_F(TypesTest, AThemeChangeResolvesColorRolesAgain) {
    ImGuiContext* context = ImGui::CreateContext();
    run(R"(p = Probe { tint = "primary" })");
    Probe* p = sync("p");
    EXPECT_EQ(p->tint(), theme().scheme.primary);
    Theme t = theme();
    t.scheme.primary = Color{0.25f, 0.5f, 0.75f, 1.0f};
    setTheme(t);
    ASSERT_TRUE(applyPendingTheme());
    EXPECT_EQ(sync("p")->tint(), (Color{0.25f, 0.5f, 0.75f, 1.0f}));
    setTheme(Theme{});
    applyPendingTheme();
    ImGui::DestroyContext(context);
}

// A Lua error during the sync (here the element table's own __index) is reported, and doesn't leave the
// element "syncing": the next sync runs. (It once skipped the reset of the flag.)
TEST_F(TypesTest, ALuaErrorDuringTheSyncDoesntFreezeTheElement) {
    run(R"(
        boom = true
        p = Probe { name = "a" }
        setmetatable(p, { __index = function() if boom then error("index exploded") end end }))");
    Probe* p = sync("p");
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(std::ranges::any_of(
        luaui::errors(), [](const std::string& e) { return e.find("index exploded") != std::string::npos; }));
    run("boom = false; p.value = 5; p.name = 'b'");
    p = sync("p");
    EXPECT_DOUBLE_EQ(p->value(), 5);
    EXPECT_EQ(p->name(), "b");
}

// Bind objects are recognized by a raw `__bind` field: a value whose __index answers anything (or raises) is
// a plain value, not a bind, and its __index never runs.
TEST_F(TypesTest, ValuesWithAnIndexMetamethodArentBinds) {
    run(R"(
        calls = 0
        p = Probe { name = setmetatable({}, { __index = function() calls = calls + 1; error("index exploded") end }) })");
    sync("p");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("Probe.name: string expected, got table"), std::string::npos);
    run("assert(calls == 0, calls)");
}

// A type registered after the state was installed works at once (its methods too), and an element created
// before its name was registered again keeps its instance.
TEST_F(TypesTest, TypesRegisteredAfterInstallTakeEffectRightAway) {
    run("old = Probe { value = 1 }; assert(old:add(1, 1) == 3)");
    m_types.registerType<Probe>("Late");
    m_types.registerType<Probe>("Probe"); // replaces the registration `old` was created with
    run(R"(
        late = ui.env.Late { value = 2 }
        assert(late:add(1, 2) == 5)
        assert(old:add(1, 1) == 3)
        local names = ui.types()
        assert(#names == 2 and names[1] == "Late", table.concat(names, ","))
    )");
    EXPECT_NE(sync("late"), nullptr);
    run("old.value = 4");
    Probe* old = sync("old");
    ASSERT_NE(old, nullptr);
    EXPECT_DOUBLE_EQ(old->value(), 4);
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
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
    EXPECT_LE(secondsUntilFrame(), 0.0);      // notify requested a frame
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

TEST_F(TypesTest, AssigningNilToASingletonPropertyResetsIt) {
    run("backend.level = 0.9; assert(backend.level == 0.9); backend.level = nil; assert(backend.level == 0.5)");
    EXPECT_DOUBLE_EQ(m_backend.level(), 0.5); // the value at registration
}

TEST(SharedSingletonTest, TheRegistryKeepsASharedSingletonAlive) {
    std::weak_ptr<Backend> weak;
    {
        TypeRegistry types;
        {
            auto backend = std::make_shared<Backend>();
            weak = backend;
            types.registerSingleton("shared", std::move(backend));
        }
        EXPECT_FALSE(weak.expired());
        lua::State* state = lua::newstate();
        lua::openlibs(state);
        types.install(state);
        ASSERT_EQ(lua::loadbuffer(state, "assert(shared.level == 0.5); shared.level = 0.7", "=test"), lua::ok);
        ASSERT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
        ASSERT_FALSE(weak.expired());
        EXPECT_DOUBLE_EQ(weak.lock()->level(), 0.7);
        lua::close(state);
        EXPECT_FALSE(weak.expired()); // outlives the Lua state
        EXPECT_THROW(types.registerSingleton("none", std::shared_ptr<Backend>{}), std::invalid_argument);
    }
    EXPECT_TRUE(weak.expired()); // released with the registry
    clearFrameRequests();
}

/// Runs `code` in `state` and returns the error it raised ("" if none).
std::string luaError(lua::State* state, std::string_view code) {
    EXPECT_EQ(lua::loadbuffer(state, code, "=test"), lua::ok);
    if (lua::pcall(state, 0, 0, 0) == lua::ok) return {};
    std::string msg{lua::tostring(state, -1)};
    lua::pop(state);
    return msg;
}

/// Expects every kind of access to the singleton global `name` to raise "singleton '<name>' was destroyed".
void expectDeadSingleton(lua::State* state, const std::string& name) {
    const std::string dead = "singleton '" + name + "' was destroyed";
    EXPECT_NE(luaError(state, "local x = " + name + ".level").find(dead), std::string::npos);
    EXPECT_NE(luaError(state, name + ".level = 1").find(dead), std::string::npos);
    EXPECT_NE(luaError(state, "local ping = getmetatable(" + name + ").__index(" + name + ", 'ping')").find(dead),
              std::string::npos);
}

// Re-registering a singleton while a state is installed destroys the old (registry-owned) object: the global the
// state still has must not reach it (it was a use after free), and says why.
TEST(SingletonLifetimeTest, ReplacedSingletonRaisesInTheInstalledState) {
    TypeRegistry types;
    std::weak_ptr<Backend> first;
    {
        auto backend = std::make_shared<Backend>();
        first = backend;
        types.registerSingleton("api", std::move(backend));
    }
    lua::State* state = newUiState(types);
    EXPECT_EQ(luaError(state, "assert(api.level == 0.5); ping = api.ping"), "");
    types.registerSingleton("api", std::make_shared<Backend>());
    EXPECT_TRUE(first.expired());
    expectDeadSingleton(state, "api");
    EXPECT_NE(luaError(state, "ping(api, 1)").find("singleton 'api' was destroyed"), std::string::npos);
    lua::close(state);

    lua::State* fresh = newUiState(types); // a new state gets the new object
    EXPECT_EQ(luaError(fresh, "assert(api.level == 0.5); assert(api:ping(2) == 4)"), "");
    lua::close(fresh);
    clearFrameRequests();
}

// A by-reference singleton the host destroys while the state is installed (or before it is): access raises, and
// closing the state doesn't touch the dead object.
TEST(SingletonLifetimeTest, DestroyedByReferenceSingletonRaises) {
    luaui::clearErrors();
    TypeRegistry types;
    auto backend = std::make_optional<Backend>();
    types.registerSingleton("api", *backend);
    lua::State* state = newUiState(types);
    EXPECT_EQ(luaError(state, "assert(api.level == 0.5)"), "");
    backend.reset();
    expectDeadSingleton(state, "api");
    lua::State* later = newUiState(types); // installed after the object died
    expectDeadSingleton(later, "api");
    types.activate(state);
    lua::close(later);
    lua::close(state);
    clearFrameRequests();
}

// A constructor that throws a non-std exception is reported with a message (it escaped the catch before), and the
// element gets no instance (`__native = false`: the failure is remembered, the constructor not tried again).
TEST(ConstructorExceptionTest, NonStdExceptionIsReported) {
    luaui::clearErrors();
    TypeRegistry types;
    types.registerType<IntThrower>("IntThrower");
    lua::State* state = newUiState(types);
    EXPECT_EQ(luaError(state, "t = ui.env.IntThrower {}; assert(rawget(t, '__native') == false)"), "");
    ASSERT_FALSE(luaui::errors().empty());
    EXPECT_NE(luaui::errors().back().find("IntThrower: constructor failed: unknown C++ exception"), std::string::npos);
    lua::close(state);
    luaui::clearErrors();
}

TEST(OverloadTest, PicksOverloadsByParameterTypes) {
    TypeRegistry types;
    Shape shape;
    types.registerSingleton("shape", shape);
    lua::State* state = lua::newstate();
    lua::openlibs(state);
    types.install(state);
    ASSERT_EQ(lua::loadbuffer(state, R"(
        assert(shape.size == 1)
        shape.size = 2
        assert(shape:area() == 4)
        assert(shape:grow(1) == 3)
        assert(shape:growBoth(1, 2) == 4 * 5)
        assert(shape:scaledArea(0.5) == 10)
        assert(shape:named("large") == "large" and shape.size == 10)
    )",
                              "=test"),
              lua::ok);
    EXPECT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
    EXPECT_DOUBLE_EQ(shape.size(1), 10);
    lua::close(state);
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

// ------------------------------------------------------------------ C++ exceptions in property hooks

// Setters and getters that throw are reported as script errors (the sync of the other properties goes on).
TEST(PropertyExceptionTest, ThrowingSettersAndGettersAreReported) {
    luaui::clearErrors();
    TypeRegistry types;
    types.registerType<Thrower>("Thrower");
    Thrower single;
    types.registerSingleton("single", single);
    lua::State* state = newUiState(types);
    const auto run = [&](std::string_view code) {
        ASSERT_EQ(lua::loadbuffer(state, code, "=test"), lua::ok) << lua::tostring(state, -1);
        ASSERT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
    };
    const auto hasError = [](std::string_view part) {
        for (const auto& e : luaui::errors())
            if (e.find(part) != std::string::npos) return true;
        return false;
    };

    // Singleton: obj.value = v runs the setter, obj.broken the getter.
    run("single.value = -1; assert(single.value == 0); assert(single.broken == nil); single.value = 3");
    EXPECT_TRUE(hasError("single.value: negative value")); // a singleton's type is named after it
    EXPECT_TRUE(hasError("single.broken: broken getter"));
    EXPECT_DOUBLE_EQ(single.value(), 3);

    // Element: the sync reports the failed setter, and the next sync still runs (it isn't left "syncing").
    luaui::clearErrors();
    run("t = ui.env.Thrower { value = -2 }");
    lua::getglobal(state, "t");
    auto* t = dynamic_cast<Thrower*>(types.sync(Node{state, -1}));
    ASSERT_NE(t, nullptr);
    EXPECT_DOUBLE_EQ(t->value(), 0);
    EXPECT_TRUE(hasError("Thrower.value: negative value"));
    run("t.value = 5");
    EXPECT_EQ(types.sync(Node{state, -1}), t);
    EXPECT_DOUBLE_EQ(t->value(), 5);
    lua::pop(state);

    lua::close(state);
    luaui::clearErrors();
    clearFrameRequests();
}
