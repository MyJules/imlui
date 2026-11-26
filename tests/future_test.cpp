// Future<T>, Promise<T> and runAsync(work) (imlui.types.future, imlui.types.script_object), and the Lua side (the
// Async section of src/resources/imlui.lua): async/await, next/catch, cancel, sleep, all, future(), unhandled
// rejections, and what happens when the object or the Lua state dies before a future settles. Progress, Lua futures
// read back into C++ (method arguments), and cancellation through next/catch chains and all. runAsyncThen(work, then):
// the main-thread step, its value, errors, cancels and progress, and the settlement right after it. whenSettled() and
// notifyWhenSettled(): once after settling, never after the owner died or cancelHandles(), a `pending` property backed
// by a member future, combinator results and Lua-adopted futures.
#include <gtest/gtest.h>

import std;

import lua;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.future;
import imlui.types.future_combinators;
import imlui.types.lua_convert;
import imlui.types.progress;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.ui.background;
import imlui.ui.frame_request;
import imlui.ui.main_thread;
import imlui.test.headless_frame;
import imlui.test.post_waiter;

using namespace imlui;

namespace {

struct Report {
    std::string title;
    int pages = 0;
};

} // namespace

template <>
struct imlui::LuaFields<Report> {
    static constexpr std::tuple fields{imlui::field("title", &Report::title), imlui::field("pages", &Report::pages)};
};

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

/// Set by Backend::hold's work when its token stopped.
std::atomic<bool> holdSawStop = false;
/// Released by Backend::hold's work when it starts.
std::counting_semaphore<> holdStarted{0};
/// Backend::ask's promise and future, for the test to settle and inspect.
std::optional<Promise<int>> asked;
std::optional<Future<int>> askedFuture;
/// The future Backend::keep got.
std::optional<Future<Report>> keptReport;
/// Released by Backend::download's work once it reported its progress; the work then waits for `downloadGo`.
std::counting_semaphore<> downloadReported{0};
std::counting_semaphore<> downloadGo{0};

/// An element whose methods return futures.
class Backend final : public Element {
public:
    static void declare(TypeBuilder<Backend>& type) {
        type.method("twice", &Backend::twice)
            .method("report", &Backend::report)
            .method("fail", &Backend::fail)
            .method("nothing", &Backend::nothing)
            .method("hold", &Backend::hold)
            .method("ask", &Backend::ask)
            .method("kept", &Backend::kept)
            .method("keep", &Backend::keep)
            .method("plusOne", &Backend::plusOne)
            .method("same", &Backend::same)
            .method("download", &Backend::download)
            .nonVisual();
    }
    void render(Frame&) override {}

    Future<int> twice(int x) {
        return runAsync([x] { return x * 2; });
    }
    Future<Report> report(std::string title) {
        return runAsync([title] { return Report{title, 3}; });
    }
    Future<int> fail(std::string message) {
        return runAsync([message]() -> int { throw std::runtime_error(message); });
    }
    Future<void> nothing() {
        return runAsync([] {});
    }
    /// Work that runs until its token stops.
    Future<void> hold() {
        return runAsync([](std::stop_token stop) {
            holdStarted.release();
            holdSawStop = waitForStop(stop);
        });
    }
    /// A promise the test settles.
    Future<int> ask() {
        auto [promise, future] = makePromise<int>();
        asked = promise;
        askedFuture = future;
        return future;
    }
    /// The same future every call.
    [[nodiscard]] Future<int> kept() const { return m_kept.second; }
    [[nodiscard]] const Promise<int>& keptPromise() const { return m_kept.first; }
    /// Takes a Lua future: the test inspects it.
    void keep(Future<Report> f) { keptReport = std::move(f); }
    /// A future of f's value + 1, or of its error.
    Future<int> plusOne(Future<int> f) {
        auto [promise, out] = makePromise<int>();
        f.onSettled([promise](const Future<int>& settled) {
            if (settled.value())
                promise.resolve(*settled.value() + 1);
            else
                promise.reject(settled.error());
        });
        return out;
    }
    /// The future it got.
    Future<int> same(Future<int> f) { return f; }
    /// Work that reports progress 100 times, waits for the test, and returns.
    Future<int> download() {
        return runAsync([](std::stop_token, Progress progress) {
            for (int i = 1; i <= 100; ++i)
                progress.report(i / 100.0);
            downloadReported.release();
            downloadGo.acquire();
            return 7;
        });
    }

private:
    std::pair<Promise<int>, Future<int>> m_kept = makePromise<int>();
};

/// Released by Filler::fill's work before it returns (the test lets it).
std::counting_semaphore<> fillGo{0};

/// An element with `pending` properties backed by member futures, notified through notifyWhenSettled().
class Filler final : public Element {
public:
    static void declare(TypeBuilder<Filler>& type) {
        type.property("filling", &Filler::filling)
            .property("watching", &Filler::watching)
            .method("fill", &Filler::fill)
            .method("watch", &Filler::watch)
            .nonVisual();
    }
    void render(Frame&) override {}

    [[nodiscard]] bool filling() const { return m_fill.pending(); }
    [[nodiscard]] bool watching() const { return m_watched.pending(); }
    /// Ten times n, through runAsyncThen; the work waits for `fillGo`.
    Future<int> fill(int n) {
        m_fill.cancel();
        m_fill = runAsyncThen(
            [n] {
                fillGo.acquire();
                return n;
            },
            [this](int rows) {
                m_rows = rows;
                return rows * 10;
            });
        notify("filling");
        notifyWhenSettled(m_fill, "filling");
        return m_fill;
    }
    /// Watches a Lua future.
    void watch(Future<int> f) {
        m_watched = std::move(f);
        notify("watching");
        notifyWhenSettled(m_watched, "watching");
    }

private:
    Future<int> m_fill;
    Future<int> m_watched;
    int m_rows = 0;
};

/// whenSettled() calls made after their Holder died: each would also touch the dead Holder (ASan).
std::atomic<int> callsAfterDeath = 0;

/// A plain object with member futures it reacts to through whenSettled().
class Holder final : public ScriptObject {
public:
    /// With `cancelInDestructor`, the destructor cancels the member futures (after cancelHandles()).
    explicit Holder(bool cancelInDestructor = false) : m_cancelInDestructor{cancelInDestructor} {}
    ~Holder() override {
        m_dying = true;
        if (!m_cancelInDestructor) return;
        cancelHandles();
        m_watched.cancel();
        m_work.cancel();
    }
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    Holder(Holder&&) = delete;
    Holder& operator=(Holder&&) = delete;

    void watch(Future<int> f) {
        m_watched = std::move(f);
        whenSettled(m_watched, [this] { react(); });
    }
    /// A runAsyncThen future whose work waits for `fillGo`.
    void work() {
        m_work = runAsyncThen(
            [] {
                fillGo.acquire();
                return 1;
            },
            [this](int n) {
                react();
                return n;
            });
        whenSettled(m_work, [this] { react(); });
    }
    [[nodiscard]] int calls() const { return m_calls; }

private:
    void react() {
        if (m_dying) ++callsAfterDeath;
        ++m_calls;
    }

    bool m_cancelInDestructor = false;
    bool m_dying = false;
    int m_calls = 0;
    Future<int> m_watched;
    Future<int> m_work;
};

class FutureTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearPosted();
        luaui::clearErrors();
        holdSawStop = false;
        while (holdStarted.try_acquire()) {
        }
        asked.reset();
        askedFuture.reset();
        keptReport.reset();
        while (downloadReported.try_acquire()) {
        }
        while (downloadGo.try_acquire()) {
        }
        while (fillGo.try_acquire()) {
        }
        callsAfterDeath = 0;
    }
    void TearDown() override {
        downloadGo.release(); // a download still waiting returns
        fillGo.release(8);    // so does a fill
        shutdownBackground(std::chrono::seconds{10});
        asked.reset();
        askedFuture.reset();
        keptReport.reset();
        runPosted(); // the promises' destructors hand their state to the main thread
        clearPosted();
        luaui::clearErrors();
    }
};

/// A harness with Backend registered as `Backend` and a root to render.
class LuaFutureTest : public FutureTest {
protected:
    void SetUp() override {
        FutureTest::SetUp();
        m_h.emplace(test::HeadlessFrame::Options{.builtins = true});
        m_h->registerType<Backend>("Backend");
        m_h->registerType<Filler>("Filler");
        ASSERT_EQ(m_h->run("root = Column {}; w = Backend {}"), "");
    }
    void TearDown() override {
        m_h.reset();
        FutureTest::TearDown();
    }
    /// Runs the posted functions until the Lua expression `condition` holds.
    bool runUntil(test::PostWaiter& posts, const std::string& condition) {
        return posts.runUntil([&] { return m_h->run("assert(" + condition + ")") == ""; });
    }

    std::optional<test::HeadlessFrame> m_h;
};

} // namespace

// ---- C++ only

TEST_F(FutureTest, PromiseResolvedOnAWorkerSettlesOnTheMainThread) {
    test::PostWaiter posts;
    auto [promise, future] = makePromise<std::string>();
    EXPECT_TRUE(future.pending());
    EXPECT_EQ(future.value(), nullptr);
    std::thread::id settledOn;
    future.onSettled([&](const Future<std::string>&) { settledOn = std::this_thread::get_id(); });
    std::thread{[promise] {
        promise.resolve("hello");
        promise.resolve("ignored: the first settlement wins");
        promise.reject("ignored too");
    }}.join();
    EXPECT_TRUE(future.pending()); // posted, not settled yet
    ASSERT_TRUE(posts.runUntil([&] { return !future.pending(); }));
    EXPECT_EQ(future.status(), FutureStatus::Done);
    ASSERT_NE(future.value(), nullptr);
    EXPECT_EQ(*future.value(), "hello");
    EXPECT_EQ(settledOn, std::this_thread::get_id());
}

TEST_F(FutureTest, VoidRejectAndOnSettledAfterTheFact) {
    auto [done, doneFuture] = makePromise<void>();
    auto [failed, failedFuture] = makePromise<int>();
    done.resolve();
    failed.reject("no luck");
    runPosted();
    EXPECT_EQ(doneFuture.status(), FutureStatus::Done);
    EXPECT_EQ(failedFuture.status(), FutureStatus::Failed);
    EXPECT_EQ(failedFuture.error(), "no luck");
    bool ran = false;
    failedFuture.onSettled([&](const Future<int>& f) { ran = f.error() == "no luck"; }); // settled: runs now
    EXPECT_TRUE(ran);
}

TEST_F(FutureTest, CancelStopsThePromiseTokenAndDropsTheValue) {
    auto [promise, future] = makePromise<int>();
    const std::stop_token token = promise.stopToken();
    EXPECT_FALSE(token.stop_requested());
    EXPECT_TRUE(future.cancel());
    EXPECT_FALSE(future.cancel());
    EXPECT_TRUE(token.stop_requested());
    EXPECT_EQ(future.status(), FutureStatus::Cancelled);
    EXPECT_EQ(future.error(), "cancelled");
    promise.resolve(1);
    runPosted();
    EXPECT_EQ(future.status(), FutureStatus::Cancelled);
}

TEST_F(FutureTest, PromiseDroppedUnsettledCancels) {
    Future<int> future;
    {
        auto made = makePromise<int>();
        future = made.second;
        std::thread{[p = std::move(made.first)] {}}.join(); // the last copy dies on a worker
    }
    EXPECT_TRUE(future.pending());
    runPosted();
    EXPECT_EQ(future.status(), FutureStatus::Cancelled);
    EXPECT_EQ(Future<int>{}.status(), FutureStatus::Cancelled); // an empty future
}

TEST_F(FutureTest, OwnerStoppingCancelsAndDropsALaterValue) {
    std::stop_source owner;
    auto [promise, future] = makePromise<int>(owner.get_token());
    promise.resolve(1); // posted
    owner.request_stop();
    runPosted();
    EXPECT_EQ(future.status(), FutureStatus::Cancelled);

    std::stop_source owner2;
    auto [promise2, future2] = makePromise<int>(owner2.get_token());
    owner2.request_stop(); // posts a cancel at once
    runPosted();
    EXPECT_EQ(future2.status(), FutureStatus::Cancelled);
    EXPECT_TRUE(promise2.stopToken().stop_requested());
}

TEST_F(FutureTest, RunAsyncReturnsAFutureOfTheResult) {
    test::PostWaiter posts;
    Owner owner;
    Future<int> value = owner.runAsync([] { return 21 * 2; });
    Future<void> nothing = owner.runAsync([](std::stop_token) {});
    Future<int> failed = owner.runAsync([]() -> int { throw std::runtime_error("parse error"); });
    Future<std::unique_ptr<int>> moveOnly = owner.runAsync([] { return std::make_unique<int>(7); });
    ASSERT_TRUE(posts.runUntil(
        [&] { return !value.pending() && !nothing.pending() && !failed.pending() && !moveOnly.pending(); }));
    EXPECT_EQ(*value.value(), 42);
    EXPECT_EQ(nothing.status(), FutureStatus::Done);
    EXPECT_EQ(failed.status(), FutureStatus::Failed);
    EXPECT_EQ(failed.error(), "parse error");
    EXPECT_EQ(**moveOnly.value(), 7);
    EXPECT_TRUE(luaui::errors().empty()); // a rejection isn't reported by C++
}

TEST_F(FutureTest, CancellingARunAsyncFutureStopsTheWork) {
    test::PostWaiter posts;
    Owner owner;
    std::latch started{1};
    std::atomic<bool> sawStop = false;
    Future<void> future = owner.runAsync([&](std::stop_token stop) {
        started.count_down();
        sawStop = waitForStop(stop);
    });
    started.wait();
    EXPECT_TRUE(future.cancel());
    shutdownBackground(std::chrono::seconds{10}); // joins: the work returned
    EXPECT_TRUE(sawStop);
    runPosted();
    EXPECT_EQ(future.status(), FutureStatus::Cancelled);
}

TEST_F(FutureTest, OwnerDeathCancelsARunAsyncFuture) {
    auto owner = std::make_unique<Owner>();
    std::latch started{1};
    std::atomic<bool> sawStop = false;
    Future<int> future = owner->runAsync([&](std::stop_token stop) {
        started.count_down();
        sawStop = waitForStop(stop);
        return 1; // arrives after the owner died: dropped
    });
    started.wait();
    owner.reset();
    shutdownBackground(std::chrono::seconds{10});
    EXPECT_TRUE(sawStop);
    runPosted();
    EXPECT_EQ(future.status(), FutureStatus::Cancelled);

    Owner cancelled;
    cancelled.cancelHandles();
    Future<int> never = cancelled.runAsync([] { return 1; });
    runPosted();
    EXPECT_EQ(never.status(), FutureStatus::Cancelled);
}

// ---- runAsyncThen

TEST_F(FutureTest, RunAsyncThenTransformsTheResultOnTheMainThread) {
    test::PostWaiter posts;
    Owner owner;
    const std::thread::id mainThread = std::this_thread::get_id();
    std::thread::id workedOn;
    std::thread::id appliedOn;
    std::vector<int> kept; // what `then` keeps in C++: the future only gets its size
    Future<std::size_t> size = owner.runAsyncThen(
        [&workedOn] {
            workedOn = std::this_thread::get_id();
            return std::vector<int>(10'000, 7);
        },
        [&](std::vector<int> rows) {
            appliedOn = std::this_thread::get_id();
            kept = std::move(rows);
            return kept.size();
        });
    int applies = 0;
    Future<void> voidWork = owner.runAsyncThen([](std::stop_token) {}, [&applies] { ++applies; });
    Future<void> voidThen = owner.runAsyncThen([] { return 3; }, [&applies](int n) { applies += n; });
    Future<std::unique_ptr<int>> moveOnly = owner.runAsyncThen(
        [p = std::make_unique<int>(4)]() mutable { return std::move(p); },
        [q = std::make_unique<int>(1)](std::unique_ptr<int> p) { return std::make_unique<int>(*p + *q); });
    ASSERT_TRUE(posts.runUntil(
        [&] { return !size.pending() && !voidWork.pending() && !voidThen.pending() && !moveOnly.pending(); }));
    ASSERT_EQ(size.status(), FutureStatus::Done);
    EXPECT_EQ(*size.value(), 10'000u);
    EXPECT_EQ(kept.size(), 10'000u);
    EXPECT_NE(workedOn, mainThread);
    EXPECT_EQ(appliedOn, mainThread);
    EXPECT_EQ(voidWork.status(), FutureStatus::Done);
    EXPECT_EQ(voidThen.status(), FutureStatus::Done);
    EXPECT_EQ(applies, 4);
    EXPECT_EQ(**moveOnly.value(), 5);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FutureTest, RunAsyncThenFailsWithWhatWorkOrThenThrew) {
    test::PostWaiter posts;
    Owner owner;
    bool applied = false;
    Future<int> workFailed = owner.runAsyncThen([]() -> int { throw std::runtime_error("bad input"); },
                                                [&applied](int n) {
                                                    applied = true;
                                                    return n;
                                                });
    Future<int> thenFailed =
        owner.runAsyncThen([] { return 1; }, [](int) -> int { throw std::runtime_error("bad apply"); });
    Future<void> unknown = owner.runAsyncThen([] { return 1; }, [](int) { throw 5; });
    ASSERT_TRUE(posts.runUntil([&] { return !workFailed.pending() && !thenFailed.pending() && !unknown.pending(); }));
    EXPECT_EQ(workFailed.status(), FutureStatus::Failed);
    EXPECT_EQ(workFailed.error(), "bad input");
    EXPECT_FALSE(applied); // `then` isn't called after work threw
    EXPECT_EQ(thenFailed.status(), FutureStatus::Failed);
    EXPECT_EQ(thenFailed.error(), "bad apply");
    EXPECT_EQ(unknown.error(), "unknown C++ exception");
    EXPECT_TRUE(luaui::errors().empty()); // a rejection, not a script error
}

TEST_F(FutureTest, CancellingARunAsyncThenFutureStopsTheWorkAndSkipsThen) {
    Owner owner;
    std::latch started{1};
    std::atomic<bool> sawStop = false;
    bool applied = false;
    Future<int> running = owner.runAsyncThen(
        [&](std::stop_token stop) {
            started.count_down();
            sawStop = waitForStop(stop);
            return 1;
        },
        [&applied](int n) {
            applied = true;
            return n;
        });
    started.wait();
    EXPECT_TRUE(running.cancel());
    shutdownBackground(std::chrono::seconds{10}); // joins: the work returned
    EXPECT_TRUE(sawStop);

    runPosted();
    EXPECT_EQ(running.status(), FutureStatus::Cancelled);

    // The work finished and its main-thread step is queued (its only post): a cancel now still skips `then`.
    test::PostWaiter posts;
    Future<int> finished = owner.runAsyncThen([] { return 2; }, [&applied](int n) {
        applied = true;
        return n;
    });
    ASSERT_TRUE(posts.waitForPosts(1));
    EXPECT_TRUE(finished.cancel());
    ASSERT_TRUE(posts.runUntil([&] { return postedCount() == 0; }));
    EXPECT_FALSE(applied);
    EXPECT_EQ(finished.status(), FutureStatus::Cancelled);
}

TEST_F(FutureTest, OwnerDeathCancelsARunAsyncThenFutureBeforeThen) {
    test::PostWaiter posts;
    auto owner = std::make_unique<Owner>();
    bool applied = false;
    Future<int> future = owner->runAsyncThen([] { return 1; }, [&applied](int n) {
        applied = true;
        return n;
    });
    ASSERT_TRUE(posts.waitForPosts(1)); // the work ran; its main-thread step is queued
    owner.reset();
    ASSERT_TRUE(posts.runUntil([&] { return !future.pending(); }));
    EXPECT_FALSE(applied);
    EXPECT_EQ(future.status(), FutureStatus::Cancelled);
}

TEST_F(FutureTest, RunAsyncThenWorkReportsProgress) {
    test::PostWaiter posts;
    Owner owner;
    std::vector<double> applied;
    std::optional<double> progressAtThen;
    Future<int> future;
    future = owner.runAsyncThen(
        [](std::stop_token, Progress progress) {
            for (int i = 1; i <= 10; ++i)
                progress.report(i / 10.0);
            return 5;
        },
        [&](int n) {
            progressAtThen = future.progress();
            return n * 2;
        });
    future.onProgress([&](double p) { applied.push_back(p); });
    ASSERT_TRUE(posts.runUntil([&] { return !future.pending(); }));
    EXPECT_EQ(*future.value(), 10);
    ASSERT_FALSE(applied.empty());
    EXPECT_EQ(applied.back(), 1.0);
    EXPECT_EQ(progressAtThen, 1.0); // reports made by the work are applied before its main-thread step
}

// The future settles inside the main-thread step that ran `then`: no later post runs first, and a cancel after `then`
// finds it settled. Inside `then` it is still pending.
TEST_F(FutureTest, RunAsyncThenSettlesRightAfterThen) {
    test::PostWaiter posts;
    Owner owner;
    bool pendingInThen = false;
    bool laterPostRan = false;
    std::optional<bool> laterPostRanAtSettle;
    Future<int> future;
    future = owner.runAsyncThen([] { return 2; },
                                [&](int n) {
                                    pendingInThen = future.pending();
                                    post([&laterPostRan] { laterPostRan = true; });
                                    return n + 1;
                                });
    future.onSettled([&](const Future<int>&) { laterPostRanAtSettle = laterPostRan; });
    ASSERT_TRUE(posts.runUntil([&] { return !future.pending(); }));
    EXPECT_TRUE(pendingInThen);
    EXPECT_EQ(laterPostRanAtSettle, false); // settled before anything posted after `then`
    EXPECT_EQ(*future.value(), 3);
    EXPECT_FALSE(future.cancel());

    // A cancel from inside `then` wins: what `then` returns is dropped.
    Future<int> cancelled;
    cancelled = owner.runAsyncThen([] { return 1; },
                                   [&cancelled](int n) {
                                       EXPECT_TRUE(cancelled.cancel());
                                       return n;
                                   });
    ASSERT_TRUE(posts.runUntil([&] { return !cancelled.pending(); }));
    EXPECT_EQ(cancelled.status(), FutureStatus::Cancelled);
    Future<void> voidCancelled;
    voidCancelled = owner.runAsyncThen([] {}, [&voidCancelled] { voidCancelled.cancel(); });
    ASSERT_TRUE(posts.runUntil([&] { return !voidCancelled.pending(); }));
    EXPECT_EQ(voidCancelled.status(), FutureStatus::Cancelled);
}

// ---- whenSettled

TEST_F(FutureTest, WhenSettledRunsOnceAfterTheFutureSettles) {
    Owner owner;
    auto [promise, future] = makePromise<int>();
    int calls = 0;
    std::optional<int> value;
    owner.whenSettled(future, [&](const Future<int>& f) {
        ++calls;
        value = *f.value();
    });
    owner.whenSettled(future, [&calls, p = std::make_unique<int>(1)] { calls += *p; }); // fn(), move-only
    EXPECT_EQ(calls, 0);
    promise.resolve(4);
    promise.resolve(5);
    runPosted();
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(value, 4);
    runPosted();
    EXPECT_EQ(calls, 2);

    // Settled already, or an empty future (cancelled): at once.
    owner.whenSettled(future, [&calls] { ++calls; });
    EXPECT_EQ(calls, 3);
    owner.whenSettled(Future<int>{}, [&calls](const Future<int>& f) {
        if (f.status() == FutureStatus::Cancelled) ++calls;
    });
    EXPECT_EQ(calls, 4);
    // An exception is reported, like onSettled's.
    owner.whenSettled(future, [] { throw std::runtime_error("bad handler"); });
    EXPECT_TRUE(hasError("bad handler"));
}

TEST_F(FutureTest, WhenSettledIsDroppedByCancelHandlesAndDeath) {
    auto [promise, future] = makePromise<int>();
    int calls = 0;
    auto captured = std::make_shared<int>(0);
    const std::weak_ptr<int> watch = captured;
    Owner cancelled;
    cancelled.whenSettled(future, [&calls, held = std::move(captured)] { ++calls; });
    EXPECT_FALSE(watch.expired());
    cancelled.cancelHandles();
    EXPECT_TRUE(watch.expired()); // destroyed with what it captured, before the future settles

    auto dying = std::make_unique<Owner>();
    auto captured2 = std::make_shared<int>(0);
    const std::weak_ptr<int> watch2 = captured2;
    dying->whenSettled(future, [&calls, held = std::move(captured2)] { ++calls; });
    dying.reset();
    EXPECT_TRUE(watch2.expired());

    // After cancelHandles(): nothing, not even for a settled future.
    cancelled.whenSettled(Future<int>{}, [&calls] { ++calls; });
    promise.resolve(1);
    runPosted();
    EXPECT_EQ(future.status(), FutureStatus::Done);
    EXPECT_EQ(calls, 0);

    // `fn` may kill its own object (cancelHandles) while it runs.
    auto [promise2, future2] = makePromise<int>();
    Owner owner;
    owner.whenSettled(future2, [&owner, &calls, keep = std::make_shared<int>(2)] {
        owner.cancelHandles();
        calls += *keep;
    });
    promise2.resolve(1);
    runPosted();
    EXPECT_EQ(calls, 2);
}

// Destroying the owner while its member futures are pending is safe: nothing calls back into it, whether the
// destructor cancels them (after cancelHandles()) or leaves that to the owner's death. ASan checks the "never".
TEST_F(FutureTest, DestroyingTheOwnerWithPendingMemberFuturesIsSafe) {
    test::PostWaiter posts;
    for (const bool cancelInDestructor : {false, true}) {
        SCOPED_TRACE(cancelInDestructor ? "the destructor cancels" : "the death cancels");
        auto [promise, future] = makePromise<int>();
        auto holder = std::make_unique<Holder>(cancelInDestructor);
        holder->watch(future);
        holder->work();
        holder.reset();
        promise.resolve(1);
        fillGo.release(); // the work returns: its `then` must not run
        shutdownBackground(std::chrono::seconds{10});
        runPosted();
        runPosted();
        EXPECT_EQ(future.status(), cancelInDestructor ? FutureStatus::Cancelled : FutureStatus::Done);
        EXPECT_EQ(callsAfterDeath, 0);
    }
    // Alive, both run: `then`, then the handler, in one step.
    Holder holder;
    holder.work();
    fillGo.release();
    ASSERT_TRUE(posts.runUntil([&] { return holder.calls() == 2; }));
}

TEST_F(FutureTest, WhenSettledWorksWithCombinatorResults) {
    Owner owner;
    auto [p1, f1] = makePromise<int>();
    auto [p2, f2] = makePromise<int>();
    int calls = 0;
    std::vector<int> values;
    owner.whenSettled(all(std::vector{f1, f2}), [&](const Future<std::vector<int>>& f) {
        ++calls;
        values = *f.value();
    });
    p1.resolve(1);
    runPosted();
    EXPECT_EQ(calls, 0);
    p2.resolve(2);
    runPosted();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(values, (std::vector{1, 2}));

    // The owner dies before the race is decided: never called.
    auto dying = std::make_unique<Owner>();
    auto [p3, f3] = makePromise<int>();
    auto [p4, f4] = makePromise<int>();
    const Future<int> raced = race(f3, f4);
    dying->whenSettled(raced, [&calls] { ++calls; });
    dying.reset();
    p4.resolve(4);
    runPosted();
    EXPECT_EQ(*raced.value(), 4);
    EXPECT_EQ(f3.status(), FutureStatus::Cancelled);
    EXPECT_EQ(calls, 1);
}

// ---- Lua

// A `pending` property backed by a member future: notify() at the start and notifyWhenSettled() write true, then
// false, back to Lua, and the change handler and an awaiter both read the settled state.
TEST_F(LuaFutureTest, APendingPropertyBackedByAMemberFutureReadsTheSettledState) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        seen = {}
        filler = Filler { onFillingChanged = function(self, v)
            seen[#seen + 1] = tostring(v) .. "/" .. tostring(self.filling)
        end }
        async(function()
            local rows = await(filler:fill(3))
            after = { rows = rows, filling = filler.filling }
        end)()
        assert(filler.filling == true and #seen == 1 and seen[1] == "true/true")
    )"),
              "");
    fillGo.release();
    ASSERT_TRUE(runUntil(posts, "after ~= nil"));
    EXPECT_EQ(m_h->run(R"(
        assert(after.rows == 30 and after.filling == false)
        assert(#seen == 2 and seen[2] == "false/false", table.concat(seen, " "))
    )"),
              "");

    // A fill that replaces a running one: the first is cancelled, the new one is pending.
    ASSERT_EQ(m_h->run("seen = {}; first = filler:fill(1); second = filler:fill(2)"), "");
    EXPECT_EQ(m_h->run(R"(assert(first.state == "cancelled" and filler.filling == true, table.concat(seen, " ")))"),
              "");
    fillGo.release(2);
    ASSERT_TRUE(runUntil(posts, "second.state == 'done'"));
    EXPECT_EQ(m_h->run("assert(filler.filling == false and seen[#seen] == 'false/false')"), "");
    EXPECT_TRUE(luaui::errors().empty());
}

// An adopted Lua future: its settling (synchronous, inside Lua's) notifies the property.
TEST_F(LuaFutureTest, NotifyWhenSettledWorksWithALuaAdoptedFuture) {
    EXPECT_EQ(m_h->run(R"(
        seen = {}
        watcher = Filler { onWatchingChanged = function(self, v) seen[#seen + 1] = v end }
        local g = future()
        watcher:watch(g)
        assert(watcher.watching == true and seen[1] == true)
        g:resolve(1)
        assert(watcher.watching == false and seen[2] == false and #seen == 2)
        local h = future()
        watcher:watch(h)
        h:cancel()
        assert(watcher.watching == false and #seen == 4)
    )"),
              "");
}

// The element is collected while its fill is pending: nothing calls back into it.
TEST_F(LuaFutureTest, AnElementCollectedWithAPendingFillIsSafe) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        local function start() return Filler {}:fill(1) end
        pendingFill = start()
    )"),
              "");
    ASSERT_EQ(m_h->run("collectgarbage(); collectgarbage()"), "");
    fillGo.release();
    ASSERT_TRUE(runUntil(posts, "pendingFill.state == 'cancelled'"));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaFutureTest, AwaitAValueAStructAndNothing) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        task = async(function(x)
            local doubled = await(w:twice(x))
            local report = await(w:report("weekly"))
            nothingCount = select("#", await(w:nothing()))
            return doubled, report
        end)(21)
        assert(task.state == "pending")
    )"),
              "");
    ASSERT_TRUE(runUntil(posts, "task.state ~= 'pending'"));
    EXPECT_EQ(m_h->run(R"(
        assert(task.state == "done", task.state)
        assert(task.value == 42)
        local _, report = table.unpack(task.__values, 1, task.__values.n)
        assert(report.title == "weekly" and report.pages == 3)
        assert(nothingCount == 0)
    )"),
              "");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaFutureTest, ARejectionRaisesInAwaitAndPcallCatchesIt) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        async(function()
            local ok, err = pcall(await, w:fail("disk full"))
            caught = { ok = ok, err = err }
        end)()
    )"),
              "");
    ASSERT_TRUE(runUntil(posts, "caught ~= nil"));
    EXPECT_EQ(m_h->run("assert(caught.ok == false); assert(caught.err == 'disk full', caught.err)"), "");
    m_h->renderFrame("root");
    EXPECT_TRUE(luaui::errors().empty()); // awaited: handled
}

TEST_F(LuaFutureTest, CancellingFromLuaStopsTheWorkAndRaisesCancelled) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        held = w:hold()
        task = async(function()
            local ok, err = pcall(await, held)
            caught = err
        end)()
    )"),
              "");
    holdStarted.acquire(); // the work runs: cancelling must stop it, not skip it
    ASSERT_EQ(m_h->run(R"(
        assert(held:cancel() == true)
        assert(held:cancel() == false)
        assert(held.state == "cancelled" and held.error == "cancelled")
        assert(caught == "cancelled", tostring(caught))
        assert(task.state == "done")
    )"),
              "");
    shutdownBackground(std::chrono::seconds{10}); // joins: the work returned
    EXPECT_TRUE(holdSawStop);
}

TEST_F(LuaFutureTest, CancellingAnAsyncFunctionCancelsWhatItAwaits) {
    ASSERT_EQ(m_h->run(R"(
        held = w:hold()
        task = async(function()
            local _ <close> = setmetatable({}, { __close = function() closed = true end })
            await(held)
            reached = true
        end)()
    )"),
              "");
    holdStarted.acquire();
    ASSERT_EQ(m_h->run(R"(
        assert(task:cancel())
        assert(task.state == "cancelled" and held.state == "cancelled")
        assert(closed == true and reached == nil)
    )"),
              "");
    shutdownBackground(std::chrono::seconds{10});
    EXPECT_TRUE(holdSawStop);
    runPosted();
    EXPECT_EQ(m_h->run("assert(reached == nil)"), "");
}

TEST_F(LuaFutureTest, NextAndCatchChain) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        out = {}
        local f = future()
        f:next(function(v) return v + 1 end)
         :next(function(v) out.a = v; error("bad step") end)
         :next(function() out.skipped = true end)
         :catch(function(e) out.e = e; return "recovered" end)
         :next(function(v) out.r = v end)
        assert(out.a == nil)
        assert(f:resolve(1) == true)
        assert(f:resolve(2) == false)
        assert(out.a == 2 and out.skipped == nil and out.r == "recovered")
        assert(out.e:find("bad step"), out.e)

        -- a handler returning a future: the chain follows it
        local inner = future()
        future():resolve() -- unrelated, harmless
        local g = future()
        local chained = g:next(function() return inner end)
        g:resolve()
        assert(chained.state == "pending")
        inner:resolve("from inner")
        assert(chained.state == "done" and chained.value == "from inner")

        -- a C++ future chains like any other
        w:twice(5):next(function(v) out.cpp = v end)
    )"),
              "");
    ASSERT_TRUE(runUntil(posts, "out.cpp == 10"));
    m_h->renderFrame("root");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaFutureTest, SleepResumesInsideAFrame) {
    ASSERT_EQ(m_h->run(R"(
        task = async(function()
            await(sleep(100))
            slept = ui.time()
            return "awake"
        end)()
    )"),
              "");
    m_h->renderFrame("root");
    EXPECT_EQ(m_h->run("assert(slept == nil)"), "");
    m_h->advanceTime(0.1);
    m_h->renderFrame("root"); // ui.__tick runs the timer: the coroutine resumes inside the frame
    EXPECT_EQ(m_h->run("assert(slept >= 0.1, tostring(slept)); assert(task.value == 'awake')"), "");

    // Cancelling a sleep clears its timer.
    ASSERT_EQ(m_h->run("s = sleep(50); s:next(function() woke = true end); assert(s:cancel())"), "");
    m_h->advanceTime(0.1);
    m_h->renderFrame("root");
    EXPECT_EQ(m_h->run("assert(woke == nil and s.state == 'cancelled')"), "");
    EXPECT_NE(m_h->run("sleep(-1)"), "");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaFutureTest, AllCollectsInOrderAndFailsOnTheFirstFailure) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        all({ w:twice(1), w:twice(2), 7 }):next(function(r) total = { r[1], r[2], r[3], r.n } end)
        all({ w:fail("x"), future() }):catch(function(e) allError = e end)
        all({}):next(function(r) empty = r.n end)
        local a, b = future(), future()
        both = all({ a, b })
        assert(both:cancel() and a.state == "cancelled" and b.state == "cancelled")
    )"),
              "");
    ASSERT_TRUE(runUntil(posts, "total ~= nil and allError ~= nil"));
    EXPECT_EQ(m_h->run(R"(
        assert(total[1] == 2 and total[2] == 4 and total[3] == 7 and total[4] == 3)
        assert(allError == "x")
        assert(empty == 0)
    )"),
              "");
}

TEST_F(LuaFutureTest, UnhandledRejectionsAreReportedAtTheNextFrame) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        async(function() error("boom in async") end)()
        local handled = async(function() error("handled") end)()
        handled:catch(function() end) -- attached after the rejection, before the frame: not reported
        dropped = w:fail("cpp failure")
    )"),
              "");
    ASSERT_TRUE(runUntil(posts, "dropped.state == 'failed'"));
    EXPECT_TRUE(luaui::errors().empty()); // nothing until the next frame
    m_h->renderFrame("root");
    EXPECT_TRUE(hasError("unhandled future rejection: "));
    EXPECT_TRUE(hasError("boom in async"));
    EXPECT_TRUE(hasError("stack traceback"));
    EXPECT_TRUE(hasError("unhandled future rejection: cpp failure"));
    EXPECT_FALSE(hasError("handled\n"));
    EXPECT_EQ(luaui::errors().size(), 2u);
}

TEST_F(LuaFutureTest, MisuseRaisesClearErrors) {
    EXPECT_NE(m_h->run("await(sleep(1))").find("await: not inside an async function"), std::string::npos);
    EXPECT_NE(m_h->run("async(42)").find("async: a function expected"), std::string::npos);
    EXPECT_NE(m_h->run("sleep(1):resolve()").find("only a future made by future()"), std::string::npos);
    EXPECT_NE(m_h->run("future().next(1)").find("next: call it on a future"), std::string::npos);
    EXPECT_EQ(m_h->run(R"(
        assert(async(function() return await(5) end)().value == 5) -- not a future: returned as is
        local y = async(function() coroutine.yield(1) end)()
        assert(y.state == "failed" and y.error:find("coroutine.yield"))
        y:catch(function() end)
        local co = coroutine.wrap(function() return pcall(await, future()) end)
        local ok, err = co()
        assert(not ok and err:find("not inside an async function")) -- a plain coroutine isn't async
    )"),
              "");
}

TEST_F(LuaFutureTest, ResumingFromAPostedFunctionRequestsAFrame) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run("async(function() label = await(w:twice(2)) end)()"), "");
    clearFrameRequests();
    ASSERT_TRUE(runUntil(posts, "label == 4"));
    EXPECT_EQ(secondsUntilFrame(), 0.0);
}

TEST_F(LuaFutureTest, PushingTheSameFutureTwiceGivesTheSameObject) {
    EXPECT_EQ(m_h->run("local a, b = w:kept(), w:kept(); assert(a == b); assert(rawequal(a, b))"), "");
    EXPECT_EQ(m_h->run("assert(w:ask() ~= w:ask())"), "");
}

TEST_F(LuaFutureTest, AFutureSettledBeforeItIsPushedArrivesSettled) {
    // Settle the C++ side first, then push.
    ASSERT_EQ(m_h->run("first = w:ask()"), "");
    asked->resolve(9);
    runPosted();
    ASSERT_EQ(askedFuture->status(), FutureStatus::Done);
    EXPECT_EQ(m_h->run("assert(first.state == 'done' and first.value == 9)"), "");
}

// The element dies while its work runs (collected: nothing references it): the future is cancelled, and the awaiting
// coroutine, which lives on (the state does), sees "cancelled".
TEST_F(LuaFutureTest, ObjectDestroyedBeforeSettleCancels) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        local function start() return w:hold() end -- `w` isn't kept by the coroutine's stack
        async(function()
            local ok, err = pcall(await, start())
            outcome = err
        end)()
    )"),
              "");
    holdStarted.acquire();
    ASSERT_EQ(m_h->run("w = nil; collectgarbage(); collectgarbage()"), "");
    ASSERT_TRUE(runUntil(posts, "outcome ~= nil"));
    EXPECT_EQ(m_h->run("assert(outcome == 'cancelled', tostring(outcome))"), "");
    shutdownBackground(std::chrono::seconds{10});
    EXPECT_TRUE(holdSawStop);
}

// Hot reload or shutdown closes the state while a future is pending: settling it later only reaches C++.
TEST_F(LuaFutureTest, StateClosedBeforeSettleIsANoOp) {
    ASSERT_EQ(m_h->run("async(function() got = await(w:ask()) end)(); pendingTwice = w:twice(1)"), "");
    ASSERT_TRUE(asked.has_value());
    m_h.reset(); // closes the state; the element dies (its runAsync future is cancelled)
    asked->resolve(5);
    runPosted();
    ASSERT_EQ(askedFuture->status(), FutureStatus::Done);
    EXPECT_EQ(*askedFuture->value(), 5);
    EXPECT_TRUE(luaui::errors().empty());
}

// ---- Progress

TEST_F(FutureTest, ProgressIsCoalescedAndTheLatestWins) {
    auto [promise, future] = makePromise<int>();
    EXPECT_EQ(future.progress(), std::nullopt);
    std::vector<double> applied;
    future.onProgress([&](double p) { applied.push_back(p); });
    std::thread{[promise] {
        for (int i = 1; i <= 1000; ++i)
            promise.progress(i / 1000.0);
    }}.join();
    EXPECT_TRUE(applied.empty()); // posted, not applied yet
    EXPECT_EQ(runPosted(), 1u);   // a thousand reports, one apply
    EXPECT_EQ(applied, std::vector<double>{1.0});
    EXPECT_EQ(future.progress(), 1.0);

    promise.progress(0.5);
    promise.progress(std::nan(""));  // dropped
    promise.reporter().report(0.75); // the same, through a reporter
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(applied, (std::vector<double>{1.0, 0.75}));
}

TEST_F(FutureTest, ProgressAfterSettlingIsDropped) {
    auto [promise, future] = makePromise<int>();
    int applies = 0;
    future.onProgress([&](double) { ++applies; });
    promise.progress(0.5); // queued before the settlement: applied before it
    promise.resolve(1);
    promise.progress(0.9); // after resolve(): dropped
    runPosted();
    EXPECT_EQ(future.status(), FutureStatus::Done);
    EXPECT_EQ(future.progress(), 0.5);
    EXPECT_EQ(applies, 1);

    auto [cancelledPromise, cancelled] = makePromise<int>();
    cancelledPromise.progress(0.1); // queued
    cancelled.cancel();
    const Progress reporter = cancelledPromise.reporter();
    reporter.report(0.2);
    runPosted();
    EXPECT_EQ(cancelled.progress(), std::nullopt); // settled before the apply ran

    Progress dangling;
    {
        auto made = makePromise<int>();
        dangling = made.first.reporter();
    }
    dangling.report(0.3); // the promise is gone: nothing
    Progress{}.report(0.4);
    runPosted();
}

TEST_F(FutureTest, RunAsyncWorkReportsProgress) {
    test::PostWaiter posts;
    Owner owner;
    std::vector<double> applied;
    Future<int> future = owner.runAsync([](std::stop_token, Progress progress) {
        for (int i = 1; i <= 10; ++i)
            progress.report(i / 10.0);
        return 5;
    });
    future.onProgress([&](double p) { applied.push_back(p); });
    ASSERT_TRUE(posts.runUntil([&] { return !future.pending(); }));
    EXPECT_EQ(*future.value(), 5);
    ASSERT_FALSE(applied.empty());
    EXPECT_LE(applied.size(), 10u);
    EXPECT_EQ(applied.back(), 1.0); // applied before the settlement (posts run in order)
    EXPECT_EQ(future.progress(), 1.0);
}

TEST_F(LuaFutureTest, ProgressReachesLuaAndOnProgress) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        seen = {}
        f = w:ask():onProgress(function(p) seen[#seen + 1] = p end)
        assert(f.progress == nil)
    )"),
              "");
    asked->progress(0.25);
    asked->progress(0.5);
    clearFrameRequests();
    runPosted();
    EXPECT_EQ(secondsUntilFrame(), 0.0); // bindings reading f.progress update
    EXPECT_EQ(m_h->run("assert(f.progress == 0.5 and #seen == 1 and seen[1] == 0.5)"), "");
    asked->resolve(3);
    asked->progress(0.9);
    runPosted();
    EXPECT_EQ(m_h->run("assert(f.state == 'done' and f.progress == 0.5 and #seen == 1)"), "");

    // runAsync work: a hundred reports before the test lets it return.
    ASSERT_EQ(m_h->run("d = w:download(); d:onProgress(function(p) last = p end)"), "");
    downloadReported.acquire();
    ASSERT_TRUE(runUntil(posts, "d.progress == 1"));
    EXPECT_EQ(m_h->run("assert(last == 1 and d.state == 'pending')"), "");
    downloadGo.release();
    ASSERT_TRUE(runUntil(posts, "d.state == 'done'"));
    EXPECT_EQ(m_h->run("assert(d.value == 7 and d.progress == 1)"), "");
}

TEST_F(LuaFutureTest, AFuturePushedAfterProgressArrivesWithIt) {
    Backend* backend = m_h->instance<Backend>("w");
    ASSERT_NE(backend, nullptr);
    backend->keptPromise().progress(0.4);
    runPosted();
    EXPECT_EQ(m_h->run("assert(w:kept().progress == 0.4, tostring(w:kept().progress))"), "");
}

TEST_F(LuaFutureTest, LuaFuturesReportProgress) {
    EXPECT_EQ(m_h->run(R"(
        local f = future()
        local got = {}
        assert(f:onProgress(function(p) got[#got + 1] = p end) == f)
        assert(f:report(0.2) == true and f.progress == 0.2)
        f:report(0.7)
        assert(#got == 2 and got[2] == 0.7)
        f:resolve("x")
        assert(f:report(0.9) == false and f.progress == 0.7 and #got == 2)
    )"),
              "");
    EXPECT_NE(m_h->run("sleep(1):report(0.5)").find("only a future made by future()"), std::string::npos);
    EXPECT_NE(m_h->run("future():report('half')").find("report: a number expected"), std::string::npos);
    EXPECT_NE(m_h->run("future():onProgress(1)").find("onProgress: a function expected"), std::string::npos);
}

// ---- Lua futures as C++ arguments

TEST_F(LuaFutureTest, ALuaMadeFutureIsReadIntoCxx) {
    ASSERT_EQ(m_h->run("f = future(); w:keep(f)"), "");
    ASSERT_TRUE(keptReport.has_value());
    EXPECT_TRUE(keptReport->pending());
    ASSERT_EQ(m_h->run("f:report(0.3)"), "");
    EXPECT_EQ(keptReport->progress(), 0.3);
    ASSERT_EQ(m_h->run("f:resolve({ title = 'weekly', pages = 4 })"), "");
    ASSERT_EQ(keptReport->status(), FutureStatus::Done); // synchronously: Lua settled it
    EXPECT_EQ(keptReport->value()->title, "weekly");
    EXPECT_EQ(keptReport->value()->pages, 4);

    // Settled already: the C++ future arrives settled.
    ASSERT_EQ(m_h->run("local g = future(); g:reject('nope'); w:keep(g)"), "");
    EXPECT_EQ(keptReport->status(), FutureStatus::Failed);
    EXPECT_EQ(keptReport->error(), "nope");
    m_h->renderFrame("root");
    EXPECT_TRUE(luaui::errors().empty()); // C++ took the rejection: not reported

    EXPECT_NE(m_h->run("w:keep(5)").find("future expected, got number"), std::string::npos);
}

TEST_F(LuaFutureTest, AValueThatDoesNotConvertRejectsWithItsPath) {
    ASSERT_EQ(m_h->run("f = future(); w:keep(f); f:resolve({ title = 'x', pages = 'many' })"), "");
    ASSERT_EQ(keptReport->status(), FutureStatus::Failed);
    EXPECT_EQ(keptReport->error(), "bad future value (pages: integer expected, got string)");
    EXPECT_EQ(m_h->run("assert(f.state == 'done')"), ""); // the Lua future itself is fine
}

TEST_F(LuaFutureTest, AsyncNextAllAndSleepFuturesAreArguments) {
    test::PostWaiter posts;
    ASSERT_EQ(m_h->run(R"(
        fromAsync = w:plusOne(async(function() return await(w:twice(20)) + 1 end)())
        fromNext = w:plusOne(all({ 1 }):next(function(r) return r[1] end))
        fromSleep = w:plusOne(sleep(10):next(function() return 9 end))
        failed = w:plusOne(async(function() error("inner", 0) end)())
        failed:catch(function() end)
    )"),
              "");
    ASSERT_TRUE(runUntil(posts, "fromAsync.state == 'done' and fromNext.state == 'done' and failed.state == 'failed'"));
    m_h->advanceTime(0.02);
    m_h->renderFrame("root"); // the sleep's timer
    ASSERT_TRUE(runUntil(posts, "fromSleep.state == 'done'"));
    EXPECT_EQ(m_h->run(R"(
        assert(fromAsync.value == 42, tostring(fromAsync.value))
        assert(fromNext.value == 2 and fromSleep.value == 10)
        assert(failed.error == "inner", failed.error)
    )"),
              "");
}

TEST_F(LuaFutureTest, AFutureReadBackIsTheSameFuture) {
    // A C++ future pushed and read back: the same state, no extra hop.
    EXPECT_EQ(m_h->run("local k = w:kept(); assert(rawequal(w:same(k), k))"), "");
    ASSERT_EQ(m_h->run("asked0 = w:ask(); back = w:same(asked0)"), "");
    asked->resolve(4);
    runPosted();
    EXPECT_EQ(m_h->run("assert(rawequal(back, asked0) and back.value == 4)"), "");
    // A Lua future read into C++ and pushed back: the same Lua object.
    EXPECT_EQ(m_h->run("local f = future(); assert(rawequal(w:same(f), f)); assert(rawequal(w:same(f), f))"), "");
}

TEST_F(LuaFutureTest, CancellingEitherSideCancelsTheOther) {
    ASSERT_EQ(m_h->run("a = future(); w:keep(a)"), "");
    EXPECT_TRUE(keptReport->cancel());
    EXPECT_EQ(m_h->run("assert(a.state == 'cancelled')"), "");

    ASSERT_EQ(m_h->run("b = future(); w:keep(b); b:cancel()"), "");
    EXPECT_EQ(keptReport->status(), FutureStatus::Cancelled);

    // Cancelled from C++, a next() future releases its source like a Lua cancel: `s` has another consumer, so it lives.
    ASSERT_EQ(m_h->run("s = sleep(10); s:next(function() woke = true end); w:keep(s:next(function() return {} end))"),
              "");
    EXPECT_TRUE(keptReport->cancel());
    m_h->advanceTime(0.02);
    m_h->renderFrame("root");
    EXPECT_EQ(m_h->run("assert(s.state == 'done' and woke == true)"), "");
}

TEST_F(LuaFutureTest, StateClosedBeforeALuaFutureSettlesCancelsTheCxxFuture) {
    ASSERT_EQ(m_h->run("f = future(); w:keep(f)"), "");
    m_h.reset();
    runPosted();
    ASSERT_TRUE(keptReport.has_value());
    EXPECT_EQ(keptReport->status(), FutureStatus::Cancelled);
    EXPECT_TRUE(luaui::errors().empty());
}

// ---- Cancellation through chains

TEST_F(LuaFutureTest, CancellingASourceCancelsWhatNextMadeFromIt) {
    EXPECT_EQ(m_h->run(R"(
        local f = future()
        local g = f:next(function() ran = true end, function() caught = true end)
        local h = f:catch(function() caught = true end)
        local deep = g:next(function() ran = true end):catch(function() caught = true end)
        assert(f:cancel())
        assert(g.state == "cancelled" and h.state == "cancelled" and deep.state == "cancelled")
        assert(ran == nil and caught == nil)

        -- a future a handler returned is followed: its cancel reaches the chain
        local s, inner = future(), future()
        local chained = s:next(function() return inner end)
        s:resolve()
        inner:cancel()
        assert(chained.state == "cancelled")

        -- an async function awaiting a cancelled future: await raises "cancelled"
        local held = future()
        local t = async(function() local ok, err = pcall(await, held); return err end)()
        held:cancel()
        assert(t.value == "cancelled")
    )"),
              "");
    m_h->renderFrame("root");
    EXPECT_TRUE(luaui::errors().empty()); // cancellation is never reported
}

TEST_F(LuaFutureTest, CancellingADerivedFutureReleasesItsSource) {
    EXPECT_EQ(m_h->run(R"(
        -- the only consumer: the source is cancelled
        local f = future()
        local g = f:next(function() end)
        assert(g:cancel() and f.state == "cancelled")

        -- two consumers: cancelled once both let go
        local s = future()
        local a, b = s:next(function() end), s:catch(function() end)
        a:cancel()
        assert(s.state == "pending")
        b:cancel()
        assert(s.state == "cancelled")

        -- an await is a consumer too
        local shared = future()
        local t = async(function() return await(shared) end)()
        shared:next(function() end):cancel()
        assert(shared.state == "pending")
        shared:resolve(1)
        assert(t.value == 1)

        -- the future a handler returned is released too
        local src, inner = future(), future()
        local chained = src:next(function() return inner end)
        src:resolve()
        chained:cancel()
        assert(inner.state == "cancelled")
    )"),
              "");
}

TEST_F(LuaFutureTest, CancellingADerivedFutureStopsTheCxxWork) {
    ASSERT_EQ(m_h->run("held = w:hold(); g = held:next(function() end)"), "");
    holdStarted.acquire();
    ASSERT_EQ(m_h->run("assert(g:cancel() and held.state == 'cancelled')"), "");
    shutdownBackground(std::chrono::seconds{10});
    EXPECT_TRUE(holdSawStop);
}

TEST_F(LuaFutureTest, AllIsCancelledByAnInputAndReleasesItsInputs) {
    EXPECT_EQ(m_h->run(R"(
        local a, b = future(), future()
        local both = all({ a, b })
        a:cancel()
        assert(both.state == "cancelled" and b.state == "pending")

        local kept, alone = future(), future()
        kept:next(function() end) -- another consumer
        local x = all({ kept, alone, 3 })
        assert(x:cancel())
        assert(kept.state == "pending" and alone.state == "cancelled")
    )"),
              "");
}
