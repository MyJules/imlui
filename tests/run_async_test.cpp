// ScriptObject::runAsync (imlui.types.script_object) and Handle::stopToken: the work runs on the background pool, the
// result reaches `done` on the main thread, a dead object stops the work's token and never gets `done`, exceptions go
// to `fail` or the error list, results and callables may be move-only, an element's `done` writes back to Lua, and the
// Task it returns cancels the job.
#include <gtest/gtest.h>

import std;

import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.script_object;
import imlui.types.task;
import imlui.types.type_builder;
import imlui.ui.background;
import imlui.ui.main_thread;
import imlui.test.headless_frame;
import imlui.test.post_waiter;

using namespace imlui;

namespace {

/// A plain object that runs background work (like a singleton, without Lua).
class Owner final : public ScriptObject {};

bool hasError(std::string_view part) {
    for (const auto& e : luaui::errors())
        if (e.find(part) != std::string::npos) return true;
    return false;
}

/// Blocks a worker until its stop token is stopped; returns whether it was.
bool waitForStop(const std::stop_token& stop) {
    std::mutex mutex;
    std::condition_variable_any cv;
    std::unique_lock lock{mutex};
    cv.wait(lock, stop, [] { return false; });
    return stop.stop_requested();
}

/// Counts down a latch when destroyed: tells a test that what held it is gone.
class DestroyProbe {
public:
    explicit DestroyProbe(std::latch& latch) : m_latch{&latch} {}
    ~DestroyProbe() { m_latch->count_down(); }
    DestroyProbe(const DestroyProbe&) = delete;
    DestroyProbe& operator=(const DestroyProbe&) = delete;
    DestroyProbe(DestroyProbe&&) = delete;
    DestroyProbe& operator=(DestroyProbe&&) = delete;

private:
    std::latch* m_latch;
};

class RunAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearPosted();
        luaui::clearErrors();
    }
    void TearDown() override {
        shutdownBackground(std::chrono::seconds{10});
        clearPosted();
        luaui::clearErrors();
    }
};

} // namespace

TEST_F(RunAsyncTest, WorkRunsInTheBackgroundAndDoneOnTheMainThread) {
    test::PostWaiter posts;
    Owner owner;
    const auto mainId = std::this_thread::get_id();
    std::optional<std::pair<std::thread::id, int>> got;
    std::thread::id doneOn;
    owner.runAsync([] { return std::pair{std::this_thread::get_id(), 42}; },
                   [&](std::pair<std::thread::id, int> result) {
                       got = result;
                       doneOn = std::this_thread::get_id();
                   });
    ASSERT_TRUE(posts.runUntil([&] { return got.has_value(); }));
    EXPECT_NE(got->first, mainId);
    EXPECT_EQ(got->second, 42);
    EXPECT_EQ(doneOn, mainId);
}

TEST_F(RunAsyncTest, VoidWorkAndAStopToken) {
    test::PostWaiter posts;
    Owner owner;
    std::atomic<bool> tokenLive = false;
    bool done = false;
    owner.runAsync([&](std::stop_token stop) { tokenLive = stop.stop_possible() && !stop.stop_requested(); },
                   [&] { done = true; });
    ASSERT_TRUE(posts.runUntil([&] { return done; }));
    EXPECT_TRUE(tokenLive);
}

TEST_F(RunAsyncTest, MoveOnlyResultAndCallables) {
    test::PostWaiter posts;
    Owner owner;
    auto input = std::make_unique<int>(20);
    auto bonus = std::make_unique<int>(1);
    int got = 0;
    owner.runAsync([input = std::move(input)] { return std::make_unique<int>(*input * 2); },
                   [&got, bonus = std::move(bonus)](std::unique_ptr<int> result) { got = *result + *bonus; });
    ASSERT_TRUE(posts.runUntil([&] { return got != 0; }));
    EXPECT_EQ(got, 41);
}

// The object dies while its work runs: the work's token stops, and `done` never runs.
TEST_F(RunAsyncTest, ObjectDestroyedWhileWorkingStopsTheTokenAndSkipsDone) {
    auto owner = std::make_unique<Owner>();
    std::latch started{1};
    std::atomic<bool> sawStop = false;
    bool done = false;
    owner->runAsync(
        [&](std::stop_token stop) {
            started.count_down();
            sawStop = waitForStop(stop);
            return 1;
        },
        [&](int) { done = true; });
    started.wait();
    owner.reset();
    shutdownBackground(std::chrono::seconds{10}); // joins the workers: the work has returned
    EXPECT_TRUE(sawStop);
    runPosted();
    EXPECT_FALSE(done);
}

// The work finished and posted `done`, then the object died: `done` is dropped.
TEST_F(RunAsyncTest, ObjectDestroyedBeforeDoneRunsSkipsDone) {
    test::PostWaiter posts;
    auto owner = std::make_unique<Owner>();
    bool done = false;
    owner->runAsync([] { return 1; }, [&](int) { done = true; });
    ASSERT_TRUE(posts.waitForPosts(1));
    owner.reset();
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_FALSE(done);
}

// The object dies while its work still waits in the queue (every worker busy): the work never runs.
TEST_F(RunAsyncTest, WorkOfAnObjectThatDiedBeforeItStartedIsSkipped) {
    const std::size_t threads = backgroundThreadLimit();
    std::latch busy{static_cast<std::ptrdiff_t>(threads)};
    std::latch release{1};
    for (std::size_t i = 0; i < threads; ++i)
        runInBackground([&](std::stop_token) {
            busy.count_down();
            release.wait();
        });
    busy.wait();

    auto owner = std::make_unique<Owner>();
    std::latch jobGone{1};
    std::atomic<bool> ran = false;
    owner->runAsync([&ran, probe = std::make_shared<DestroyProbe>(jobGone)] { ran = true; }, [] {});
    owner.reset();
    release.count_down();
    jobGone.wait(); // the job was taken from the queue and dropped
    EXPECT_FALSE(ran);
}

TEST_F(RunAsyncTest, ExceptionGoesToFailOnTheMainThread) {
    test::PostWaiter posts;
    Owner owner;
    std::vector<std::string> failures;
    std::thread::id failOn;
    const auto fail = [&](std::string message) {
        failures.push_back(std::move(message));
        failOn = std::this_thread::get_id();
    };
    owner.runAsync([]() -> int { throw std::runtime_error("fetch failed"); }, [](int) { ADD_FAILURE(); }, fail);
    owner.runAsync([] { throw 42; }, [] { ADD_FAILURE(); }, fail);
    ASSERT_TRUE(posts.runUntil([&] { return failures.size() == 2; }));
    std::ranges::sort(failures);
    EXPECT_EQ(failures, (std::vector<std::string>{"fetch failed", "unknown C++ exception"}));
    EXPECT_EQ(failOn, std::this_thread::get_id());
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(RunAsyncTest, ExceptionWithoutFailIsReported) {
    test::PostWaiter posts;
    Owner owner;
    owner.runAsync([] { throw std::runtime_error("parse error"); }, [] { ADD_FAILURE(); });
    ASSERT_TRUE(posts.runUntil([] { return !luaui::errors().empty(); }));
    EXPECT_TRUE(hasError("runAsync ("));
    EXPECT_TRUE(hasError("Owner"));
    EXPECT_TRUE(hasError(": parse error"));
}

// A shutdown stops the token of work whose object still lives: the work returns, and `done` still runs (the object
// lives) when the main thread runs its posts.
TEST_F(RunAsyncTest, ShutdownStopsTheTokenOfALiveObjectsWork) {
    Owner owner;
    std::latch started{1};
    std::optional<bool> got;
    owner.runAsync(
        [&](std::stop_token stop) {
            started.count_down();
            return waitForStop(stop);
        },
        [&](bool stopped) { got = stopped; });
    started.wait();
    shutdownBackground(std::chrono::seconds{10});
    runPosted();
    ASSERT_TRUE(got.has_value());
    EXPECT_TRUE(*got);
}

// After cancelHandles() (what a host destroying a singleton calls first) runAsync does nothing.
TEST_F(RunAsyncTest, CancelledObjectRunsNothing) {
    Owner owner;
    owner.cancelHandles();
    std::atomic<bool> ran = false;
    owner.runAsync([&] { ran = true; }, [] {});
    shutdownBackground(std::chrono::seconds{10});
    EXPECT_FALSE(ran);
    EXPECT_EQ(runPosted(), 0u);
}

TEST_F(RunAsyncTest, HandleStopTokenStopsWhenTheObjectDies) {
    EXPECT_TRUE(ScriptObject::Handle{}.stopToken().stop_requested()); // an empty handle is never alive

    auto owner = std::make_unique<Owner>();
    const ScriptObject::Handle handle = owner->handle();
    const std::stop_token token = handle.stopToken();
    EXPECT_FALSE(token.stop_requested());
    bool callbackRan = false;
    const std::stop_callback callback{token, [&] { callbackRan = true; }};
    owner.reset();
    EXPECT_TRUE(token.stop_requested());
    EXPECT_TRUE(callbackRan); // on the main thread, inside the destructor
    EXPECT_FALSE(handle.alive());

    Owner cancelled;
    const std::stop_token before = cancelled.handle().stopToken();
    cancelled.cancelHandles();
    EXPECT_TRUE(before.stop_requested());
    EXPECT_TRUE(cancelled.handle().stopToken().stop_requested());
}

namespace {
/// An element that computes its value in the background (the documented pattern).
class Doubler final : public Element {
public:
    static void declare(TypeBuilder<Doubler>& type) {
        type.property("value", &Doubler::value).method("start", &Doubler::start).nonVisual();
    }
    void render(Frame&) override {}
    [[nodiscard]] int value() const { return m_value; }
    void start(int input) {
        runAsync([input] { return input * 2; },
                 [this](int result) {
                     m_value = result;
                     notify("value");
                 });
    }

private:
    int m_value = 0;
};
} // namespace

TEST_F(RunAsyncTest, ElementResultReachesLua) {
    test::PostWaiter posts;
    test::HeadlessFrame h{test::HeadlessFrame::Options{.builtins = true}};
    h.registerType<Doubler>("Doubler");
    ASSERT_EQ(h.run("d = Doubler { onValueChanged = function(self, v) got = v end }; d:start(21)"), "");
    ASSERT_TRUE(posts.runUntil([&] { return h.instance<Doubler>("d")->value() != 0; }));
    EXPECT_EQ(h.run("assert(got == 42, tostring(got)); assert(d.value == 42)"), "");
}

// Closing the Lua state (hot reload, shutdown) destroys the element: its running work sees the stop.
TEST_F(RunAsyncTest, ClosingTheStateStopsAnElementsWork) {
    std::atomic<bool> sawStop = false;
    std::latch started{1};
    {
        test::HeadlessFrame h{test::HeadlessFrame::Options{.builtins = true}};
        h.registerType<Doubler>("Doubler");
        ASSERT_EQ(h.run("d = Doubler {}"), "");
        Doubler* d = h.instance<Doubler>("d");
        ASSERT_NE(d, nullptr);
        d->runAsync(
            [&](std::stop_token stop) {
                started.count_down();
                sawStop = waitForStop(stop);
            },
            [] { ADD_FAILURE() << "done ran for a dead element"; });
        started.wait();
    }
    shutdownBackground(std::chrono::seconds{10});
    EXPECT_TRUE(sawStop);
    runPosted();
}

// ---- Task: the handle runAsync(work, done[, fail]) returns

TEST_F(RunAsyncTest, TaskRunsUntilDone) {
    test::PostWaiter posts;
    Owner owner;
    EXPECT_FALSE(Task{}.running());
    EXPECT_FALSE(Task{}.cancel());
    std::optional<bool> runningInDone;
    Task task;
    task = owner.runAsync([] { return 1; }, [&](int) { runningInDone = task.running(); });
    EXPECT_TRUE(task.running());
    ASSERT_TRUE(posts.runUntil([&] { return runningInDone.has_value(); }));
    EXPECT_FALSE(*runningInDone); // ended right before done ran
    EXPECT_FALSE(task.running());
    EXPECT_FALSE(task.cancel()); // after done: nothing to cancel
    EXPECT_FALSE(task.cancelled());
}

// Cancelled while the work waits in the queue (every worker busy): the work never runs, done never runs.
TEST_F(RunAsyncTest, TaskCancelledBeforeTheWorkStarts) {
    const std::size_t threads = backgroundThreadLimit();
    std::latch busy{static_cast<std::ptrdiff_t>(threads)};
    std::latch release{1};
    for (std::size_t i = 0; i < threads; ++i)
        runInBackground([&](std::stop_token) {
            busy.count_down();
            release.wait();
        });
    busy.wait();

    Owner owner;
    std::latch jobGone{1};
    std::atomic<bool> ran = false;
    bool done = false;
    const Task task =
        owner.runAsync([&ran, probe = std::make_shared<DestroyProbe>(jobGone)] { ran = true; }, [&] { done = true; });
    EXPECT_TRUE(task.cancel());
    EXPECT_FALSE(task.cancel());
    EXPECT_TRUE(task.cancelled());
    EXPECT_FALSE(task.running());
    release.count_down();
    jobGone.wait(); // the work was skipped and the job dropped
    shutdownBackground(std::chrono::seconds{10});
    runPosted();
    EXPECT_FALSE(ran);
    EXPECT_FALSE(done);
}

// Cancelled while the work runs: its token stops, and done never runs although the object lives.
TEST_F(RunAsyncTest, TaskCancelledWhileWorkingStopsTheToken) {
    Owner owner;
    std::latch started{1};
    std::atomic<bool> sawStop = false;
    bool done = false;
    const Task task = owner.runAsync(
        [&](std::stop_token stop) {
            started.count_down();
            sawStop = waitForStop(stop);
            return 1;
        },
        [&](int) { done = true; });
    started.wait();
    EXPECT_TRUE(task.running());
    EXPECT_TRUE(task.cancel());
    shutdownBackground(std::chrono::seconds{10}); // joins: the work returned
    EXPECT_TRUE(sawStop);
    runPosted();
    EXPECT_FALSE(done);
}

// The work finished and posted its continuation, then the task was cancelled: neither done nor fail runs, and an
// exception isn't reported either.
TEST_F(RunAsyncTest, TaskCancelledAfterTheWorkSkipsDoneAndFail) {
    test::PostWaiter posts;
    Owner owner;
    bool called = false;
    const Task ok = owner.runAsync([] { return 1; }, [&](int) { called = true; });
    const Task failing = owner.runAsync([]() -> int { throw std::runtime_error("x"); }, [&](int) { called = true; },
                                        [&](std::string) { called = true; });
    const Task reported = owner.runAsync([] { throw std::runtime_error("y"); }, [&] { called = true; });
    ASSERT_TRUE(posts.waitForPosts(3));
    EXPECT_TRUE(ok.cancel());
    EXPECT_TRUE(failing.cancel());
    EXPECT_TRUE(reported.cancel());
    EXPECT_EQ(runPosted(), 3u);
    EXPECT_FALSE(called);
    EXPECT_TRUE(luaui::errors().empty());
}

// A cancel from another thread: either it wins (done never runs) or done already ended the task (cancel is false).
TEST_F(RunAsyncTest, TaskCancelFromAnotherThreadDecidesOnce) {
    test::PostWaiter posts;
    Owner owner;
    for (int i = 0; i < 20; ++i) {
        bool done = false;
        const Task task = owner.runAsync([] { return 1; }, [&](int) { done = true; });
        std::atomic<bool> cancelled = false;
        std::thread canceller{[&] { cancelled = task.cancel(); }};
        ASSERT_TRUE(posts.runUntil([&] { return !task.running(); }));
        canceller.join();
        shutdownBackground(std::chrono::seconds{10});
        runPosted();
        EXPECT_NE(done, cancelled.load());
    }
}

// The object dies: the task ends (not cancelled) once the job is dropped.
TEST_F(RunAsyncTest, TaskOfADeadObjectEnds) {
    auto owner = std::make_unique<Owner>();
    std::latch started{1};
    const Task task = owner->runAsync(
        [&](std::stop_token stop) {
            started.count_down();
            waitForStop(stop);
        },
        [] { ADD_FAILURE(); });
    started.wait();
    owner.reset();
    shutdownBackground(std::chrono::seconds{10});
    runPosted();
    EXPECT_FALSE(task.running());
    EXPECT_FALSE(task.cancelled());
    EXPECT_FALSE(task.cancel());

    Owner cancelled;
    cancelled.cancelHandles();
    EXPECT_FALSE(cancelled.runAsync([] {}, [] {}).running()); // nothing started
}
