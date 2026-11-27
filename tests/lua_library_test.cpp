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

// A write notifies the watchers there were when it happened: one added by a watcher runs from the next write on, one
// removed by a watcher that runs first is still called this time. A watcher that writes a state again (this one or
// another) notifies in turn without disturbing the outer notification.
TEST_F(LuaLibraryTest, WatchersAddedOrRemovedWhileNotifyingWaitForTheNextWrite) {
    run(R"(
        local s, other = ui.state { x = 0 }, ui.state { y = 0 }
        local log = {}
        local stopB
        ui.watch(s, "x", function(v)
            log[#log + 1] = "a" .. v
            if v == 1 then
                stopB()
                ui.watch(s, "x", function(w) log[#log + 1] = "c" .. w end)
                other.y = v -- nested notification
            end
        end)
        stopB = ui.watch(s, "x", function(v) log[#log + 1] = "b" .. v end)
        ui.watch(other, "*", function(v) log[#log + 1] = "y" .. v end)
        s.x = 1
        assert(table.concat(log, " ") == "a1 y1 b1", table.concat(log, " "))
        s.x = 2
        assert(table.concat(log, " ") == "a1 y1 b1 a2 c2", table.concat(log, " "))
    )");
}

// The frame callbacks of a frame are those there were when it started, however many (more than table.unpack can
// return), and a callback that adds or removes callbacks, or runs a tick itself, doesn't change who runs this frame.
TEST_F(LuaLibraryTest, FrameCallbacksRunFromASnapshotOfAnySize) {
    run(R"(
        local calls = 0
        local function count() calls = calls + 1 end
        local stops = {}
        for i = 1, 1000001 do stops[i] = ui.onFrame(count) end
        ui.__tick(1)
        assert(calls == 1000001, calls)
        for i = #stops, 1, -1 do stops[i]() end -- last first: a removal looks for its callback from the end

        local log = {}
        local stopB, nested
        ui.onFrame(function(t)
            log[#log + 1] = "a" .. t
            if t == 1 then
                stopB()
                ui.onFrame(function(u) log[#log + 1] = "c" .. u end)
                nested = true
                ui.__tick(9) -- runs a, the new c and not b; the outer frame still goes on to b
                nested = false
            end
        end)
        stopB = ui.onFrame(function(t) log[#log + 1] = "b" .. t end)
        ui.__tick(1)
        assert(table.concat(log, " ") == "a1 a9 c9 b1", table.concat(log, " "))
        ui.__tick(2)
        assert(table.concat(log, " ") == "a1 a9 c9 b1 a2 c2", table.concat(log, " "))
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

TEST_F(LuaLibraryTest, LuaSideContainersRegisterTheirIds) {
    run(R"(
        local rep = ui.Repeater { id = "rep", model = 2, delegate = function() return ui.Text {} end }
        local list = ui.ListView { id = "list", model = 1, delegate = function() return ui.Text {} end }
        local plain = ui.ListView { id = "plainList", ui.Text {} }
        local cond = ui.If { id = "cond", condition = true }
        local loader = ui.Loader { id = "loader", item = ui.Text {} }
        local resp = ui.Responsive { id = "resp", compact = ui.Text {} }
        assert(ui.env.rep == rep and ui.env.list == list and ui.env.plainList == plain)
        assert(ui.env.cond == cond and ui.env.loader == loader and ui.env.resp == resp)
        assert(#reported == 0, tostring(reported[1]))

        -- The same collision checks as the other elements.
        ui.Loader { id = "io" }
        assert(#reported == 1 and reported[1]:find("element id 'io' collides with Lua global 'io'", 1, true),
               tostring(reported[1]))
        ui.Repeater { id = "bind", model = 0, delegate = function() end }
        assert(#reported == 2 and reported[2]:find("collides with ui member 'ui.bind'", 1, true), tostring(reported[2]))
    )");
}

// Without the C++ types (plain element tables, no metatable before), parent links and scopes work the same.
TEST_F(LuaLibraryTest, ParentLinksAndScopesWithoutRegisteredTypes) {
    run(R"(
        local leaf = ui.Text { "x" }
        local col = ui.Column { leaf }
        assert(leaf.parent == col and col.parent == nil)
        assert(getmetatable(leaf) == getmetatable(col), "untyped elements share one metatable")
        col.spacing = 4
        assert(rawget(col, "spacing") == 4)

        local C = ui.component(function(props, ids) return ui.Column { ui.Text { id = "t" }, ids = ids } end)
        local a, b = C {}, C {}
        assert(a.ids.t == a[1] and b.ids.t == b[1] and ui.ids.t == nil)
        assert(#reported == 0, tostring(reported[1]))
    )");
}

// A watcher, a frame callback and a timer set up while a delegate is built belong to its scope: a delegate rebuilt
// again and again leaves none of them behind (they don't run, aren't kept, and don't keep the delegate's elements or
// its scope alive). At app level they stay, as before.
TEST_F(LuaLibraryTest, ScopedCallbacksGoWithTheirDelegate) {
    run(R"(
        ui.native.requestFrame = function() end
        local s = ui.state { x = 0 }
        local writes, frames, ticks, appWrites = 0, 0, 0, 0
        ui.watch(s, "x", function() appWrites = appWrites + 1 end)
        local aliveElements = setmetatable({}, { __mode = "k" })
        local aliveScopes = setmetatable({}, { __mode = "k" })
        local items = {}
        local function refill() for i = 1, 10 do items[i] = { i } end end -- new items: every delegate is rebuilt
        refill()
        local list = ui.ListView { model = items, delegate = function(item, i, ids)
            local t = ui.Text { text = tostring(item[1]) }
            -- Each callback refers to the element: kept by them, the delegate would never be collected.
            ui.watch(s, "x", function() writes = writes + 1; t.seen = true end)
            ui.watch(s, "never", function() t.seen = true end) -- a key nobody writes: pruned as it grows
            ui.onFrame(function() frames = frames + 1; t.seen = true end)
            ui.setInterval(function() ticks = ticks + 1; t.seen = true end, 1)
            aliveElements[t] = true
            aliveScopes[ids] = true
            return t
        end }
        -- Rebuilds every delegate `times` times, with a frame now and then (it drops the timers of collected scopes).
        local function rebuild(times)
            for round = 1, times do
                refill()
                list.__count(list)
                list.__range(list, 1, 10)
                if round % 50 == 0 then
                    collectgarbage()
                    ui.__tick(0)
                end
            end
            collectgarbage()
            collectgarbage()
            ui.__tick(0)
            collectgarbage()
            writes, frames, ticks = 0, 0, 0
        end
        local function count(t)
            local n = 0
            for _ in pairs(t) do n = n + 1 end
            return n
        end
        rebuild(50)
        assert(count(aliveElements) == 10, count(aliveElements) .. " delegates alive")
        assert(count(aliveScopes) == 10, count(aliveScopes) .. " delegate scopes alive")
        s.x = 1
        assert(writes == 10 and appWrites == 1, writes .. " watchers ran")
        ui.__tick(1)
        assert(frames == 10, frames .. " frame callbacks ran")
        assert(ticks == 10, ticks .. " timers fired")

        -- Many more rebuilds: what the dropped delegates set up doesn't pile up.
        local before = collectgarbage("count")
        rebuild(500)
        local grown = collectgarbage("count") - before
        assert(grown < 64, ("%.0f KB more after 500 rebuilds"):format(grown))
        s.x = 2
        ui.__tick(2)
        assert(writes == 10 and frames == 10 and appWrites == 2, writes .. " watchers, " .. frames .. " frame callbacks")

        -- Removing one by hand still works, and its scope's table lets it go.
        local stop
        local C = ui.component(function()
            stop = ui.watch(s, "x", function() writes = writes + 1 end)
            return ui.Text {}
        end)
        local keep = C {}
        writes = 0
        s.x = 3
        assert(writes == 11, writes)
        stop()
        s.x = 4
        assert(writes == 21, writes)
        assert(keep ~= nil)
    )");
}

// A Repeater's key that is nil or NaN falls back to the item; an item that is NaN (or nil) too has one stand-in key, so
// it neither raises ("table index is NaN") nor rebuilds its delegate every call.
TEST_F(LuaLibraryTest, RepeaterKeysThatCantIndexATable) {
    run(R"(
        local built = 0
        local items = { 0/0, "a", 0/0 }
        local rep = ui.Repeater { model = function() return items end, key = function(item) return item end,
                                  delegate = function() built = built + 1; return ui.Text {} end }
        local first = rep.__children(rep)
        assert(#first == 3 and built == 3, built)
        rep.__children(rep)
        rep.__children(rep)
        assert(built == 3, built .. " delegates built: rebuilt every call")
        items = { "a", 0/0 }
        local list = rep.__children(rep)
        assert(#list == 2 and built == 3, built)
        local nilKeys = ui.Repeater { model = { 0/0, 1 }, key = function() return nil end,
                                      delegate = function() return ui.Text {} end }
        assert(#nilKeys.__children(nilKeys) == 2)
        assert(#reported == 0, tostring(reported[1]))
    )");
}

// A keyed ListView over a plain Lua table counts the model's keys again only when an item changed: each render then
// asks only for the keys of the items it shows. The list __range returns is reused by the next call.
TEST_F(LuaLibraryTest, AKeyedListViewOverALuaTableCountsKeysOnlyWhenItemsChange) {
    run(R"(
        local calls = 0
        local items = {}
        for i = 1, 20 do items[i] = { id = i } end
        local list = ui.ListView { model = items, key = function(item) calls = calls + 1; return item.id end,
                                   delegate = function() return ui.Text {} end }
        local function render(last)
            list.__count(list)
            return list.__range(list, 1, last or 5)
        end
        render()
        render() -- counts the keys once, now that there are entries
        calls = 0
        for _ = 1, 10 do render() end
        assert(calls == 50, calls .. " key calls: the keys were counted again")
        assert(list.__view.atIndex == nil, "atIndex is for list models")

        -- An item replaced: counted again, and the entry of the key that went away is dropped.
        local three = list.__view.keyed[3]
        assert(three ~= nil)
        items[3] = { id = 99 }
        calls = 0
        render()
        assert(calls > 5 and list.__view.keyed[3] == nil and list.__view.keyed[99] ~= nil, calls)
        calls = 0
        render()
        assert(calls == 5, calls)

        -- The same items behind another key function: counted again.
        list.key = function(item) calls = calls + 1; return -item.id end
        calls = 0
        render()
        assert(calls > 5 and list.__view.keyed[1] == nil and list.__view.keyed[-1] ~= nil, calls)

        local a = render(5)
        local b = render(3)
        assert(a == b and #b == 3 and b[1] ~= nil)
    )");
}

// A group's member set lives as long as its members: one per `group` value ever used would otherwise pile up.
TEST_F(LuaLibraryTest, RadioGroupsGoWithTheirLastMember) {
    run(R"(
        for i = 1, 100 do ui.RadioButton { text = "x", group = "temp" .. i } end
        local kept = ui.RadioButton { text = "kept", group = "kept" }
        collectgarbage()
        collectgarbage()
        local n = 0
        for _ in pairs(ui.__radioGroups) do n = n + 1 end
        assert(n == 1, n .. " groups left")
        assert(ui.__radioGroups.kept == kept.__radioGroup and kept.__radioGroup[kept])
        local again = ui.RadioButton { text = "again", group = "kept" }
        assert(again.__radioGroup == kept.__radioGroup)
    )");
}

// What `debug` scripts see is up to imlui.native's `fullDebug` (luaui::fullDebugLibrary(): IMLUI_DEV_PATHS): false
// leaves them debug.traceback alone, also through require("debug"), true or absent (a stand-in native) the whole
// library. Both modes in every build: the stub native sets the flag.
TEST(LuaLibraryDebug, ScriptsGetTheWholeDebugLibraryOnlyWhenNativeSaysSo) {
    for (const char* flag : {"false", "true", "nil"}) {
        SCOPED_TRACE(flag);
        lua::State* state = lua::newstate();
        lua::openlibs(state);
        const std::string boot = std::string{"package.path = '"} + IMLUI_RESOURCES_DIR + "/?.lua;' .. package.path\n" +
                                 "package.preload['imlui.native'] = function() return { time = function() return 0 " +
                                 "end, log = function() end, reportError = function(m) reported = m end, fullDebug = " + flag +
                                 " } end\n" + R"(
            local whole = debug
            local ui = require("imlui")
            local full = ui.native.fullDebug ~= false
            assert(require("debug") == debug)
            assert(type(debug.traceback) == "function")
            if full then
                assert(debug == whole and type(debug.getinfo) == "function")
            else
                assert(debug ~= whole and next(debug) == "traceback" and next(debug, "traceback") == nil)
                assert(debug.getregistry == nil and debug.sethook == nil and debug.getupvalue == nil)
            end
            -- The DSL still has the whole library: a duplicate id report names the component by where it is defined.
            local Twice = ui.component(function()
                ui.registerId({ id = "same" })
                ui.registerId({ id = "same" })
                return {}
            end)
            Twice {}
            assert(reported and reported:find("component (test:", 1, true), tostring(reported)))";
        ASSERT_EQ(lua::loadbuffer(state, boot, "=test"), lua::ok) << lua::tostring(state, -1);
        EXPECT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
        lua::close(state);
    }
}
