// Live<T> (imlui.types.live): sets from any thread coalesce into one apply per main-loop iteration with the latest
// value, which is written back to Lua with on<Name>Changed; it works for elements and singletons, and an owner that
// dies with an apply queued (or before a writer's set) is left alone.
#include <gtest/gtest.h>

import std;

import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.live;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.ui.background;
import imlui.ui.main_thread;
import imlui.test.headless_frame;
import imlui.test.post_waiter;

using namespace imlui;

namespace {

/// An element whose `load` a worker produces.
class Meter final : public Element {
public:
    static void declare(TypeBuilder<Meter>& type) {
        type.property("load", &Meter::load).method("measure", &Meter::measure).nonVisual();
    }
    void render(Frame&) override {}
    [[nodiscard]] double load() const { return m_load.get(); }
    [[nodiscard]] Live<double>::Writer writer() const { return m_load.writer(); }
    void set(double v) { m_load.set(v); }

    /// The pattern with runAsync: the work reports progress through a writer.
    void measure(int steps) {
        runAsync([load = m_load.writer(), steps] {
            for (int i = 1; i <= steps; ++i)
                load.set(i);
        },
                 [] {});
    }

private:
    Live<double> m_load{*this, "load"};
};

/// A singleton with a string status.
class Status final : public ScriptObject {
public:
    static void declare(TypeBuilder<Status>& type) { type.property("text", &Status::text); }
    [[nodiscard]] const std::string& text() const { return m_text.get(); }
    void set(std::string s) { m_text.set(std::move(s)); }

private:
    Live<std::string> m_text{*this, "text", "idle"};
};

class LiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearPosted();
        luaui::clearErrors();
        m_h.emplace(test::HeadlessFrame::Options{.builtins = true});
        m_h->registerType<Meter>("Meter");
        m_h->types().registerSingleton("status", m_status);
    }
    void TearDown() override {
        shutdownBackground(std::chrono::seconds{10});
        m_h.reset();
        clearPosted();
        luaui::clearErrors();
    }
    void run(std::string_view code) { ASSERT_EQ(m_h->run(code), ""); }

    Status m_status; // outlives the harness, like a host's singleton
    std::optional<test::HeadlessFrame> m_h;
};

} // namespace

// Many sets from another thread before the main thread runs its posts: one apply, the last value.
TEST_F(LiveTest, SetsFromAThreadCoalesceIntoOneApplyWithTheLastValue) {
    run("m = Meter { onLoadChanged = function(self, v) changes = (changes or 0) + 1; got = v end }");
    Meter* m = m_h->instance<Meter>("m");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->load(), 0.0);
    std::thread{[m] {
        for (int i = 1; i <= 10'000; ++i)
            m->set(i);
    }}.join();
    EXPECT_EQ(m->load(), 0.0); // not applied before the main thread runs its posts
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(m->load(), 10'000.0);
    run("assert(changes == 1, tostring(changes)); assert(got == 10000); assert(m.load == 10000)");

    // The next set queues the next apply.
    std::thread{[m] { m->set(7); }}.join();
    EXPECT_EQ(runPosted(), 1u);
    run("assert(changes == 2); assert(m.load == 7)");
}

// The main loop runs while a worker sets: at most one apply per set, never more than one queued, and the last value
// wins.
TEST_F(LiveTest, ConcurrentSetsWhileTheMainThreadApplies) {
    test::PostWaiter posts;
    run("m = Meter { onLoadChanged = function(self, v) changes = (changes or 0) + 1 end }");
    Meter* m = m_h->instance<Meter>("m");
    ASSERT_NE(m, nullptr);
    constexpr int sets = 20'000;
    std::atomic<std::size_t> maxQueued = 0;
    std::thread worker{[m, &maxQueued] {
        for (int i = 1; i <= sets; ++i) {
            m->set(i);
            maxQueued = std::max<std::size_t>(maxQueued, postedCount());
        }
    }};
    ASSERT_TRUE(posts.runUntil([&] { return m->load() == sets; }));
    worker.join();
    runPosted();
    EXPECT_EQ(m->load(), static_cast<double>(sets));
    EXPECT_LE(maxQueued.load(), 1u);
    run("assert(changes >= 1 and changes <= 20000, tostring(changes)); assert(m.load == 20000)");
}

TEST_F(LiveTest, WriterInBackgroundWork) {
    test::PostWaiter posts;
    run("m = Meter {}; m:measure(500)");
    ASSERT_TRUE(posts.runUntil([&] { return m_h->instance<Meter>("m")->load() == 500.0; }));
    run("assert(m.load == 500)");
}

TEST_F(LiveTest, SingletonOwner) {
    run(R"(assert(status.text == "idle"); status.onTextChanged = function(v) seen = v end)");
    std::thread{[this] {
        m_status.set("loading");
        m_status.set("ready");
    }}.join();
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(m_status.text(), "ready");
    run(R"(assert(seen == "ready", tostring(seen)); assert(status.text == "ready"))");
}

// The owner dies with an apply queued: the apply is dropped. A writer used afterwards posts nothing.
TEST_F(LiveTest, DeadOwnerIsLeftAlone) {
    run("m = Meter {}");
    Meter* m = m_h->instance<Meter>("m");
    ASSERT_NE(m, nullptr);
    const Live<double>::Writer writer = m->writer();
    std::thread{[writer] { writer.set(1.0); }}.join();
    EXPECT_EQ(postedCount(), 1u);
    run("m = nil; collectgarbage(); collectgarbage()");
    EXPECT_EQ(runPosted(), 1u); // run by the queue, not passed on to the dead owner
    std::thread{[writer] { writer.set(2.0); }}.join();
    EXPECT_EQ(postedCount(), 0u);
    Live<double>::Writer{}.set(3.0); // an empty writer
    EXPECT_EQ(postedCount(), 0u);
}
