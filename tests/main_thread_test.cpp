// The main-thread queue (imlui.ui.main_thread) and ScriptObject handles: posts from other threads run on the main
// thread and wake the loop, a throwing posted function is reported and the rest still run, many threads post at
// once, and a post through the handle of an element Lua destroyed (collected, or its state closed) is dropped.
#include <gtest/gtest.h>

import std;

import lua;
import imlui.lua.errors;
import imlui.ui.main_thread;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

int workerRuns = 0; ///< posted functions that reached a Worker (main thread only)

/// An element that computes its value on a worker thread, the documented pattern: the thread holds only a handle
/// and its inputs, and hands the result back through handle.post().
class Worker final : public Element {
public:
    static void declare(TypeBuilder<Worker>& type) {
        type.property("value", &Worker::value, &Worker::setValue).method("start", &Worker::start).nonVisual();
    }

    void render(Frame&) override {}

    [[nodiscard]] int value() const { return m_value; }
    void setValue(int v) { m_value = v; }

    void start(int input) {
        std::thread{[handle = handle(), this, input] {
            const int result = input * 2; // the slow work; never touches `this`
            handle.post([this, result] {  // runs on the main thread, only while the Worker is alive
                ++workerRuns;
                m_value = result;
                notify("value");
            });
        }}.detach();
    }

    /// Main thread: a function that touches the element, posted through its handle (a use after free if it ran
    /// after the element died).
    void postTouch(int v) {
        handle().post([this, v] {
            ++workerRuns;
            m_value = v;
            notify("value");
        });
    }

private:
    int m_value = 0;
};

bool hasError(std::string_view part) {
    for (const auto& e : luaui::errors())
        if (e.find(part) != std::string::npos) return true;
    return false;
}

/// Waits (a few seconds at most) until `n` functions are queued.
bool waitForPosted(std::size_t n) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (postedCount() < n) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::yield();
    }
    return true;
}

class MainThreadTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearPosted();
        luaui::clearErrors();
        workerRuns = 0;
    }
    void TearDown() override {
        setPostWakeHandler({});
        clearPosted();
        luaui::clearErrors();
    }
};

class ScriptObjectHandleTest : public MainThreadTest {
protected:
    void SetUp() override {
        MainThreadTest::SetUp();
        m_h.emplace(test::HeadlessFrame::Options{.builtins = true});
        m_h->registerType<Worker>("Worker");
    }
    void TearDown() override {
        m_h.reset();
        MainThreadTest::TearDown();
    }
    void run(std::string_view code) { ASSERT_EQ(m_h->run(code), ""); }

    std::optional<test::HeadlessFrame> m_h;
};

} // namespace

TEST_F(MainThreadTest, PostFromAnotherThreadRunsOnTheMainThreadAndWakesTheLoop) {
    std::atomic<int> wakes = 0;
    setPostWakeHandler([&] { ++wakes; });
    std::thread::id ranOn;
    std::thread{[&] { post([&] { ranOn = std::this_thread::get_id(); }); }}.join();
    EXPECT_EQ(wakes, 1);
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(ranOn, std::this_thread::get_id());
    EXPECT_EQ(runPosted(), 0u);
}

// No loop running (no wake handler): the function waits for the next runPosted().
TEST_F(MainThreadTest, PostWithoutALoopIsKeptForTheNextRun) {
    int ran = 0;
    post([&] { ++ran; });
    post({}); // ignored
    EXPECT_EQ(postedCount(), 1u);
    EXPECT_EQ(ran, 0);
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(ran, 1);
}

TEST_F(MainThreadTest, ClearPostedDropsWithoutRunning) {
    int ran = 0;
    post([&] { ++ran; });
    clearPosted();
    EXPECT_EQ(runPosted(), 0u);
    EXPECT_EQ(ran, 0);
}

TEST_F(MainThreadTest, FunctionsPostedWhileRunningWaitForTheNextRun) {
    std::vector<int> order;
    post([&] {
        order.push_back(1);
        post([&] { order.push_back(3); });
    });
    post([&] { order.push_back(2); });
    EXPECT_EQ(runPosted(), 2u);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST_F(MainThreadTest, ThrowingPostedFunctionIsReportedAndTheRestStillRun) {
    int ran = 0;
    post([] { throw std::runtime_error("posted boom"); });
    post([] { throw 42; });
    post([&] { ++ran; });
    EXPECT_EQ(runPosted(), 3u);
    EXPECT_EQ(ran, 1);
    EXPECT_TRUE(hasError("posted function: posted boom"));
    EXPECT_TRUE(hasError("posted function: unknown C++ exception"));
}

// Several threads post at once while the main thread drains: every function runs once, on the main thread, and
// each thread's functions in the order it posted them.
TEST_F(MainThreadTest, ManyThreadsPostConcurrently) {
    constexpr int threads = 8;
    constexpr int perThread = 500;
    std::atomic<int> wakes = 0;
    setPostWakeHandler([&] { ++wakes; });
    const auto mainId = std::this_thread::get_id();
    std::array<int, threads> last{};
    last.fill(-1);
    int total = 0;
    bool ordered = true;
    bool onMain = true;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            for (int i = 0; i < perThread; ++i)
                post([&, t, i] {
                    const auto slot = static_cast<std::size_t>(t);
                    if (last[slot] != i - 1) ordered = false;
                    last[slot] = i;
                    if (std::this_thread::get_id() != mainId) onMain = false;
                    ++total;
                });
        });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (total < threads * perThread && std::chrono::steady_clock::now() < until)
        runPosted();
    for (auto& w : workers)
        w.join();
    runPosted();
    EXPECT_EQ(total, threads * perThread);
    EXPECT_EQ(wakes, threads * perThread);
    EXPECT_TRUE(ordered);
    EXPECT_TRUE(onMain);
}

TEST_F(MainThreadTest, EmptyHandleIsNeverAlive) {
    const ScriptObject::Handle handle;
    EXPECT_FALSE(handle.alive());
    EXPECT_FALSE(handle.post([] { ADD_FAILURE() << "an empty handle's post ran"; }));
    EXPECT_EQ(postedCount(), 0u);
}

// The element pattern: a worker thread computes, posts through the handle, and the main thread writes the result
// back to Lua (notify: the property and onValueChanged).
TEST_F(ScriptObjectHandleTest, WorkerResultReachesLua) {
    run("w = Worker { onValueChanged = function(self, v) got = v end }; w:start(21)");
    ASSERT_TRUE(waitForPosted(1));
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(workerRuns, 1);
    run("assert(got == 42, tostring(got)); assert(w.value == 42)");
}

// Posted while alive, then Lua collects the element: the function is dropped when the main thread gets to it.
TEST_F(ScriptObjectHandleTest, PostToACollectedElementIsDropped) {
    run("w = Worker {}");
    Worker* w = m_h->instance<Worker>("w");
    ASSERT_NE(w, nullptr);
    const ScriptObject::Handle handle = w->handle();
    EXPECT_TRUE(handle.alive());
    w->postTouch(7);
    run("w = nil; collectgarbage(); collectgarbage()");
    EXPECT_FALSE(handle.alive());
    EXPECT_EQ(runPosted(), 1u); // run by the queue, but not passed on to the dead element
    EXPECT_EQ(workerRuns, 0);
    EXPECT_FALSE(handle.post([] { ADD_FAILURE() << "posted to a dead element"; }));
    EXPECT_EQ(runPosted(), 0u);
}

// A worker thread still running when the element dies: its later post is dropped too.
TEST_F(ScriptObjectHandleTest, WorkerOutlivingItsElementPostsNothing) {
    run("w = Worker {}");
    const ScriptObject::Handle handle = m_h->instance<Worker>("w")->handle();
    std::atomic<bool> go = false;
    std::atomic<bool> posted = true;
    std::thread worker{[&, handle] {
        while (!go)
            std::this_thread::yield();
        posted = handle.post([] { ADD_FAILURE() << "posted to a dead element"; });
    }};
    run("w = nil; collectgarbage(); collectgarbage()");
    go = true;
    worker.join();
    EXPECT_FALSE(posted);
    EXPECT_EQ(runPosted(), 0u);
}

// Closing the Lua state (what a hot reload and shutdown do) destroys the elements and so invalidates their handles.
TEST_F(ScriptObjectHandleTest, PostToAnElementOfAClosedStateIsDropped) {
    run("w = Worker {}; keep = { w }");
    Worker* w = m_h->instance<Worker>("w");
    ASSERT_NE(w, nullptr);
    const ScriptObject::Handle handle = w->handle();
    w->postTouch(3);
    m_h.reset(); // closes the state: every instance is deleted
    EXPECT_FALSE(handle.alive());
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(workerRuns, 0);
}

// A posted function runs outside every Lua call. A Lua error that notify() raises there (here a script's __newindex
// on the element table, when the value is written back) is reported, instead of reaching Lua's panic handler (abort).
TEST_F(ScriptObjectHandleTest, LuaErrorInAPostedNotifyIsReported) {
    run("w = Worker {}; setmetatable(w, { __newindex = function() error('no writes here') end })");
    Worker* w = m_h->instance<Worker>("w");
    ASSERT_NE(w, nullptr);
    w->postTouch(5);
    int after = 0;
    post([&] { ++after; });
    EXPECT_EQ(runPosted(), 2u);
    EXPECT_EQ(workerRuns, 1);
    EXPECT_EQ(after, 1);
    EXPECT_TRUE(hasError("no writes here"));
    EXPECT_EQ(lua::gettop(m_h->state()), 0); // the stack is left as it was
}

namespace {
/// Records what its handle says while it is being destroyed.
class Watched final : public Element {
public:
    static void declare(TypeBuilder<Watched>& type) { type.nonVisual(); }
    Watched() : m_handle{handle()} {}
    ~Watched() override { aliveInDestructor = m_handle.alive(); }
    void render(Frame&) override {}
    void destroyed() override { aliveInDestroyed = m_handle.alive(); }

    static inline bool aliveInDestroyed = true;
    static inline bool aliveInDestructor = true;

private:
    Handle m_handle;
};
} // namespace

// An element's handles are dead before its destroyed() hook and its destructor run, so a worker sees the
// cancellation while the element's members still live.
TEST_F(MainThreadTest, ElementHandlesAreCancelledBeforeDestroyedAndTheDestructor) {
    test::HeadlessFrame h{test::HeadlessFrame::Options{.builtins = true}};
    h.registerType<Watched>("Watched");
    ASSERT_EQ(h.run("w = Watched {}"), "");
    ASSERT_NE(h.instance<Watched>("w"), nullptr);
    ASSERT_EQ(h.run("w = nil; collectgarbage(); collectgarbage()"), "");
    EXPECT_FALSE(Watched::aliveInDestroyed);
    EXPECT_FALSE(Watched::aliveInDestructor);

    // cancelHandles() on a live object: its handles, and those taken afterwards, are dead.
    Watched direct;
    const ScriptObject::Handle before = direct.handle();
    direct.cancelHandles();
    EXPECT_FALSE(before.alive());
    EXPECT_FALSE(direct.handle().alive());
}
