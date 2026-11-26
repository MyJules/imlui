// The background runner (imlui.ui.background): work runs on worker threads, a throwing task is reported on the main
// thread, and a shutdown stops the running tasks' tokens, drops the queued ones, joins the workers (or detaches a
// worker whose task ignores its token) and leaves the runner ready to start a new pool.
#include <gtest/gtest.h>

import std;

import imlui.lua.errors;
import imlui.ui.background;
import imlui.ui.main_thread;
import imlui.test.post_waiter;

using namespace imlui;

namespace {

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

class BackgroundTest : public ::testing::Test {
protected:
    void SetUp() override {
        shutdownBackground(std::chrono::seconds{10});
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

TEST_F(BackgroundTest, ThreadLimitIsBetweenTwoAndEight) {
    EXPECT_GE(backgroundThreadLimit(), 2u);
    EXPECT_LE(backgroundThreadLimit(), 8u);
}

TEST_F(BackgroundTest, WorkRunsOnAWorkerThread) {
    std::promise<std::thread::id> ranOn;
    auto future = ranOn.get_future();
    runInBackground([&](std::stop_token stop) {
        EXPECT_FALSE(stop.stop_requested());
        ranOn.set_value(std::this_thread::get_id());
    });
    runInBackground({}); // ignored
    EXPECT_NE(future.get(), std::this_thread::get_id());
}

// Every task runs once, also when there are many more of them than threads.
TEST_F(BackgroundTest, ManyTasksAllRun) {
    constexpr int tasks = 200;
    std::latch finished{tasks};
    std::atomic<int> runs = 0;
    for (int i = 0; i < tasks; ++i)
        runInBackground([&](std::stop_token) {
            ++runs;
            finished.count_down();
        });
    finished.wait();
    EXPECT_EQ(runs, tasks);
}

TEST_F(BackgroundTest, ThrowingTaskIsReportedOnTheMainThread) {
    test::PostWaiter posts;
    runInBackground([](std::stop_token) { throw std::runtime_error("background boom"); });
    runInBackground([](std::stop_token) { throw 42; });
    ASSERT_TRUE(posts.runUntil([] { return luaui::errors().size() >= 2; }));
    EXPECT_TRUE(hasError("background task: background boom"));
    EXPECT_TRUE(hasError("background task: unknown C++ exception"));
}

// All threads busy with tasks that wait for their token, more tasks queued behind them: the shutdown stops the
// running ones' tokens (they return, and the workers are joined) and drops the queued ones without running them.
TEST_F(BackgroundTest, ShutdownStopsRunningTasksAndDropsQueuedOnes) {
    const std::size_t threads = backgroundThreadLimit();
    std::latch started{static_cast<std::ptrdiff_t>(threads)};
    std::atomic<std::size_t> stopped = 0;
    for (std::size_t i = 0; i < threads; ++i)
        runInBackground([&](std::stop_token stop) {
            started.count_down();
            if (waitForStop(stop)) ++stopped;
        });
    started.wait();

    std::atomic<int> queuedRuns = 0;
    auto capture = std::make_shared<int>(0);
    const std::weak_ptr<int> captured = capture;
    for (int i = 0; i < 3; ++i)
        runInBackground([&queuedRuns, capture](std::stop_token) { ++queuedRuns; });
    capture.reset();
    EXPECT_EQ(backgroundTaskCount(), threads + 3);

    shutdownBackground(std::chrono::seconds{10});
    EXPECT_EQ(stopped, threads);
    EXPECT_EQ(queuedRuns, 0);
    EXPECT_TRUE(captured.expired()); // the dropped tasks were destroyed
    EXPECT_EQ(backgroundTaskCount(), 0u);
}

// A task that ignores its token doesn't hold the shutdown past its wait: the worker is detached, finishes the task on
// its own, and the runner starts a new pool for the next work.
TEST_F(BackgroundTest, ShutdownDetachesAWorkerWhoseTaskIgnoresItsToken) {
    auto started = std::make_shared<std::latch>(1);
    auto release = std::make_shared<std::latch>(1);
    auto finished = std::make_shared<std::latch>(1);
    auto sawStop = std::make_shared<std::atomic<bool>>(false);
    runInBackground([=](std::stop_token stop) {
        started->count_down();
        release->wait();
        *sawStop = stop.stop_requested();
        finished->count_down();
    });
    started->wait();
    shutdownBackground(std::chrono::milliseconds{0}); // returns although the task still runs
    EXPECT_EQ(backgroundTaskCount(), 0u);             // the old pool is no longer the runner's

    std::promise<void> ranInNewPool;
    runInBackground([&](std::stop_token stop) {
        EXPECT_FALSE(stop.stop_requested()); // a new pool's token
        ranInNewPool.set_value();
    });
    ranInNewPool.get_future().get();

    release->count_down();
    finished->wait();
    EXPECT_TRUE(*sawStop);
}

TEST_F(BackgroundTest, ShutdownWithoutAPoolDoesNothing) {
    shutdownBackground();
    EXPECT_EQ(backgroundTaskCount(), 0u);
}
