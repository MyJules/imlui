// Tests for signal listeners (`connect`) and the singletons' reload hook (ScriptObject::scriptReloading): several
// listeners per signal on singletons, typed elements and built-ins, after the handler slot in connect order;
// disconnecting (also during a dispatch); error isolation; lifetimes (a connection never keeps its target alive, one
// made in a component scope goes with it); and reloads (connections stay with their state, the hook runs before a new
// state links the singleton).
#include <gtest/gtest.h>

import std;

import lua;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.registry;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.ui.frame_request;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

/// What Sensor::scriptReloading saw.
struct ReloadLog {
    int reloads = 0;
    lua::State* stateAtReload = nullptr; ///< the state the singleton was linked to when the hook ran
    bool throwOnReload = false;
};
ReloadLog reloadLog;

/// The number of Meter instances alive.
int metersAlive = 0;

class Sensor final : public ScriptObject {
public:
    static void declare(TypeBuilder<Sensor>& type) {
        type.property("level", &Sensor::level, &Sensor::setLevel).signal("onPeak").method("peak", &Sensor::peak);
    }

    [[nodiscard]] double level() const { return m_level; }
    void setLevel(double v) {
        m_level = v;
        notify("level");
    }
    bool peak(int n) { return emit("onPeak", n); }

    /// Records the call, and tells the script it ran (the old script, the singleton is still linked to it).
    void scriptReloading() override {
        ++reloadLog.reloads;
        reloadLog.stateAtReload = luaState();
        emit("onPeak", -1);
        if (reloadLog.throwOnReload) throw std::runtime_error("hook failed");
    }

private:
    double m_level = 0;
};

class Meter final : public Element {
public:
    static void declare(TypeBuilder<Meter>& type) {
        type.property("value", &Meter::value, &Meter::setValue)
            .signal("onPeak")
            .method("fire", &Meter::fire)
            .method("bump", &Meter::bump);
    }

    Meter() { ++metersAlive; }
    ~Meter() override { --metersAlive; }
    Meter(const Meter&) = delete;
    Meter& operator=(const Meter&) = delete;
    Meter(Meter&&) = delete;
    Meter& operator=(Meter&&) = delete;

    void render(Frame&) override {}

    [[nodiscard]] double value() const { return m_value; }
    void setValue(double v) { m_value = v; }
    bool fire(int n) { return emit("onPeak", n); }
    void bump() {
        m_value += 1;
        notify("value");
    }

private:
    double m_value = 0;
};

class SignalsTest : public ::testing::Test {
protected:
    SignalsTest() {
        reloadLog = {};
        metersAlive = 0;
        m_h.registerType<Meter>("Meter");
        m_h.types().registerSingleton("sensor", m_sensor);
    }

    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    static void expectNoErrors() {
        for (const auto& e : luaui::errors())
            ADD_FAILURE() << e;
    }

    Sensor m_sensor; ///< before the harness: it outlives the harness's Lua state
    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

/// A plain Lua state with the registered types, imlui.lua and a stub `imlui.native`, like an engine's (re)load.
lua::State* newUiState(TypeRegistry& types) {
    lua::State* state = lua::newstate();
    lua::openlibs(state);
    types.install(state);
    const std::string boot = std::string{"package.path = '"} + IMLUI_RESOURCES_DIR + "/?.lua;' .. package.path\n" +
                             R"(package.preload["imlui.native"] = function()
                                    return { time = function() return 0 end, log = function() end }
                                end
                                ui = require("imlui"))";
    EXPECT_EQ(lua::loadbuffer(state, boot, "=boot"), lua::ok) << lua::tostring(state, -1);
    EXPECT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
    return state;
}

void runIn(lua::State* state, std::string_view code) {
    ASSERT_EQ(lua::loadbuffer(state, code, "=test"), lua::ok) << lua::tostring(state, -1);
    ASSERT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
}

} // namespace

// ------------------------------------------------------------------ several listeners

TEST_F(SignalsTest, SingletonListenersRunAfterTheSlotInConnectOrder) {
    run(R"(
        log = {}
        sensor.onPeak = function(n) log[#log + 1] = "slot" .. n end
        connect(sensor, "onPeak", function(n) log[#log + 1] = "a" .. n end)
        connect(sensor, "onPeak", function(...) log[#log + 1] = "b" .. select("#", ...) end)
        assert(sensor:peak(3))
        assert(table.concat(log, ",") == "slot3,a3,b1", table.concat(log, ","))
        sensor.onPeak = nil
        log = {}
    )");
    EXPECT_TRUE(m_sensor.peak(4)); // from C++, outside every Lua call: listeners alone count as handled
    run(R"(assert(table.concat(log, ",") == "a4,b1", table.concat(log, ",")))");
    expectNoErrors();
}

TEST_F(SignalsTest, ElementListenersGetSelfLikeTheSlot) {
    run(R"(
        log = {}
        m = Meter { onPeak = function(self, n) log[#log + 1] = "slot" .. n; assert(self == m) end }
        connect(m, "onPeak", function(self, n) log[#log + 1] = "a" .. n; assert(self == m) end)
        connect(m, "onPeak", function(self, n) log[#log + 1] = "b" .. n end)
        assert(m:fire(5))
        assert(table.concat(log, ",") == "slot5,a5,b5", table.concat(log, ","))
        raw = { __type = "Meter" } -- no instance yet: connect creates it
        connect(raw, "onPeak", function(self, n) rawHit = n end)
        raw:fire(6)
        assert(rawHit == 6)
    )");
    expectNoErrors();
}

// A coroutine (an async function's body too) runs on a Lua thread of its own: connect compares the target's state
// with the main thread's, not the running thread's.
TEST_F(SignalsTest, ConnectWorksInsideCoroutines) {
    run(R"(
        log = {}
        m = Meter {}
        local ok, err = pcall(coroutine.wrap(function()
            connect(sensor, "onPeak", function(n) log[#log + 1] = "co" .. n end)
            connect(m, "onPeak", function(self, n) log[#log + 1] = "el" .. n end)
        end))
        assert(ok, err)
        async(function() connect(sensor, "onPeak", function(n) log[#log + 1] = "async" .. n end) end)()
        assert(sensor:peak(1))
        assert(m:fire(2))
        assert(table.concat(log, ",") == "co1,async1,el2", table.concat(log, ","))
    )");
    expectNoErrors();
}

// An element built in an async function after an await is constructed on the coroutine's thread: it links to the main
// thread anyway, so connect accepts it, its signals reach Lua after the coroutine finished and was collected, and
// emit/notify from C++ (outside every Lua call) don't run on the dead coroutine.
TEST_F(SignalsTest, ElementsBuiltInACoroutineLinkToTheMainThread) {
    run(R"(
        log = {}
        gate = future()
        done = async(function()
            await(gate) -- resumed from the main thread's gate:resolve(): the rest runs on the coroutine's thread
            m = Meter { onValueChanged = function(self, v) log[#log + 1] = "slot" .. v end }
            connect(m, "onPeak", function(self, n) assert(self == m); log[#log + 1] = "peak" .. n end)
            connect(m, "onValueChanged", function(self, v) log[#log + 1] = "value" .. v end)
        end)()
        gate:resolve()
        assert(done.state == "done", done.state)
        gate, done = nil, nil
        collectgarbage()
        collectgarbage() -- the finished coroutine is gone
    )");
    Meter* m = m_h.instance<Meter>("m");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->luaState(), lua::mainthread(m_h.state()));
    EXPECT_TRUE(m->fire(1)); // from C++, outside every Lua call
    m->bump();
    run(R"(
        assert(m:fire(2))
        m:bump()
        assert(table.concat(log, ",") == "peak1,slot1,value1,peak2,slot2,value2", table.concat(log, ","))
    )");
    expectNoErrors();
}

// A box is recognized by a tag Lua can't write, not by a field of its metatable: a foreign userdata with a forged
// `__imlui` marker, or with a box's own metatable (debug.setmetatable), is rejected (listen says why, a method or a
// singleton read raises), not read as a box.
TEST_F(SignalsTest, AForeignUserdataIsNotABox) {
    run(R"(
        local types = require("imlui.types")
        local stdoutMeta = getmetatable(io.stdout)
        stdoutMeta.__imlui = true
        m = Meter {}
        local fake = setmetatable({ __native = io.stdout }, getmetatable(m))
        local ok, err = types.listen(io.stdout, "onPeak") -- false, why
        local callOk, callErr = pcall(m.fire, fake, 1) -- a method trampoline on a table with a forged __native
        stdoutMeta.__imlui = nil
        assert(not ok and err:find("not an element of a C++ type", 1, true), tostring(err))
        assert(not callOk, "a method ran on a forged __native")

        -- A box's own metatable on a foreign userdata: its methods and __gc see no box.
        debug.setmetatable(io.stdout, getmetatable(rawget(m, "__native")))
        ok, err = types.listen(io.stdout, "onPeak")
        local get = getmetatable(io.stdout).__get
        local gotOk, gotErr = pcall(get, io.stdout, "value")
        getmetatable(io.stdout).__gc(io.stdout) -- does nothing to a foreign userdata
        debug.setmetatable(io.stdout, stdoutMeta)
        assert(not ok and err:find("not an element of a C++ type", 1, true), tostring(err))
        assert(not gotOk and gotErr:find("not a live imlui object"), tostring(gotErr))
        io.stdout:write("") -- still a working file

        local sensorMeta = getmetatable(sensor)
        debug.setmetatable(io.stdout, sensorMeta)
        ok, err = pcall(function() return io.stdout.level end)
        debug.setmetatable(io.stdout, stdoutMeta)
        assert(not ok, "a foreign userdata read as a singleton")
        assert(m:fire(1) == false and sensor:peak(1) == false) -- the real boxes still work
    )");
    expectNoErrors();
}

TEST_F(SignalsTest, NotifyChangeSignalsReachListeners) {
    run(R"(
        log = {}
        connect(sensor, "onLevelChanged", function(v) log[#log + 1] = "sensor" .. v end)
        m = Meter { onValueChanged = function(self, v) log[#log + 1] = "slot" .. v end }
        connect(m, "onValueChanged", function(self, v) assert(self == m); log[#log + 1] = "m" .. v end)
        m:bump()
    )");
    m_sensor.setLevel(2);
    run(R"(assert(table.concat(log, ",") == "slot1,m1,sensor2", table.concat(log, ",")))");
    expectNoErrors();
}

// A built-in's signal goes through ScriptObject::emit like an app type's: a simulated click reaches every listener.
TEST_F(SignalsTest, ABuiltInButtonsClickReachesListeners) {
    run(R"(
        log = {}
        root = Button { "OK", onClicked = function(self) log[#log + 1] = "slot" end }
        connect(root, "onClicked", function(self) assert(self == root); log[#log + 1] = "a" end)
        connect(root, "onClicked", function() log[#log + 1] = "b" end)
    )");
    m_h.renderFrame("root"); // the window exists, so it can be hovered
    m_h.click("root", 20, 20);
    run(R"(assert(table.concat(log, ",") == "slot,a,b", table.concat(log, ",")))");
    expectNoErrors();
}

TEST_F(SignalsTest, ConnectChecksItsArguments) {
    const auto fails = [&](std::string_view code, std::string_view part) {
        const std::string error = m_h.run(code);
        EXPECT_NE(error.find(part), std::string::npos) << code << ": " << error;
    };
    fails(R"(connect(sensor, "onNope", print))", "connect: sensor has no signal 'onNope'");
    fails(R"(connect(Meter {}, "onValueChange", print))", "connect: Meter has no signal 'onValueChange'");
    fails(R"(connect({}, "onPeak", print))", "connect: the target is not an element of a C++ type or a singleton");
    fails(R"(connect(Column {}, "onPeak", print))", "connect: Column has no signal 'onPeak'");
    fails(R"(connect(sensor, 1, print))", "connect: signal name expected, got number");
    fails(R"(connect(sensor, "onPeak", 5))", "connect: function expected, got number");
    run(R"(connect(Button {}, "onTextChanged", print))"); // the change signal of a property
}

// ------------------------------------------------------------------ disconnecting

TEST_F(SignalsTest, DisconnectingDuringADispatchIsSafe) {
    run(R"(
        log = {}
        local stopA, stopB
        stopA = connect(sensor, "onPeak", function()
            log[#log + 1] = "a"
            stopA()
            stopB() -- not run in this dispatch either
            connect(sensor, "onPeak", function() log[#log + 1] = "c" end) -- runs from the next one on
        end)
        stopB = connect(sensor, "onPeak", function() log[#log + 1] = "b" end)
        sensor:peak(1)
        assert(table.concat(log, ",") == "a", table.concat(log, ","))
        log = {}
        sensor:peak(2)
        assert(table.concat(log, ",") == "c", table.concat(log, ","))
        stopA() -- again: nothing
        stopB()
    )");
    expectNoErrors();
}

TEST_F(SignalsTest, AListenerErrorIsReportedAndTheOthersStillRun) {
    run(R"(
        log = {}
        connect(sensor, "onPeak", function() error("listener exploded") end)
        connect(sensor, "onPeak", function() log[#log + 1] = "after" end)
    )");
    EXPECT_TRUE(m_sensor.peak(1));
    run(R"(assert(log[1] == "after"))");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("listener exploded"), std::string::npos) << luaui::errors()[0];
}

// ------------------------------------------------------------------ lifetimes

TEST_F(SignalsTest, ConnectionsDontKeepElementsAlive) {
    run(R"(
        do
            local m = Meter {}
            connect(m, "onPeak", function(self) return m end) -- a listener that holds its own target
            local n = Meter {}
            connect(n, "onValueChanged", function() end)
        end
        collectgarbage(); collectgarbage()
    )");
    EXPECT_EQ(metersAlive, 0);
    expectNoErrors();
}

// A connection made while a component instance is built (or by its elements' handlers) runs in the instance's scope
// and is dropped with it; one made at app level stays.
TEST_F(SignalsTest, AComponentsConnectionsGoWithIt) {
    run(R"(
        hits, appHits = 0, 0
        Comp = component(function(props, ids)
            instanceIds = ids
            connect(sensor, "onPeak", function()
                hits = hits + 1
                built = Text { id = "inner" } -- registers in the instance's ids: the listener runs in its scope
            end)
            return Meter {}
        end)
        holder = { Comp {} }
        connect(sensor, "onPeak", function() appHits = appHits + 1 end)
        collectgarbage(); collectgarbage()
        sensor:peak(1)
        assert(hits == 1 and appHits == 1)
        assert(instanceIds.inner == built and ui.ids.inner == nil)
        holder, built = nil, nil
    )");
    m_h.renderFrame("none"); // ui.__tick: the ids no longer pin `built`, which would keep the scope
    run(R"(
        collectgarbage(); collectgarbage()
        sensor:peak(2)
        assert(hits == 1, "the dropped component's listener ran")
        assert(appHits == 2)
    )");
    EXPECT_EQ(metersAlive, 0);
    expectNoErrors();
}

// A Repeater that rebuilds its delegates again and again: the listeners each one connected go with it, so a dispatch
// runs the current delegates' only, and the dropped Meters are collected.
TEST_F(SignalsTest, RebuiltDelegatesLeaveNoListenersBehind) {
    run(R"(
        hits = 0
        items = {}
        rep = Repeater { model = function() return items end, delegate = function()
            local m = Meter {}
            connect(sensor, "onPeak", function() hits = hits + 1; m:bump() end) -- holds its delegate's Meter
            return m
        end }
        for _ = 1, 40 do
            for i = 1, 5 do items[i] = { i } end -- new items: new delegates
            rep.__children(rep)
        end
        collectgarbage(); collectgarbage()
        sensor:peak(1)
        assert(hits == 5, hits .. " listeners ran")
    )");
    EXPECT_EQ(metersAlive, 5);
    expectNoErrors();
}

// ------------------------------------------------------------------ hot reload

// Connections belong to their Lua state: a new state starts without them, a failed reload that goes back to the old
// state finds them again, and closing the old state drops them.
TEST(SignalReloadTest, ConnectionsStayWithTheirState) {
    luaui::clearErrors();
    reloadLog = {};
    TypeRegistry types;
    Sensor sensor;
    types.registerSingleton("sensor", sensor);
    lua::State* old = newUiState(types);
    runIn(old, R"(hits = 0; ui.connect(sensor, "onPeak", function() hits = hits + 1 end))");
    EXPECT_TRUE(sensor.peak(1));

    lua::State* failed = newUiState(types); // a reload: the new state links the singleton
    EXPECT_FALSE(sensor.peak(2));           // no handler, no listener there
    lua::close(failed);                     // its script failed: the engine goes back to the old state
    types.activate(old);
    EXPECT_TRUE(sensor.peak(3));
    runIn(old, "assert(hits == 3, hits)"); // 1, the reload hook's emit (-1), 3

    lua::State* next = newUiState(types); // a reload that succeeds: the old state closes, its connections with it
    lua::close(old);
    runIn(next, "hits = 0");
    EXPECT_FALSE(sensor.peak(4));
    runIn(next, "assert(hits == 0)");
    lua::close(next);
    EXPECT_TRUE(luaui::errors().empty());
    clearFrameRequests();
}

// scriptReloading() runs in install() for every singleton an earlier state had, before the new state links it: still
// linked to the running script's state, which it may notify. Not on the first install, and not again when a failed
// reload goes back to the old state (activate()).
TEST(SignalReloadTest, TheReloadHookRunsBeforeANewStateLinksTheSingleton) {
    luaui::clearErrors();
    reloadLog = {};
    TypeRegistry types;
    Sensor sensor;
    types.registerSingleton("sensor", sensor);
    lua::State* first = newUiState(types);
    EXPECT_EQ(reloadLog.reloads, 0); // nothing ran before
    runIn(first, R"(ui.connect(sensor, "onPeak", function(n) told = n end))");

    lua::State* second = newUiState(types);
    EXPECT_EQ(reloadLog.reloads, 1);
    EXPECT_EQ(reloadLog.stateAtReload, first);
    EXPECT_EQ(sensor.luaState(), second);
    runIn(first, "assert(told == -1, tostring(told))"); // the hook's emit reached the old script

    lua::close(second); // the new script failed
    types.activate(first);
    EXPECT_EQ(reloadLog.reloads, 1);

    lua::State* third = newUiState(types);
    EXPECT_EQ(reloadLog.reloads, 2);
    lua::close(first);
    lua::close(third); // no state left (a failed load closed the last one): the next install runs it, unlinked
    reloadLog.throwOnReload = true;
    lua::State* fourth = newUiState(types);
    EXPECT_EQ(reloadLog.reloads, 3);
    EXPECT_EQ(reloadLog.stateAtReload, nullptr);
    EXPECT_EQ(sensor.luaState(), fourth); // an exception from the hook is reported, the install goes on
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("scriptReloading(): hook failed"), std::string::npos) << luaui::errors()[0];
    lua::close(fourth);
    luaui::clearErrors();
    clearFrameRequests();
}
