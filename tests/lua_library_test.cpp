// Tests for src/resources/imlui.lua itself, loaded in a plain Lua state with a stub `imlui.native`.
#include <gtest/gtest.h>

import lua;

namespace {

class LuaLibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_state = lua::newstate();
        lua::openlibs(m_state);
        // Only what imlui.lua touches outside of callbacks; the rest may stay nil.
        std::string boot = std::string{"package.path = '"} + IMLUI_RESOURCES_DIR + "/?.lua;' .. package.path\n" +
                           R"(package.preload["imlui.native"] = function()
                                  local now = 0
                                  reported = {}
                                  return { time = function() return now end, log = function() end,
                                           reportError = function(msg) reported[#reported + 1] = msg end }
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

TEST_F(LuaLibraryTest, PickSizeClassFallsBackToTheNearestSmallerClass) {
    run(R"(
        local p = { medium = "M", large = "L" }
        assert(ui.pickSizeClass(p, "compact") == "M", "no smaller class: the smallest defined one")
        assert(ui.pickSizeClass(p, "medium") == "M")
        assert(ui.pickSizeClass(p, "expanded") == "M", "expanded falls back to medium")
        assert(ui.pickSizeClass(p, "large") == "L")
        assert(ui.pickSizeClass(p, "extraLarge") == "L")
        assert(ui.pickSizeClass(p, "bogus") == "M", "unknown names count as compact")
        assert(ui.pickSizeClass({}, "large") == nil)
        local q = { compact = false, expanded = "E" }
        assert(ui.pickSizeClass(q, "compact") == false, "false is a defined class")
        assert(ui.pickSizeClass(q, "medium") == false)
        assert(ui.pickSizeClass(q, "extraLarge") == "E")
    )");
}

TEST_F(LuaLibraryTest, ResponsiveShowsTheElementForTheCurrentSizeClass) {
    run(R"(
        local class = "compact"
        ui.sizeClass = function() return class end
        local phone, tablet, desktop = ui.Column {}, ui.Column {}, ui.Row {}
        local r = ui.Responsive { compact = phone, medium = tablet, expanded = desktop, large = false }
        assert(r.__type == "Responsive" and r.__inline)
        local function shown() return r:__children()[1] end
        assert(shown() == phone)
        class = "medium"; assert(shown() == tablet)
        class = "expanded"; assert(shown() == desktop)
        class = "large"; assert(shown() == nil, "false shows nothing")
        class = "extraLarge"; assert(shown() == nil)
        class = "compact"; assert(shown() == phone, "the same element instance comes back")
    )");
}

TEST_F(LuaLibraryTest, ElementIdsThatCollideWithUiMembersOrGlobalsAreReported) {
    run(R"(
        local shadowed = ui.Column { id = "io" }
        assert(#reported == 1, "no error for an id that a global shadows")
        assert(reported[1] == "element id 'io' collides with Lua global 'io'; it can't be referenced by name — rename it",
               reported[1])
        assert(ui.env.io == io, "the lookup order stays: ui, then globals, then ids")
        assert(ui.ids.io == shadowed)

        ui.Text { id = "state" }
        assert(#reported == 2)
        assert(reported[2]:find("element id 'state' collides with ui member 'ui.state'", 1, true), reported[2])

        local field = ui.TextField { id = "nameField" }
        local count = ui.Row { id = 3 } -- not a name: nothing to collide with
        assert(#reported == 2, "a free id was reported: " .. tostring(reported[3]))
        assert(ui.env.nameField == field)
    )");
}
