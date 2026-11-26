// Stress tests of the threading code, for the sanitizer builds (ctest --preset clang-asan; ThreadSanitizer isn't
// available on Windows, so these look for the use-after-free and double-free a race would cause): objects dying while
// their runAsync jobs run, report progress and register stop callbacks; Task::cancel racing the job's end from several
// threads; the pool shut down while other threads queue work; promise copies settling and reporting from several
// threads; Live writers racing their owner's death. The outcomes vary from run to run; what is checked holds for
// every interleaving.
#include <gtest/gtest.h>

import std;

import imlui.lua.errors;
import imlui.types.future;
import imlui.types.live;
import imlui.types.progress;
import imlui.types.script_object;
import imlui.types.task;
import imlui.ui.background;
import imlui.ui.main_thread;
import imlui.test.post_waiter;

using namespace imlui;

namespace {

/// An object with background work and a Live value. Its `done` continuations touch it, so one that ran after its
/// death is a use-after-free (which AddressSanitizer reports).
class Owner final : public ScriptObject {
public:
    [[nodiscard]] Live<int>::Writer writer() const { return m_value.writer(); }
    void bump() { ++m_done; }
    [[nodiscard]] int done() const { return m_done; }

private:
    Live<int> m_value{*this, "value"};
    int m_done = 0;
};

/// Work that runs until its token stops (or `limit` rounds), reporting through `live` and `progress`, with a stop
/// callback registered on its token (destroyed while another thread may be running it).
int spin(const std::stop_token& stop, const Live<int>::Writer& live, const Progress& progress, int limit) {
    std::atomic<bool> seen = false;
    const std::stop_callback onStop{stop, [&seen] { seen = true; }};
    int i = 0;
    for (; i < limit && !stop.stop_requested(); ++i) {
        live.set(i);
        progress.report(static_cast<double>(i) / static_cast<double>(limit));
    }
    return i;
}

class AsyncStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        shutdownBackground(std::chrono::seconds{10});
        clearPosted();
        luaui::clearErrors();
    }
    void TearDown() override {
        shutdownBackground(std::chrono::seconds{10});
        clearPosted();
        EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
        luaui::clearErrors();
    }
};

} // namespace

// Owners die, one by one between runs of the queue, while their jobs (runAsync with done, runAsync futures) spin,
// report and hold stop callbacks: the death stops each job's token on the main thread while the worker may be
// registering or destroying its callbacks; no continuation runs after the death; every task and future ends.
TEST_F(AsyncStressTest, OwnersDieWhileTheirJobsRunAndReport) {
    test::PostWaiter posts;
    constexpr int owners = 6;
    constexpr int jobsEach = 4;
    for (int round = 0; round < 4; ++round) {
        std::vector<std::unique_ptr<Owner>> list;
        std::vector<Task> tasks;
        std::vector<Future<int>> futures;
        auto started = std::make_shared<std::atomic<int>>(0);
        for (int o = 0; o < owners; ++o) {
            auto& owner = *list.emplace_back(std::make_unique<Owner>());
            for (int j = 0; j < jobsEach; ++j) {
                const int limit = 1000000 * (j + 1); // until the owner dies, in practice
                tasks.push_back(owner.runAsync(
                    [live = owner.writer(), started, limit](std::stop_token stop) {
                        ++*started;
                        return spin(stop, live, Progress{}, limit);
                    },
                    [o = &owner](int) { o->bump(); }));
                futures.push_back(owner.runAsync([live = owner.writer(), started, limit](std::stop_token stop,
                                                                                        Progress progress) {
                    ++*started;
                    return spin(stop, live, progress, limit);
                }));
            }
        }
        ASSERT_TRUE(posts.runUntil([&] { return *started >= 2; }));
        for (std::size_t i = 0; i < list.size(); ++i) {
            if ((i + static_cast<std::size_t>(round)) % 2 == 0) list[i]->cancelHandles(); // the registry's order
            list[i].reset();
            runPosted();
        }
        shutdownBackground(std::chrono::seconds{10});
        runPosted();
        for (const Task& task : tasks)
            EXPECT_FALSE(task.running());
        for (const Future<int>& future : futures)
            EXPECT_FALSE(future.pending());
    }
}

// Cancels race the jobs' ends: two threads cancel every task (in opposite orders) while the main thread runs the
// continuations. Each task ends exactly once: its `done` ran, or one cancel() won.
TEST_F(AsyncStressTest, CancelsFromSeveralThreadsDecideEachTaskOnce) {
    test::PostWaiter posts;
    Owner owner;
    constexpr std::size_t count = 300;
    std::vector<Task> tasks;
    std::vector<int> done(count, 0);
    auto wins = std::make_shared<std::vector<std::atomic<int>>>(count);
    for (std::size_t i = 0; i < count; ++i)
        tasks.push_back(owner.runAsync(
            [i](std::stop_token stop) {
                // A stop callback on the token a cancel stops, from another thread.
                const std::stop_callback onStop{std::move(stop), [] {}};
                return i;
            },
            [&done](std::size_t n) { ++done[n]; }));
    std::latch go{3};
    const auto cancelAll = [&](bool forward) {
        go.arrive_and_wait();
        for (std::size_t k = 0; k < count; ++k) {
            const std::size_t i = forward ? k : count - 1 - k;
            if (tasks[i].cancel()) ++(*wins)[i];
        }
        post([] {}); // wakes runUntil: a task cancelled before its work started posts nothing
    };
    std::thread first{cancelAll, true};
    std::thread second{cancelAll, false};
    go.arrive_and_wait();
    ASSERT_TRUE(posts.runUntil([&] { return std::ranges::none_of(tasks, &Task::running); }));
    first.join();
    second.join();
    shutdownBackground(std::chrono::seconds{10});
    runPosted();
    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_EQ(done[i] + (*wins)[i], 1) << i;
        EXPECT_EQ(tasks[i].cancelled(), (*wins)[i] == 1) << i;
    }
}

// The pool is shut down again and again while other threads queue work: every task is destroyed exactly once, run or
// dropped, and none runs after the shutdown that dropped it.
TEST_F(AsyncStressTest, ShutdownWhileOtherThreadsQueueWork) {
    struct Counts {
        std::atomic<int> made{0};
        std::atomic<int> ran{0};
        std::atomic<int> destroyed{0};
    };
    /// Counts its destruction: what a task's captures hold.
    class Probe {
    public:
        explicit Probe(std::shared_ptr<Counts> counts) : m_counts{std::move(counts)} { ++m_counts->made; }
        ~Probe() { ++m_counts->destroyed; }
        Probe(const Probe&) = delete;
        Probe& operator=(const Probe&) = delete;
        Probe(Probe&&) = delete;
        Probe& operator=(Probe&&) = delete;
        void ran() const { ++m_counts->ran; }

    private:
        std::shared_ptr<Counts> m_counts;
    };
    auto counts = std::make_shared<Counts>();
    constexpr int producers = 4;
    constexpr int tasksEach = 400;
    std::latch go{producers + 1};
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p)
        threads.emplace_back([&go, counts] {
            go.arrive_and_wait();
            for (int i = 0; i < tasksEach; ++i)
                runInBackground([probe = std::make_shared<const Probe>(counts)](std::stop_token stop) {
                    if (!stop.stop_requested()) probe->ran();
                });
        });
    go.arrive_and_wait();
    for (int i = 0; i < 20; ++i)
        shutdownBackground(std::chrono::seconds{10});
    for (auto& thread : threads)
        thread.join();
    shutdownBackground(std::chrono::seconds{10});
    clearPosted();
    EXPECT_EQ(counts->made.load(), producers * tasksEach);
    EXPECT_EQ(counts->destroyed.load(), counts->made.load());
    EXPECT_LE(counts->ran.load(), counts->made.load());
    EXPECT_EQ(backgroundTaskCount(), 0u);
}

// Copies of one promise on several threads report progress and race to settle it (resolve, reject), then drop their
// copies (the last one may die on a worker): the future settles once, with the winner, and no progress is applied
// after that.
TEST_F(AsyncStressTest, PromiseCopiesSettleAndReportFromSeveralThreads) {
    test::PostWaiter posts;
    for (int round = 0; round < 40; ++round) {
        auto [promise, future] = makePromise<int>();
        int settledCalls = 0;
        bool lateProgress = false;
        future.onSettled([&](const Future<int>&) { ++settledCalls; });
        future.onProgress([&](double) { lateProgress = lateProgress || settledCalls > 0; });
        std::latch go{4};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&go, copy = promise, t, round] {
                go.arrive_and_wait();
                const Progress reporter = copy.reporter();
                for (int i = 0; i < 300; ++i) {
                    reporter.report(i / 300.0);
                    copy.progress(i / 300.0);
                    if (i == 100 + 50 * t) {
                        if ((t + round) % 2 == 0)
                            copy.resolve(t);
                        else
                            copy.reject("thread " + std::to_string(t));
                    }
                }
            });
        promise = {}; // the main thread's copy goes first
        ASSERT_TRUE(posts.runUntil([&] { return !future.pending(); }));
        for (auto& thread : threads)
            thread.join();
        runPosted();
        runPosted();
        EXPECT_EQ(settledCalls, 1);
        EXPECT_FALSE(lateProgress);
        EXPECT_NE(future.status(), FutureStatus::Cancelled);
        if (future.status() == FutureStatus::Done) {
            ASSERT_NE(future.value(), nullptr);
            EXPECT_GE(*future.value(), 0);
            EXPECT_LT(*future.value(), 4);
        } else {
            EXPECT_EQ(future.error().rfind("thread ", 0), 0u) << future.error();
        }
    }
}

// Main-thread cancels race runAsync futures that settle and report on workers: a cancel that came first wins (the
// result is dropped), one that came after the settlement does nothing.
TEST_F(AsyncStressTest, FutureCancelsRaceTheirWork) {
    test::PostWaiter posts;
    Owner owner;
    std::vector<Future<int>> futures;
    for (int i = 0; i < 200; ++i)
        futures.push_back(owner.runAsync([i, live = owner.writer()](std::stop_token stop, Progress progress) {
            return spin(stop, live, progress, 50 * (i % 7 + 1));
        }));
    std::vector<bool> cancelled(futures.size(), false);
    for (std::size_t i = 0; i < futures.size(); i += 2) {
        cancelled[i] = futures[i].cancel();
        if (i % 10 == 0) runPosted();
    }
    ASSERT_TRUE(posts.runUntil([&] { return std::ranges::none_of(futures, &Future<int>::pending); }));
    for (std::size_t i = 0; i < futures.size(); ++i)
        EXPECT_EQ(futures[i].status(), cancelled[i] ? FutureStatus::Cancelled : FutureStatus::Done) << i;
}

// Writers on several threads set a Live while its owner dies: the applies queued before the death are dropped with it.
TEST_F(AsyncStressTest, LiveWritersRaceTheirOwnersDeath) {
    for (int round = 0; round < 20; ++round) {
        auto owner = std::make_unique<Owner>();
        std::atomic<bool> stop = false;
        std::latch go{5};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&go, &stop, live = owner->writer()] {
                go.arrive_and_wait();
                for (int i = 0; !stop; ++i)
                    live.set(i);
            });
        go.arrive_and_wait();
        for (int i = 0; i < 3; ++i)
            runPosted();
        owner.reset();
        for (int i = 0; i < 3; ++i)
            runPosted();
        stop = true;
        for (auto& thread : threads)
            thread.join();
        runPosted();
    }
}
