// Tests for lua/imlui.lua itself, loaded in a plain Lua state with a stub `imlui.native`.
#include <gtest/gtest.h>

import lua;

namespace {

class LuaLibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_state = lua::newstate();
        lua::openlibs(m_state);
        // Only what imlui.lua touches outside of callbacks; the rest may stay nil.
        std::string boot = std::string{"package.path = '"} + IMLUI_LUA_DIR + "/?.lua;' .. package.path\n" +
                           R"(package.preload["imlui.native"] = function()
                                  local now = 0
                                  return { time = function() return now end, log = function() end }
                              end
                              ui = require("imlui"))";
        run(boot);
    }

    void TearDown() override { lua::close(m_state); }

    /// Runs `code` with the library in global `ui`; fails the test on any Lua error.
    void run(std::string_view code) {
        ASSERT_EQ(lua::loadbuffer(m_state, code, "=test"), lua::ok) << lua::tostring(m_state, -1);
        ASSERT_EQ(lua::pcall(m_state, 0, 0, 0), lua::ok) << lua::tostring(m_state, -1);
    }

    lua::State* m_state = nullptr;
};

} // namespace

TEST_F(LuaLibraryTest, WatchReturnsAFunctionThatRemovesTheWatcher) {
    run(R"(
        local s = ui.state { x = 1 }
        local calls, all = 0, 0
        local stop = ui.watch(s, "x", function() calls = calls + 1 end)
        local stopAll = ui.watch(s, "*", function() all = all + 1 end)
        assert(type(stop) == "function")
        s.x = 2
        assert(calls == 1 and all == 1)
        stop()
        s.x = 3
        assert(calls == 1 and all == 2, "removed watcher still called")
        stop() -- removing twice is harmless
        stopAll()
        s.x = 4
        assert(all == 2)
    )");
}

TEST_F(LuaLibraryTest, WatcherCanRemoveItselfWhileRunning) {
    run(R"(
        local s = ui.state { x = 0 }
        local first, second = 0, 0
        local stop
        stop = ui.watch(s, "x", function() first = first + 1; stop() end)
        ui.watch(s, "x", function() second = second + 1 end)
        s.x = 1
        s.x = 2
        assert(first == 1, "self-removing watcher ran " .. first .. " times")
        assert(second == 2, "the next watcher was skipped")
    )");
}

TEST_F(LuaLibraryTest, OnFrameReturnsAFunctionThatRemovesTheCallback) {
    run(R"(
        local a, b, seen = 0, 0, nil
        local stopA = ui.onFrame(function(t) a = a + 1; seen = t end)
        local stopB
        stopB = ui.onFrame(function() b = b + 1; stopB() end)
        ui.__tick(1.5)
        assert(a == 1 and b == 1 and seen == 1.5)
        ui.__tick(2)
        assert(a == 2 and b == 1, "self-removed callback ran again")
        stopA()
        ui.__tick(3)
        assert(a == 2, "removed callback ran")
    )");
}

TEST_F(LuaLibraryTest, RadioButtonsWithAGroupShareAMemberSet) {
    run(R"(
        local a = ui.RadioButton { text = "A", group = "size" }
        local b = ui.RadioButton { text = "B", group = "size" }
        local c = ui.RadioButton { text = "C", group = "color" }
        local d = ui.RadioButton { text = "D" }
        assert(a.__type == "RadioButton")
        assert(a.__radioGroup ~= nil and a.__radioGroup == b.__radioGroup)
        assert(a.__radioGroup == ui.__radioGroups.size)
        assert(a.__radioGroup[a] and a.__radioGroup[b] and not a.__radioGroup[c])
        assert(c.__radioGroup == ui.__radioGroups.color and c.__radioGroup ~= a.__radioGroup)
        assert(d.__radioGroup == nil)
    )");
}

TEST_F(LuaLibraryTest, NativeWrappersAreExposed) {
    run(R"(
        local native = require("imlui.native")
        assert(ui.isDark == native.isDark)
        assert(ui.deltaTime == native.deltaTime)
        assert(ui.imguiVersion == native.imguiVersion)
    )");
}
