// C++ code reached from Lua that fails: exceptions from element hooks (componentComplete, tick, destroyed),
// from the host's NativeHooks and from methods never unwind through Lua's C frames; argument checks of the
// native library; a Lua error during a property sync doesn't freeze the element; setInterval's checks.
#include <gtest/gtest.h>

import std;

import lua;
import imlui.lua.errors;
import imlui.lua.native;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.frame_request;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

struct Calls {
    int ticks = 0;
    int completes = 0;
    int destroyed = 0;
    int pokes = 0;
};
Calls calls;

/// Throws from every hook that has its flag set.
class Exploder final : public Element {
public:
    static void declare(TypeBuilder<Exploder>& type) {
        type.property("failComplete", &Exploder::failComplete, &Exploder::setFailComplete)
            .property("failTick", &Exploder::failTick, &Exploder::setFailTick)
            .property("failDestroyed", &Exploder::failDestroyed, &Exploder::setFailDestroyed)
            .property("ticking", &Exploder::ticking, &Exploder::setTickingProp)
            .method("poke", &Exploder::poke)
            .method("throwInt", &Exploder::throwInt);
    }

    void render(Frame&) override {}
    void componentComplete() override {
        ++calls.completes;
        if (m_failComplete) throw std::runtime_error("complete exploded");
    }
    void tick(double) override {
        ++calls.ticks;
        if (m_failTick) throw std::runtime_error("tick exploded");
    }
    void destroyed() override {
        ++calls.destroyed;
        if (m_failDestroyed) throw std::runtime_error("destroyed exploded");
    }

    [[nodiscard]] bool failComplete() const { return m_failComplete; }
    void setFailComplete(bool on) { m_failComplete = on; }
    [[nodiscard]] bool failTick() const { return m_failTick; }
    void setFailTick(bool on) { m_failTick = on; }
    [[nodiscard]] bool failDestroyed() const { return m_failDestroyed; }
    void setFailDestroyed(bool on) { m_failDestroyed = on; }
    [[nodiscard]] bool ticking() const { return isTicking(); }
    void setTickingProp(bool on) { setTicking(on); }

    int poke() { return ++calls.pokes; }
    [[noreturn]] void throwInt() { throw 42; }

private:
    bool m_failComplete = false;
    bool m_failTick = false;
    bool m_failDestroyed = false;
};

bool hasError(std::string_view part) {
    for (const auto& e : luaui::errors())
        if (e.find(part) != std::string::npos) return true;
    return false;
}

class LuaGuardsTest : public ::testing::Test {
protected:
    void SetUp() override {
        calls = {};
        m_h.registerType<Exploder>("Exploder");
    }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// componentComplete() that throws is reported (from a render and from a method call's lazy sync); the element
// keeps working.
TEST_F(LuaGuardsTest, ThrowingComponentCompleteIsReported) {
    run(R"(
        a = Exploder { failComplete = true }
        b = Exploder { failComplete = true }
        root = Column { a })");
    m_h.renderFrames("root", 2);
    EXPECT_TRUE(hasError("Exploder.componentComplete(): complete exploded"));
    luaui::clearErrors();
    run("assert(b:poke() == 1); assert(b:poke() == 2)"); // b never rendered: synced (and completed) by the call
    EXPECT_TRUE(hasError("Exploder.componentComplete(): complete exploded"));
    EXPECT_EQ(calls.completes, 2);
}

// A tick() that throws is reported, and the other ticking elements still tick.
TEST_F(LuaGuardsTest, ThrowingTickIsReportedAndTheOthersStillTick) {
    run(R"(
        a = Exploder { ticking = true, failTick = true }
        b = Exploder { ticking = true }
        root = Column { a, b })");
    m_h.renderFrames("root", 3); // ticking starts with the first sync, in the first frame's render
    EXPECT_TRUE(hasError("Exploder.tick(): tick exploded"));
    EXPECT_EQ(calls.ticks, 4); // both, in frames 2 and 3
}

// destroyed() that throws in __gc is reported (a finalizer can't raise), and the instance is still deleted.
TEST_F(LuaGuardsTest, ThrowingDestroyedIsReportedFromTheFinalizer) {
    run("a = Exploder { failDestroyed = true }; a:poke(); a = nil; collectgarbage(); collectgarbage()"); // poke: synced
    EXPECT_EQ(calls.destroyed, 1);
    EXPECT_TRUE(hasError("Exploder.destroyed(): destroyed exploded"));
}

// A method that throws something that isn't a std::exception raises a Lua error too.
TEST_F(LuaGuardsTest, MethodThrowingANonStdExceptionRaisesALuaError) {
    run("a = Exploder {}");
    EXPECT_NE(m_h.run("a:throwInt()").find("Exploder:throwInt: unknown C++ exception"), std::string::npos);
    run("assert(a:poke() == 1)");
}

// textSize used to hand a null range to ImGui for a non-string argument (strlen(nullptr)).
TEST_F(LuaGuardsTest, TextSizeChecksItsArgument) {
    EXPECT_NE(m_h.run("ui.native.textSize(nil)").find("textSize expects a string"), std::string::npos);
    EXPECT_NE(m_h.run("ui.native.textSize({})").find("textSize expects a string"), std::string::npos);
    run("local w, h = ui.native.textSize('abc'); assert(w > 0 and h > 0)");
    run("local w, h = ui.native.textSize(12); assert(w > 0 and h > 0)");
}

// An interval of nil, 0, a negative or a non-finite number would fire every frame (or never): an error.
TEST_F(LuaGuardsTest, SetIntervalRejectsNonPositiveAndNonFiniteIntervals) {
    for (const char* ms : {"", ", 0", ", -5", ", math.huge", ", 0 / 0", ", '10'"}) {
        const std::string error = m_h.run(std::string{"ui.setInterval(function() end"} + ms + ")");
        EXPECT_NE(error.find("setInterval: the interval must be a positive, finite number"), std::string::npos)
            << ms << ": " << error;
    }
    run("local id = ui.setInterval(function() end, 10); ui.clearTimer(id); ui.setTimeout(function() end)");
}

// A host hook that throws becomes a Lua error of the native function instead of unwinding through Lua.
TEST(NativeHooksTest, ThrowingHooksRaiseLuaErrors) {
    luaui::NativeHooks hooks;
    hooks.quit = [] { throw std::runtime_error("quit exploded"); };
    hooks.setTitle = [](const std::string&) { throw 7; };
    lua::State* state = lua::newstate();
    lua::openlibs(state);
    luaui::registerNative(state, &hooks);
    const auto error = [&](std::string_view code) {
        EXPECT_EQ(lua::loadbuffer(state, code, "=test"), lua::ok);
        if (lua::pcall(state, 0, 0, 0) == lua::ok) return std::string{};
        std::string msg{lua::tostring(state, -1)};
        lua::pop(state);
        return msg;
    };
    EXPECT_NE(error("require('imlui.native').quit()").find("imlui.native.quit: quit exploded"), std::string::npos);
    EXPECT_NE(error("require('imlui.native').setTitle('x')").find("imlui.native.setTitle: unknown C++ exception"),
              std::string::npos);
    EXPECT_EQ(error("require('imlui.native').reload()"), ""); // an empty hook is a no-op
    lua::close(state);
}
