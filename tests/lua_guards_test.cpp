// C++ code reached from Lua that fails: exceptions from element hooks (componentComplete, tick, destroyed),
// from the host's NativeHooks and from methods become script errors or Lua errors with a message; argument checks of
// the native library, and non-finite colors; a Lua error during a property sync doesn't freeze the element;
// setInterval's checks; the ticking list by id, so an element destroyed mid-tick isn't replaced by one created at its
// address.
#include <gtest/gtest.h>

import std;

import imgui;
import lua;
import imlui.lua.errors;
import imlui.lua.native;
import imlui.theme.scheme;
import imlui.theme.theme;
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

/// A ticking element allocated in a single slot while the slot is free: a Recycled created after another one died
/// lives at the dead one's address, which forces the address reuse a general allocator does by chance.
class Recycled final : public Element {
public:
    static void declare(TypeBuilder<Recycled>& type) {
        type.property("ticking", &Recycled::ticking, &Recycled::setTickingProp).method("ticks", &Recycled::ticks);
    }
    static void* operator new(std::size_t size) {
        if (!m_slotUsed && size <= sizeof(m_slot)) {
            m_slotUsed = true;
            return m_slot;
        }
        return ::operator new(size);
    }
    static void operator delete(void* p) {
        if (p == static_cast<void*>(m_slot))
            m_slotUsed = false;
        else
            ::operator delete(p);
    }

    Recycled() { m_addresses.push_back(this); }
    void render(Frame&) override {}
    void tick(double) override {
        ++m_ticks;
        emit("onTick");
    }
    [[nodiscard]] bool ticking() const { return isTicking(); }
    void setTickingProp(bool on) { setTicking(on); }
    [[nodiscard]] int ticks() const { return m_ticks; }

    /// Where each Recycled was constructed, in order.
    inline static std::vector<const void*> m_addresses;

private:
    alignas(std::max_align_t) inline static std::byte m_slot[512];
    inline static bool m_slotUsed = false;
    int m_ticks = 0;
};

/// A visual element whose constructor throws. Counts the attempts.
class Bomb final : public Element {
public:
    static void declare(TypeBuilder<Bomb>&) {}
    Bomb() {
        ++m_constructions;
        throw std::runtime_error("bomb constructor exploded");
    }
    void render(Frame&) override {}
    inline static int m_constructions = 0;
};

/// A ticking element that never renders: start() turns ticking on.
class TickOnly final : public Element {
public:
    void render(Frame&) override {}
    void start() { setTicking(true); }
    void stop() { setTicking(false); }
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
        m_h.registerType<Recycled>("Recycled");
        m_h.registerType<Bomb>("Bomb");
        Recycled::m_addresses.clear();
        Bomb::m_constructions = 0;
    }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// A constructor that throws is reported once and not tried again: the table remembers the failure, so rendering it
// (and a Row asking for its size) doesn't construct it, and report the error, on every query and frame.
TEST_F(LuaGuardsTest, AThrowingConstructorIsTriedOnce) {
    run(R"(
        bomb = Bomb { }
        root = Row { Column { }, bomb })");
    m_h.renderFrames("root", 4);
    EXPECT_EQ(Bomb::m_constructions, 1);
    EXPECT_EQ(secondsUntilFrame(), std::numeric_limits<double>::infinity()); // settled: the Row stops asking
    int reported = 0;
    for (const auto& e : luaui::errors())
        if (e.find("Bomb: constructor failed: bomb constructor exploded") != std::string::npos) ++reported;
    EXPECT_EQ(reported, 1);
    EXPECT_EQ(m_h.syncElement("bomb"), nullptr);
    EXPECT_EQ(Bomb::m_constructions, 1);
}

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

// The ticking list holds elements by id, never reused: an element destroyed and another one created at its address
// (forced here with placement new) can't take its entry, so a stale id finds nothing.
TEST(ElementTicking, AStaleIdDoesntFindAnElementAtTheSameAddress) {
    alignas(TickOnly) std::byte storage[sizeof(TickOnly)];
    auto* a = new (storage) TickOnly;
    a->start();
    const std::uint64_t idA = a->objectId();
    EXPECT_EQ(Element::findTicking(idA), a);
    EXPECT_EQ(std::ranges::count(Element::tickingElements(), idA), 1);
    a->~TickOnly();
    EXPECT_EQ(Element::findTicking(idA), nullptr);
    auto* b = new (storage) TickOnly;
    b->start();
    ASSERT_EQ(static_cast<void*>(a), static_cast<void*>(b));
    EXPECT_GT(b->objectId(), idA);
    EXPECT_EQ(Element::findTicking(idA), nullptr); // the old entry can't answer for the new element
    EXPECT_EQ(Element::findTicking(b->objectId()), b);
    EXPECT_TRUE(b->isTicking());
    b->~TickOnly();
    EXPECT_EQ(Element::findTicking(b->objectId()), nullptr);
}

// The ticking list is looked up by id and keeps its order through stops (it leaves stale entries and sweeps them in
// bulk) and restarts: an element that stops and starts again ticks last, once, and a stopped one isn't found.
TEST(ElementTicking, TheOrderSurvivesStopsAndARestartTicksLast) {
    std::vector<std::unique_ptr<TickOnly>> elements(64);
    for (auto& e : elements) {
        e = std::make_unique<TickOnly>();
        e->start();
    }
    // Ours only: other suites' elements may still tick.
    const auto ours = [&] {
        std::vector<std::uint64_t> ids;
        for (const std::uint64_t id : Element::tickingElements())
            if (std::ranges::any_of(elements, [id](const auto& e) { return e && e->objectId() == id; }))
                ids.push_back(id);
        return ids;
    };
    for (std::size_t i = 1; i < elements.size(); ++i)
        if (i % 4 != 0) elements[i]->stop(); // three in four: the stale entries outnumber the live ones
    elements[0]->stop();
    elements[0]->start();
    elements[0]->start(); // already ticking: no second entry
    elements[8].reset();  // destroyed while ticking
    std::vector<std::uint64_t> expected;
    for (std::size_t i = 4; i < elements.size(); i += 4)
        if (elements[i]) expected.push_back(elements[i]->objectId());
    expected.push_back(elements[0]->objectId());
    EXPECT_EQ(ours(), expected);
    EXPECT_EQ(Element::findTicking(elements[1]->objectId()), nullptr);
    EXPECT_FALSE(elements[1]->isTicking());
    EXPECT_EQ(Element::findTicking(elements[4]->objectId()), elements[4].get());
    std::vector<std::uint64_t> reused{1, 2, 3}; // the overload that fills a buffer clears it first
    Element::tickingElements(reused);
    EXPECT_EQ(reused, Element::tickingElements());
}

// A tick() whose handler destroys a ticking element that comes later in this frame's loop and creates one at its
// address: the loop skips the dead element's entry instead of ticking the new one in its place (it ticks from the
// next frame on, like any element that starts ticking mid-loop).
TEST_F(LuaGuardsTest, AnElementDestroyedMidTickIsNotReplacedByOneAtItsAddress) {
    run(R"(
        victim = Recycled { ticking = true } -- in the slot
        killer = Recycled { ticking = true,
            onTick = function()
                if done then return end
                done = true
                victim = nil
                collectgarbage() collectgarbage()
                fresh = Recycled { ticking = true } -- in the slot again: the victim's address
                assert(fresh:ticks() == 0) -- synced: ticking from now on
            end }
        assert(killer:ticks() == 0) -- synced first, so it ticks first
        assert(victim:ticks() == 0)
        root = Column {})");
    m_h.renderFrame("root");
    ASSERT_EQ(Recycled::m_addresses.size(), 3u);                   // victim, killer, fresh
    EXPECT_EQ(Recycled::m_addresses[2], Recycled::m_addresses[0]); // fresh lives at the victim's address
    EXPECT_EQ(m_h.run("assert(fresh:ticks() == 0, 'ticked in the dead victim slot')"), "");
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.run("assert(fresh:ticks() == 1)"), "");
    EXPECT_TRUE(luaui::errors().empty());
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
    // The style is a TextStyle name (nil: bodyLarge); a typo is an error that lists the names.
    run("local a = ui.textSize('abc'); local b = ui.textSize('abc', nil); local c = ui.textSize('abc', 'bodyLarge');"
        "assert(a == b and b == c); assert(ui.textSize('abc', 'displayLarge') > a)");
    EXPECT_NE(m_h.run("ui.textSize('abc', 'huge')").find(R"(textSize: style: TextStyle ('displayLarge'|)"),
              std::string::npos);
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

// A NaN delay used to make the timer's due time NaN: it never fired nor went away, and ui.__tick reported the next
// timer as due now (math.max(0, NaN) is 0), so the app rendered continuously. Negative and infinite delays and
// non-numbers are rejected too; nil and 0 mean "on the next frame".
TEST_F(LuaGuardsTest, SetTimeoutRejectsNegativeAndNonFiniteDelays) {
    for (const char* ms : {", -5", ", math.huge", ", -math.huge", ", 0 / 0", ", '10'", ", {}"}) {
        const std::string error = m_h.run(std::string{"ui.setTimeout(function() end"} + ms + ")");
        EXPECT_NE(error.find("setTimeout: the delay must be a finite number of milliseconds >= 0"), std::string::npos)
            << ms << ": " << error;
    }
    run("assert(ui.__tick(ui.native.time()) == nil)"); // nothing was added
    run(R"(
        fired = 0
        ui.setTimeout(function() fired = fired + 1 end)
        ui.setTimeout(function() fired = fired + 1 end, 0)
        local later = ui.setTimeout(function() end, 60000)
        local now = ui.native.time()
        local wait = ui.__tick(now + 1)
        assert(fired == 2, fired)
        assert(wait > 50 and wait <= 60, wait)
        ui.clearTimer(later)
        assert(ui.__tick(now + 2) == nil))");
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

// A color component a script gives as math.huge (or 0/0) used to become a NaN scheme: generating it from an infinite
// seed computed inf - inf, and the NaN reached ImGuiStyle::Colors, which ImGui casts to int (undefined). Table colors
// are clamped to 0..1, and color.mix clamps `t` to its documented 0..1 (0 * inf was NaN).
TEST_F(LuaGuardsTest, NonFiniteColorsStayValid) {
    run(R"(
        local function hex(v) return type(v) == "string" and v:match("^#%x%x%x%x%x%x$") ~= nil end
        local huge = ui.theme { seed = { math.huge, 0, 0 } }
        local red = ui.schemeFor { seed = { 1, 0, 0 } }
        for role, v in pairs(huge) do
            assert(hex(v), role .. " = " .. tostring(v))
            assert(v == red[role], role)
        end
        for role, v in pairs(ui.schemeFor { seed = { 0 / 0, -math.huge, 1e300 } }) do assert(hex(v), role) end

        assert(ui.color.mix("#000", "#000", math.huge) == "#000000")
        assert(ui.color.mix("#000", "#FFF", math.huge) == "#FFFFFF")
        assert(ui.color.mix("#000", "#FFF", -math.huge) == "#000000")
        assert(ui.color.mix("#000", "#FFF", 0 / 0) == "#000000")
        assert(ui.color.mix({ math.huge, 0, 0 }, { 0, 0, 0 }, 0) == "#FF0000")
        assert(ui.color.resolve({ 0 / 0, 2, -1, math.huge }) == "#00FF00")
        assert(ui.color.alpha("#FFF", 0 / 0) == "#FFFFFF00")
        assert(ui.color.tone({ math.huge, math.huge, math.huge }, 50):match("^#%x%x%x%x%x%x$"))
    )");
    applyPendingTheme();
    for (const ImVec4& c : ImGui::GetStyle().Colors)
        for (const float channel : {c.x, c.y, c.z, c.w})
            ASSERT_TRUE(channel >= 0.0f && channel <= 1.0f) << channel;
    EXPECT_EQ(theme().scheme.primary, generateScheme({1, 0, 0, 1}, theme().dark).primary);
    setTheme(Theme{});
    applyPendingTheme();
}
