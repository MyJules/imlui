// Script errors that used to escape every protected call (reaching Lua's panic handler, which aborts) or to cut
// a frame short: errors in a two-way bind's source, in the render pass, and in timer and frame callbacks.
#include <gtest/gtest.h>

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.element_scope;
import imlui.lua.listeners;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.checkbox;
import imlui.test.headless_frame;
import imlui.test.render_counter;

using namespace imlui;

namespace {

/// While `armed`, opens ImGui state (a style var, an ID, a child window) and raises a Lua error without
/// protection, mid-render: what a Lua API call that hits an error deep in an element does.
class Raiser final : public Element {
public:
    static void declare(TypeBuilder<Raiser>& type) { type.property("armed", &Raiser::armed, &Raiser::setArmed); }

    void render(Frame&) override {
        if (!m_armed) return;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        ImGui::PushID("raiser");
        ImGui::BeginChild("##raiser", {10.0f, 10.0f});
        lua::error(luaState(), "render exploded");
    }

    [[nodiscard]] bool armed() const { return m_armed; }
    void setArmed(bool armed) { m_armed = armed; }

private:
    bool m_armed = false;
};

class LuaErrorsTest : public ::testing::Test {
protected:
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    static bool hasError(std::string_view part) {
        for (const auto& e : luaui::errors())
            if (e.find(part) != std::string::npos) return true;
        return false;
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// A watcher that fails while a widget writes its bound state (s.on = true through bind(s, "on")) is reported;
// the write-back finishes and the widget's signal still runs.
TEST_F(LuaErrorsTest, WatcherErrorDuringBindWriteBackIsReported) {
    run(R"(
        s = state { on = false, toggled = {} }
        ui.watch(s, "on", function() error("watcher exploded") end)
        root = Checkbox { "Wi-Fi", checked = bind(s, "on"),
                          onToggled = function(_, v) s.toggled[#s.toggled + 1] = v end })");
    m_h.renderFrame("root");
    m_h.click("root", 28, 28);
    run("assert(s.on == true); assert(s.toggled[1] == true)");
    EXPECT_TRUE(hasError("watcher exploded"));
}

// A Lua error raised without protection in the middle of rendering is reported, the ImGui state it left open
// is unwound (the frame ends without an assert), and the next frame renders normally.
TEST_F(LuaErrorsTest, UnprotectedErrorWhileRenderingIsReportedAndImGuiRecovers) {
    m_h.registerType<Raiser>("Raiser");
    m_h.registerType<test::RenderCounter>("Counter");
    run(R"(
        s = state { armed = true }
        root = Column { Raiser { armed = bind(s, "armed") }, Counter { id = "counter" } })");
    m_h.renderFrame("root");
    EXPECT_TRUE(hasError("render exploded"));
    EXPECT_EQ(m_h.instance<test::RenderCounter>("counter")->renders(), 0); // the pass stopped at the error

    luaui::clearErrors();
    run("s.armed = false");
    m_h.renderFrames("root", 2);
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(m_h.instance<test::RenderCounter>("counter")->renders(), 2);
}

// One failing timer or frame callback doesn't skip the others.
TEST_F(LuaErrorsTest, FailingTimersAndFrameCallbacksDontSkipTheOthers) {
    run(R"(
        s = { timers = 0, frames = 0 }
        ui.setTimeout(function() error("timer exploded") end, 0)
        ui.setTimeout(function() s.timers = s.timers + 1 end, 0)
        ui.setTimeout(function() error("timer exploded") end, 0)
        ui.onFrame(function() error("frame exploded") end)
        ui.onFrame(function() s.frames = s.frames + 1 end)
        root = Column {})");
    m_h.renderFrames("root", 2);
    run("assert(s.timers == 1, s.timers); assert(s.frames == 2, s.frames)");
    EXPECT_TRUE(hasError("timer exploded"));
    EXPECT_TRUE(hasError("frame exploded"));
    EXPECT_TRUE(hasError("stack traceback"));
}

TEST_F(LuaErrorsTest, RequestFrameRejectsNonFiniteDelays) {
    EXPECT_NE(m_h.run("ui.requestFrame(math.huge)").find("finite"), std::string::npos);
    EXPECT_NE(m_h.run("ui.native.requestFrame(0 / 0)").find("finite"), std::string::npos);
    EXPECT_NE(m_h.run("ui.native.requestFrame(-math.huge)").find("finite"), std::string::npos);
    run("ui.requestFrame(5); ui.native.requestFrame()");
}

// A foreign C++ exception (not a std::exception, not turned into a Lua error) that reaches safeCall's pcall ends it with
// status -1, which leaves no message on the stack: it is reported as "unknown C++ exception".
TEST(SafeCallTest, ForeignExceptionIsReportedWithAMessage) {
    luaui::clearErrors();
    lua::State* state = lua::newstate();
    lua::pushstring(state, "below");
    lua::pushcfunction(state, [](lua::State*) -> int { throw 42; });
    EXPECT_FALSE(luaui::safeCall(state, 0, 0));
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_EQ(luaui::errors().front(), "unknown C++ exception");
    EXPECT_EQ(lua::gettop(state), 1); // only what was below the call
    lua::close(state);
    luaui::clearErrors();
}

// Showing a value as text (a Text's boolean or table, a Table cell) is a read: it isn't counted as a possible change
// of the Lua state (luaui::changes()), which would make every element sync again. A raising __tostring is reported.
TEST(SafeCallTest, DisplayingAValueIsNotAChange) {
    luaui::clearErrors();
    lua::State* state = lua::newstate();
    const std::uint64_t before = luaui::changes();
    lua::pushboolean(state, true);
    EXPECT_EQ(luaui::toDisplayString(state, -1), "true");
    lua::newtable(state);
    EXPECT_TRUE(luaui::toDisplayString(state, -1).starts_with("table: "));
    EXPECT_EQ(luaui::changes(), before);
    EXPECT_EQ(lua::gettop(state), 2);
    lua::newtable(state); // a metatable whose __tostring raises
    lua::pushcfunction(state, [](lua::State* s) -> int { lua::error(s, "no text"); });
    lua::setfield(state, -2, "__tostring");
    lua::setmetatable(state, -2);
    EXPECT_EQ(luaui::toDisplayString(state, -1), "");
    EXPECT_EQ(luaui::errors().size(), 1u);
    EXPECT_EQ(lua::gettop(state), 2);
    lua::close(state);
    luaui::clearErrors();
}

// With the Lua stack full (lua::checkstack fails: LUAI_MAXSTACK slots in use), the helpers that push before a call
// report a stack overflow and bail out instead of writing past the stack.
TEST(SafeCallTest, AFullStackIsReportedNotOverrun) {
    luaui::clearErrors();
    lua::State* state = lua::newstate();
    lua::newtable(state); // a value with a __tostring, which toDisplayString would call
    lua::newtable(state);
    lua::pushcfunction(state, [](lua::State* s) -> int {
        lua::pushstring(s, "text");
        return 1;
    });
    lua::setfield(state, -2, "__tostring");
    lua::setmetatable(state, -2);
    // The first failing checkstack grows the stack to its error size (LUAI_MAXSTACK plus room for an error message)
    // and fails; only once that room is used too does it fail every time.
    while (lua::checkstack(state, 1) || lua::checkstack(state, 1))
        lua::pushnil(state);
    const int top = lua::gettop(state);

    EXPECT_EQ(luaui::toDisplayString(state, 1), "");
    EXPECT_FALSE(luaui::callListeners(state, 1, "onClicked", 1, 0));
    EXPECT_EQ(luaui::scopedArgs(state, 1, 0), 0);
    EXPECT_EQ(lua::gettop(state), top);
    ASSERT_EQ(luaui::errors().size(), 3u);
    for (const std::string& e : luaui::errors())
        EXPECT_TRUE(e.starts_with("stack overflow")) << e;

    lua::settop(state, 1); // with room again, the same value converts
    EXPECT_EQ(luaui::toDisplayString(state, 1), "text");
    lua::close(state);
    luaui::clearErrors();
}
