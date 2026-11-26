// Future<T>, Promise<T> and runAsync(work) (imlui.types.future, imlui.types.script_object), and the Lua side (the
// Async section of src/resources/imlui.lua): async/await, next/catch, cancel, sleep, all, future(), unhandled
// rejections, and what happens when the object or the Lua state dies before a future settles.
#include <gtest/gtest.h>

import std;

import lua;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.future;
import imlui.types.lua_convert;
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

private:
    std::pair<Promise<int>, Future<int>> m_kept = makePromise<int>();
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
    }
    void TearDown() override {
        shutdownBackground(std::chrono::seconds{10});
        asked.reset();
        askedFuture.reset();
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

// ---- Lua

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
