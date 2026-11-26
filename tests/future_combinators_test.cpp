// Main-thread timers (imlui.ui.main_timers), the future combinators (imlui.types.future_combinators: delay,
// withTimeout, race, all) over C++ and Lua futures, the prelude's timeout and race, and the guarantees of
// imlui.types.future they rely on: first settlement wins, handlers run in order and are released on settle, no state
// outlives its settlement. Timers run on a simulated clock.
#include <gtest/gtest.h>

import std;

import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.future;
import imlui.types.future_combinators;
import imlui.types.lua_convert;
import imlui.types.type_builder;
import imlui.ui.clock;
import imlui.ui.main_thread;
import imlui.ui.main_timers;
import imlui.test.headless_frame;

using namespace imlui;
using namespace std::chrono_literals;

namespace {

bool hasError(std::string_view part) {
    for (const auto& e : luaui::errors())
        if (e.find(part) != std::string::npos) return true;
    return false;
}

/// A weak reference to a future's shared state: expired once nothing holds it.
template <class T>
std::weak_ptr<future_detail::Core> probe(const Future<T>& f) {
    return future_detail::Access<T>::state(f);
}

/// The futures the Lua tests' methods made or got, for the test to inspect.
std::optional<Future<void>> lastVoid;
std::optional<Future<int>> lastInt;
std::optional<Promise<int>> lastPromise;
std::optional<Future<int>> lastPromised;

/// An element whose methods use the combinators.
class Timing final : public Element {
public:
    static void declare(TypeBuilder<Timing>& type) {
        type.method("wait", &Timing::wait)
            .method("timeoutAny", &Timing::timeoutAny)
            .method("timeoutInt", &Timing::timeoutInt)
            .method("timeoutMaybe", &Timing::timeoutMaybe)
            .method("raceInts", &Timing::raceInts)
            .method("allInts", &Timing::allInts)
            .method("allAny", &Timing::allAny)
            .method("promised", &Timing::promised)
            .nonVisual();
    }
    void render(Frame&) override {}

    Future<void> wait(int ms) { return delay(std::chrono::milliseconds{ms}); }
    /// Generic over Lua futures: any one, its values ignored.
    Future<void> timeoutAny(Future<void> f, int ms) {
        lastVoid = withTimeout(std::move(f), std::chrono::milliseconds{ms});
        return *lastVoid;
    }
    Future<int> timeoutInt(Future<int> f, int ms) {
        lastInt = withTimeout(std::move(f), std::chrono::milliseconds{ms});
        return *lastInt;
    }
    /// A value or none (sleep): nil reads as nullopt.
    Future<std::optional<int>> timeoutMaybe(Future<std::optional<int>> f, int ms) {
        return withTimeout(std::move(f), std::chrono::milliseconds{ms});
    }
    Future<int> raceInts(std::vector<Future<int>> fs) {
        lastInt = race(std::move(fs));
        return *lastInt;
    }
    Future<std::vector<int>> allInts(std::vector<Future<int>> fs) { return all(std::move(fs)); }
    Future<void> allAny(std::vector<Future<void>> fs) { return all(std::move(fs)); }
    /// A promise the test settles.
    Future<int> promised() {
        auto [promise, future] = makePromise<int>();
        lastPromise = promise;
        lastPromised = future;
        return future;
    }
};

/// The app clock on a simulated time, the main timers cleared.
class CombinatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearPosted();
        clearMainTimers();
        luaui::clearErrors();
        setClockSource([this] { return m_now; });
    }
    void TearDown() override {
        lastVoid.reset();
        lastInt.reset();
        lastPromise.reset();
        lastPromised.reset();
        clearMainTimers();
        runPosted();
        clearPosted();
        setClockSource({});
        luaui::clearErrors();
    }
    /// Advances the clock and runs the timers due, like the Engine's loop.
    void advance(std::chrono::milliseconds ms) {
        m_now += std::chrono::duration<double>(ms).count();
        runDueMainTimers();
    }

    double m_now = 100.0;
};

/// A harness with Timing registered and `t` made.
class LuaCombinatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearPosted();
        luaui::clearErrors();
        m_h.emplace(test::HeadlessFrame::Options{.builtins = true});
        m_h->registerType<Timing>("Timing");
        ASSERT_EQ(m_h->run("root = Column {}; t = Timing {}"), "");
    }
    void TearDown() override {
        lastVoid.reset();
        lastInt.reset();
        lastPromise.reset();
        lastPromised.reset();
        m_h.reset();
        runPosted();
        clearPosted();
        luaui::clearErrors();
    }
    void advance(double seconds) { m_h->advanceTime(seconds); }

    std::optional<test::HeadlessFrame> m_h;
};

} // namespace

// ---- Main timers

TEST_F(CombinatorTest, MainTimersRunWhenDueInOrder) {
    std::vector<int> ran;
    addMainTimer(0.2, [&] { ran.push_back(2); });
    addMainTimer(0.1, [&] { ran.push_back(1); });
    addMainTimer(0.1, [&] { ran.push_back(11); }); // same time: the order added
    const MainTimerId removed = addMainTimer(0.05, [&] { ran.push_back(0); });
    EXPECT_EQ(mainTimerCount(), 4u);
    EXPECT_NEAR(secondsUntilMainTimer(), 0.05, 1e-9);
    EXPECT_TRUE(removeMainTimer(removed));
    EXPECT_FALSE(removeMainTimer(removed));
    EXPECT_FALSE(removeMainTimer(0));
    advance(99ms);
    EXPECT_TRUE(ran.empty());
    advance(1ms);
    EXPECT_EQ(ran, (std::vector{1, 11}));
    advance(1000ms); // late: still runs, once
    EXPECT_EQ(ran, (std::vector{1, 11, 2}));
    EXPECT_EQ(mainTimerCount(), 0u);
    EXPECT_EQ(secondsUntilMainTimer(), std::numeric_limits<double>::infinity());
    EXPECT_EQ(addMainTimer(1.0, {}), 0u); // nothing to run
}

TEST_F(CombinatorTest, MainTimersAddedWhileRunningWaitAndThrowsAreReported) {
    int nested = 0;
    addMainTimer(0.0, [&] {
        addMainTimer(0.0, [&] { ++nested; }); // due, but left for the next call
        throw std::runtime_error("tick failed");
    });
    bool after = false;
    addMainTimer(0.0, [&] { after = true; });
    EXPECT_EQ(runDueMainTimers(), 2u);
    EXPECT_TRUE(after); // the throw didn't stop the others
    EXPECT_EQ(nested, 0);
    EXPECT_TRUE(hasError("main timer: tick failed"));
    EXPECT_EQ(runDueMainTimers(), 1u);
    EXPECT_EQ(nested, 1);
    luaui::clearErrors();
}

// ---- delay

TEST_F(CombinatorTest, DelayIsDoneAfterItsTime) {
    const Future<void> d = delay(100ms);
    EXPECT_TRUE(d.pending());
    EXPECT_EQ(mainTimerCount(), 1u);
    advance(99ms);
    EXPECT_TRUE(d.pending());
    advance(1ms);
    EXPECT_EQ(d.status(), FutureStatus::Done);
    EXPECT_EQ(mainTimerCount(), 0u);

    const Future<void> now = delay(-5ms); // <= 0: the next loop iteration
    EXPECT_TRUE(now.pending());
    runDueMainTimers();
    EXPECT_EQ(now.status(), FutureStatus::Done);
}

TEST_F(CombinatorTest, CancellingADelayRemovesItsTimerAndFreesIt) {
    std::weak_ptr<future_detail::Core> state;
    {
        const Future<void> d = delay(100ms);
        state = probe(d);
        EXPECT_TRUE(d.cancel());
        EXPECT_EQ(mainTimerCount(), 0u);
    }
    EXPECT_TRUE(state.expired());
    // Not cancelled, nobody holding it: the timer keeps it until it rings, then it goes.
    state = probe(delay(10ms));
    EXPECT_FALSE(state.expired());
    advance(10ms);
    EXPECT_TRUE(state.expired());
}

TEST_F(CombinatorTest, DroppedTimersCancelTheirDelays) {
    std::weak_ptr<future_detail::Core> result;
    std::weak_ptr<future_detail::Core> input;
    {
        auto [promise, f] = makePromise<int>();
        const Future<int> r = withTimeout(f, 100ms);
        result = probe(r);
        input = probe(f);
        const Future<void> d = delay(100ms);
        clearMainTimers(); // the Engine's shutdown
        EXPECT_EQ(d.status(), FutureStatus::Cancelled);
        EXPECT_EQ(r.status(), FutureStatus::Cancelled); // its timer was cancelled: so is it, and its input
        EXPECT_EQ(f.status(), FutureStatus::Cancelled);
    }
    runPosted();
    EXPECT_TRUE(result.expired());
    EXPECT_TRUE(input.expired());
}

// ---- withTimeout

TEST_F(CombinatorTest, WithTimeoutSettlesLikeItsInput) {
    auto [promise, input] = makePromise<std::string>();
    const Future<std::string> result = withTimeout(input, 500ms);
    EXPECT_TRUE(result.pending());
    promise.resolve("in time");
    runPosted();
    ASSERT_EQ(result.status(), FutureStatus::Done);
    EXPECT_EQ(*result.value(), "in time");
    EXPECT_EQ(mainTimerCount(), 0u); // the timer is gone
    advance(1000ms);
    EXPECT_EQ(result.status(), FutureStatus::Done);

    auto [failing, failingInput] = makePromise<int>();
    const Future<int> failed = withTimeout(failingInput, 500ms);
    failing.reject("broken");
    runPosted();
    EXPECT_EQ(failed.status(), FutureStatus::Failed);
    EXPECT_EQ(failed.error(), "broken");

    auto [cancelling, cancelledInput] = makePromise<int>();
    const Future<int> cancelled = withTimeout(cancelledInput, 500ms);
    EXPECT_TRUE(cancelledInput.cancel());
    EXPECT_EQ(cancelled.status(), FutureStatus::Cancelled); // synchronously
    EXPECT_EQ(mainTimerCount(), 0u);
}

TEST_F(CombinatorTest, WithTimeoutFiresAndCancelsItsInput) {
    auto [promise, input] = makePromise<int>();
    const Future<int> result = withTimeout(input, 250ms);
    advance(249ms);
    EXPECT_TRUE(result.pending());
    advance(1ms);
    EXPECT_EQ(result.status(), FutureStatus::Failed);
    EXPECT_EQ(result.error(), "timed out after 250 ms");
    EXPECT_EQ(input.status(), FutureStatus::Cancelled);
    EXPECT_TRUE(promise.stopToken().stop_requested()); // the work behind it sees it
    promise.resolve(1); // too late: dropped
    runPosted();
    EXPECT_EQ(result.error(), "timed out after 250 ms");
}

TEST_F(CombinatorTest, CancellingAWithTimeoutCancelsItsInputAndTimer) {
    auto [promise, input] = makePromise<int>();
    const Future<int> result = withTimeout(input, 250ms);
    EXPECT_TRUE(result.cancel());
    EXPECT_EQ(input.status(), FutureStatus::Cancelled);
    EXPECT_TRUE(promise.stopToken().stop_requested());
    EXPECT_EQ(mainTimerCount(), 0u);
}

TEST_F(CombinatorTest, WithTimeoutOfASettledOrEmptyFutureSettlesAtOnce) {
    auto [promise, input] = makePromise<int>();
    promise.resolve(3);
    runPosted();
    const Future<int> result = withTimeout(input, 250ms);
    ASSERT_EQ(result.status(), FutureStatus::Done);
    EXPECT_EQ(*result.value(), 3);
    EXPECT_EQ(mainTimerCount(), 0u);
    EXPECT_EQ(withTimeout(Future<int>{}, 250ms).status(), FutureStatus::Cancelled);
    EXPECT_EQ(withTimeout(Future<void>{}, 250ms).status(), FutureStatus::Cancelled);
}

TEST_F(CombinatorTest, WithTimeoutForwardsProgress) {
    auto [promise, input] = makePromise<int>();
    promise.progress(0.25);
    runPosted();
    const Future<int> result = withTimeout(input, 250ms);
    EXPECT_EQ(result.progress(), 0.25); // the progress so far
    std::vector<double> seen;
    result.onProgress([&](double p) { seen.push_back(p); });
    promise.progress(0.5);
    runPosted();
    EXPECT_EQ(seen, (std::vector{0.5}));
    EXPECT_EQ(result.progress(), 0.5);
}

TEST_F(CombinatorTest, WithTimeoutOfVoid) {
    auto [promise, input] = makePromise<void>();
    const Future<void> result = withTimeout(input, 250ms);
    promise.resolve();
    runPosted();
    EXPECT_EQ(result.status(), FutureStatus::Done);
}

// ---- race

TEST_F(CombinatorTest, RaceTakesTheFirstToSettleAndCancelsTheLosers) {
    auto [a, fa] = makePromise<int>();
    auto [b, fb] = makePromise<int>();
    auto [c, fc] = makePromise<int>();
    const Future<int> winner = race(fa, fb, fc);
    b.resolve(2);
    runPosted();
    ASSERT_EQ(winner.status(), FutureStatus::Done);
    EXPECT_EQ(*winner.value(), 2);
    EXPECT_EQ(fa.status(), FutureStatus::Cancelled);
    EXPECT_EQ(fc.status(), FutureStatus::Cancelled);
    EXPECT_TRUE(a.stopToken().stop_requested());
    EXPECT_FALSE(b.stopToken().stop_requested()); // the winner wasn't cancelled

    // A failure wins too.
    auto [d, fd] = makePromise<int>();
    auto [e, fe] = makePromise<int>();
    const Future<int> failed = race(std::vector{fd, fe});
    e.reject("first");
    runPosted();
    EXPECT_EQ(failed.status(), FutureStatus::Failed);
    EXPECT_EQ(failed.error(), "first");
    EXPECT_EQ(fd.status(), FutureStatus::Cancelled);

    // So does a cancellation.
    auto [g, fg] = makePromise<int>();
    auto [h, fh] = makePromise<int>();
    const Future<int> cancelled = race(fg, fh);
    EXPECT_TRUE(fg.cancel());
    EXPECT_EQ(cancelled.status(), FutureStatus::Cancelled);
    EXPECT_EQ(fh.status(), FutureStatus::Cancelled);
}

TEST_F(CombinatorTest, RaceWithASettledInputDecidesAtOnce) {
    auto [a, fa] = makePromise<int>();
    auto [b, fb] = makePromise<int>();
    auto [c, fc] = makePromise<int>();
    b.resolve(2);
    c.resolve(3);
    runPosted();
    const Future<int> winner = race(fa, fb, fc);
    ASSERT_EQ(winner.status(), FutureStatus::Done);
    EXPECT_EQ(*winner.value(), 2); // the first settled one, in order
    EXPECT_EQ(fa.status(), FutureStatus::Cancelled);
    EXPECT_EQ(fc.status(), FutureStatus::Done);

    EXPECT_EQ(race(std::vector<Future<int>>{}).status(), FutureStatus::Cancelled);
    auto [d, fd] = makePromise<int>();
    EXPECT_EQ(race(fd, Future<int>{}).status(), FutureStatus::Cancelled); // an empty future is cancelled
    EXPECT_EQ(fd.status(), FutureStatus::Cancelled);
}

TEST_F(CombinatorTest, CancellingARaceCancelsEveryInput) {
    auto [a, fa] = makePromise<int>();
    const Future<void> timer = delay(100ms);
    const Future<int> r = race(fa, fa); // the same input twice is fine
    const Future<void> rv = race(timer, delay(200ms));
    EXPECT_TRUE(r.cancel());
    EXPECT_TRUE(rv.cancel());
    EXPECT_EQ(fa.status(), FutureStatus::Cancelled);
    EXPECT_EQ(timer.status(), FutureStatus::Cancelled);
    EXPECT_EQ(mainTimerCount(), 0u);
}

TEST_F(CombinatorTest, RaceAgainstADelayIsATimeout) {
    auto [promise, input] = makePromise<void>();
    const Future<void> r = race(input, delay(50ms));
    advance(50ms);
    EXPECT_EQ(r.status(), FutureStatus::Done); // the delay won
    EXPECT_EQ(input.status(), FutureStatus::Cancelled);
}

// ---- all

TEST_F(CombinatorTest, AllCollectsTheValuesInOrder) {
    auto [a, fa] = makePromise<int>();
    auto [b, fb] = makePromise<int>();
    const Future<std::vector<int>> both = all(std::vector{fa, fb});
    b.resolve(2);
    runPosted();
    EXPECT_TRUE(both.pending());
    a.resolve(1);
    runPosted();
    ASSERT_EQ(both.status(), FutureStatus::Done);
    EXPECT_EQ(*both.value(), (std::vector{1, 2}));

    const Future<std::vector<int>> none = all(std::vector<Future<int>>{});
    ASSERT_EQ(none.status(), FutureStatus::Done);
    EXPECT_TRUE(none.value()->empty());

    const Future<void> waits = all(std::vector{delay(10ms), delay(20ms)});
    advance(10ms);
    EXPECT_TRUE(waits.pending());
    advance(10ms);
    EXPECT_EQ(waits.status(), FutureStatus::Done);
    EXPECT_EQ(all(std::vector<Future<void>>{}).status(), FutureStatus::Done);
}

TEST_F(CombinatorTest, AllFailsOnTheFirstFailureAndCancelsTheRest) {
    auto [a, fa] = makePromise<int>();
    auto [b, fb] = makePromise<int>();
    auto [c, fc] = makePromise<int>();
    const Future<std::vector<int>> each = all(std::vector{fa, fb, fc});
    a.resolve(1);
    b.reject("b failed");
    runPosted();
    EXPECT_EQ(each.status(), FutureStatus::Failed);
    EXPECT_EQ(each.error(), "b failed");
    EXPECT_EQ(fa.status(), FutureStatus::Done);
    EXPECT_EQ(fc.status(), FutureStatus::Cancelled);
    EXPECT_TRUE(c.stopToken().stop_requested());

    auto [d, fd] = makePromise<int>();
    auto [e, fe] = makePromise<int>();
    const Future<std::vector<int>> cancelledByInput = all(std::vector{fd, fe});
    EXPECT_TRUE(fe.cancel());
    EXPECT_EQ(cancelledByInput.status(), FutureStatus::Cancelled);
    EXPECT_EQ(fd.status(), FutureStatus::Cancelled);

    auto [f, ff] = makePromise<int>();
    EXPECT_EQ(all(std::vector{ff, Future<int>{}}).status(), FutureStatus::Cancelled);
    EXPECT_EQ(ff.status(), FutureStatus::Cancelled);
}

TEST_F(CombinatorTest, CancellingAllCancelsItsInputs) {
    auto [a, fa] = makePromise<int>();
    const Future<void> d = delay(10ms);
    const Future<std::vector<int>> each = all(std::vector{fa});
    const Future<void> waits = all(std::vector{d});
    EXPECT_TRUE(each.cancel());
    EXPECT_TRUE(waits.cancel());
    EXPECT_EQ(fa.status(), FutureStatus::Cancelled);
    EXPECT_EQ(d.status(), FutureStatus::Cancelled);
    EXPECT_EQ(mainTimerCount(), 0u);
}

TEST_F(CombinatorTest, CombinatorsCompose) {
    auto [a, fa] = makePromise<int>();
    auto [b, fb] = makePromise<int>();
    const Future<std::vector<int>> slow = withTimeout(all(std::vector{fa, withTimeout(fb, 50ms)}), 100ms);
    a.resolve(1);
    runPosted();
    advance(50ms);
    EXPECT_EQ(slow.status(), FutureStatus::Failed); // the inner timeout, passed up
    EXPECT_EQ(slow.error(), "timed out after 50 ms");
    EXPECT_EQ(fb.status(), FutureStatus::Cancelled);
    EXPECT_EQ(mainTimerCount(), 0u);
}

// ---- No leaks: every state goes once its future settled and nothing holds it

TEST_F(CombinatorTest, SharedStatesAreFreedOnceSettled) {
    std::vector<std::weak_ptr<future_detail::Core>> states;
    {
        auto [promise, input] = makePromise<int>();
        const Future<int> result = withTimeout(input, 100ms);
        states = {probe(input), probe(result)};
        promise.resolve(1);
        runPosted();
    }
    {
        auto [promise, input] = makePromise<int>();
        const Future<int> result = withTimeout(input, 100ms);
        states.push_back(probe(input));
        states.push_back(probe(result));
        advance(100ms);
        EXPECT_EQ(result.status(), FutureStatus::Failed);
    }
    {
        auto [a, fa] = makePromise<int>();
        auto [b, fb] = makePromise<int>();
        const Future<int> r = race(fa, fb);
        const Future<std::vector<int>> each = all(std::vector{fa, fb});
        states.insert(states.end(), {probe(fa), probe(fb), probe(r), probe(each)});
        a.resolve(1);
        runPosted();
    }
    {
        auto [a, fa] = makePromise<int>();
        const Future<int> result = withTimeout(race(fa), 100ms);
        states.insert(states.end(), {probe(fa), probe(result)});
        EXPECT_TRUE(result.cancel());
    }
    runPosted(); // the promises hand their state to the main thread
    for (std::size_t i = 0; i < states.size(); ++i)
        EXPECT_TRUE(states[i].expired()) << "state " << i;
    EXPECT_EQ(mainTimerCount(), 0u);
}

// A pending input keeps a result alive (like a promise does), so its handlers run although nobody kept the result.
TEST_F(CombinatorTest, APendingInputKeepsTheResultAlive) {
    auto [promise, input] = makePromise<int>();
    int got = 0;
    withTimeout(input, 100ms).onSettled([&](const Future<int>& r) { got = *r.value(); });
    promise.resolve(5);
    runPosted();
    EXPECT_EQ(got, 5);
}

// ---- The guarantees of imlui.types.future

TEST_F(CombinatorTest, HandlersAreReleasedWhenTheFutureSettles) {
    const auto captured = std::make_shared<int>(0);
    auto [promise, future] = makePromise<int>();
    future.onSettled([captured](const Future<int>&) {});
    future.onProgress([captured](double) {});
    EXPECT_EQ(captured.use_count(), 3);
    promise.resolve(1);
    runPosted();
    EXPECT_EQ(captured.use_count(), 1); // both handlers are gone

    // A cycle through a handler (the future's own promise, a future that leads back): broken by the settlement.
    std::weak_ptr<future_detail::Core> state;
    {
        auto [p, f] = makePromise<int>();
        state = probe(f);
        f.onSettled([p, f](const Future<int>&) {}); // holds its own future and promise
        f.cancel();
    }
    runPosted();
    EXPECT_TRUE(state.expired());
}

TEST_F(CombinatorTest, HandlersRunInOrderAfterTheStatusChanged) {
    auto [promise, future] = makePromise<int>();
    std::vector<std::string> order;
    future.onSettled([&](const Future<int>& f) {
        order.push_back("first");
        EXPECT_EQ(f.status(), FutureStatus::Done);
        EXPECT_FALSE(f.cancel()); // settled: nothing to cancel
        f.onSettled([&](const Future<int>&) { order.push_back("added inside: at once"); });
    });
    future.onSettled([&](const Future<int>&) { order.push_back("second"); });
    promise.resolve(1);
    runPosted();
    EXPECT_EQ(order, (std::vector<std::string>{"first", "added inside: at once", "second"}));
    future.onSettled([&](const Future<int>&) { order.push_back("after: at once"); });
    EXPECT_EQ(order.back(), "after: at once");
}

TEST_F(CombinatorTest, TheFirstSettlementWinsAgainstACancel) {
    {
        auto [promise, future] = makePromise<int>();
        promise.resolve(1); // posted
        EXPECT_TRUE(future.cancel()); // reaches the main thread first
        runPosted();
        EXPECT_EQ(future.status(), FutureStatus::Cancelled);
    }
    {
        auto [promise, future] = makePromise<int>();
        promise.resolve(1);
        runPosted();
        EXPECT_FALSE(future.cancel());
        EXPECT_EQ(*future.value(), 1);
    }
    // Workers resolving and rejecting while the main thread cancels: one outcome, each handler once.
    for (int round = 0; round < 20; ++round) {
        auto [promise, future] = makePromise<int>();
        int calls = 0;
        future.onSettled([&](const Future<int>&) { ++calls; });
        std::vector<std::jthread> workers;
        for (int i = 0; i < 4; ++i)
            workers.emplace_back([promise, i] {
                if (i % 2 == 0)
                    promise.resolve(i);
                else
                    promise.reject("worker");
            });
        if (round % 2 == 0) future.cancel();
        workers.clear(); // joins
        runPosted();
        EXPECT_NE(future.status(), FutureStatus::Pending);
        EXPECT_EQ(calls, 1);
        if (round % 2 == 0) EXPECT_EQ(future.status(), FutureStatus::Cancelled);
    }
}

// ---- Lua futures as inputs

TEST_F(LuaCombinatorTest, DelayFromLuaResumesAnAwait) {
    ASSERT_EQ(m_h->run("task = async(function() await(t:wait(100)); woke = ui.time() end)()"), "");
    advance(0.05);
    EXPECT_EQ(m_h->run("assert(woke == nil)"), "");
    advance(0.05); // runs the timer: no frame needed
    EXPECT_EQ(m_h->run("assert(woke ~= nil and task.state == 'done')"), "");
    ASSERT_EQ(m_h->run("d = t:wait(100); assert(d:cancel())"), "");
    EXPECT_EQ(mainTimerCount(), 0u);
}

TEST_F(LuaCombinatorTest, FutureOfVoidTakesAnyLuaFuture) {
    ASSERT_EQ(m_h->run(R"(
        slept = t:timeoutAny(sleep(10), 100)                  -- no value
        valued = t:timeoutAny(async(function() return 1, 2 end)(), 100) -- values: ignored
        made = future(); fromMade = t:timeoutAny(made, 100)
        made:resolve({ anything = true })
    )"),
              "");
    m_h->renderFrame("root");
    advance(0.01);
    m_h->renderFrame("root"); // the sleep's timer
    EXPECT_EQ(m_h->run(R"(
        assert(slept.state == "done", slept.state)
        assert(valued.state == "done" and valued.value == nil)
        assert(fromMade.state == "done")
    )"),
              "");
    EXPECT_EQ(mainTimerCount(), 0u); // every timeout's timer went with its input
}

TEST_F(LuaCombinatorTest, ATimeoutCancelsTheLuaFuture) {
    ASSERT_EQ(m_h->run(R"(
        slow = future()
        timed = t:timeoutAny(slow, 50)
        timed:catch(function(e) caught = e end)
    )"),
              "");
    advance(0.05);
    EXPECT_EQ(m_h->run(R"(
        assert(timed.state == "failed" and caught == "timed out after 50 ms", tostring(caught))
        assert(slow.state == "cancelled")
    )"),
              "");
    // Cancelling the result from Lua cancels the input.
    ASSERT_EQ(m_h->run("other = future(); r = t:timeoutAny(other, 50); assert(r:cancel()); assert(other.state == 'cancelled')"),
              "");
    EXPECT_EQ(mainTimerCount(), 0u);
}

TEST_F(LuaCombinatorTest, TypedTimeoutsKeepTheValue) {
    ASSERT_EQ(m_h->run(R"(
        f = future(); typed = t:timeoutInt(f, 100); f:resolve(41)
        maybeSlept = t:timeoutMaybe(sleep(1), 100)
        bad = t:timeoutInt(sleep(1), 100); bad:catch(function(e) badError = e end)
    )"),
              "");
    m_h->renderFrame("root");
    advance(0.01);
    m_h->renderFrame("root");
    EXPECT_EQ(m_h->run(R"(
        assert(typed.state == "done" and typed.value == 41)
        assert(maybeSlept.state == "done" and maybeSlept.value == nil)
        assert(bad.state == "failed" and badError:find("bad future value"), tostring(badError))
    )"),
              "");
}

TEST_F(LuaCombinatorTest, RaceAndAllOverLuaFutures) {
    ASSERT_EQ(m_h->run(R"(
        a, b = future(), future()
        first = t:raceInts({ a, b })
        b:resolve(2)
        assert(first.state == "done" and first.value == 2)
        assert(a.state == "cancelled") -- the loser

        c, d = future(), future()
        both = t:allInts({ c, t:promised() })
        c:resolve(1)
    )"),
              "");
    ASSERT_TRUE(lastPromise.has_value());
    lastPromise->resolve(5);
    runPosted();
    EXPECT_EQ(m_h->run("assert(both.state == 'done' and both.value[1] == 1 and both.value[2] == 5)"), "");
    EXPECT_EQ(m_h->run(R"(
        x, y = future(), future()
        anyAll = t:allAny({ x, y, sleep(1) })
        x:reject("x failed")
        assert(anyAll.state == "failed" and anyAll.error == "x failed")
        assert(y.state == "cancelled")
    )"),
              "");
}

// The result isn't marked handled by its own combinator: a rejection nobody takes is reported like any.
TEST_F(LuaCombinatorTest, AnUnhandledTimeoutIsReported) {
    ASSERT_EQ(m_h->run("dropped = t:timeoutAny(future(), 10)"), "");
    advance(0.01);
    m_h->renderFrame("root");
    EXPECT_TRUE(hasError("unhandled future rejection: timed out after 10 ms"));
    luaui::clearErrors();
}

TEST_F(LuaCombinatorTest, TheStateClosingCancelsAResultOverALuaFuture) {
    ASSERT_EQ(m_h->run("pendingLua = future(); t:timeoutAny(pendingLua, 1000)"), "");
    ASSERT_TRUE(lastVoid.has_value());
    m_h.reset(); // the adopted Lua future goes: the C++ input is cancelled at the next loop iteration
    runPosted();
    EXPECT_EQ(lastVoid->status(), FutureStatus::Cancelled);
    EXPECT_EQ(mainTimerCount(), 0u);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaCombinatorTest, AResultPushedToAClosedStateSettlesInCxxOnly) {
    ASSERT_EQ(m_h->run("r = t:raceInts({ t:promised() })"), "");
    ASSERT_TRUE(lastPromise.has_value() && lastInt.has_value());
    m_h.reset();
    lastPromise->resolve(3);
    runPosted();
    ASSERT_EQ(lastInt->status(), FutureStatus::Done);
    EXPECT_EQ(*lastInt->value(), 3);
    EXPECT_TRUE(luaui::errors().empty());
}

// ---- The prelude's timeout and race

TEST_F(LuaCombinatorTest, LuaTimeoutPassesValuesOrFails) {
    ASSERT_EQ(m_h->run(R"(
        f = future()
        g = timeout(f, 100)
        g:onProgress(function(p) seenProgress = p end)
        f:report(0.5)
        assert(g.progress == 0.5 and seenProgress == 0.5)
        f:resolve(1, 2)
        assert(g.state == "done" and g.__values.n == 2 and g.__values[2] == 2)

        slow = future()
        late = timeout(slow, 50)
        late:catch(function(e) lateError = e end)
        shared = future()
        shared:next(function() end) -- another consumer: released, not cancelled
        sharedLate = timeout(shared, 50)
        sharedLate:catch(function() end)
        settled = future(); settled:reject("already")
        atOnce = timeout(settled, 50); atOnce:catch(function() end)
        assert(atOnce.state == "failed" and atOnce.error == "already")
    )"),
              "");
    advance(0.05);
    m_h->renderFrame("root"); // Lua timers run in ui.__tick
    EXPECT_EQ(m_h->run(R"(
        assert(late.state == "failed" and lateError == "timed out after 50 ms", tostring(lateError))
        assert(slow.state == "cancelled")
        assert(sharedLate.state == "failed" and shared.state == "pending")
    )"),
              "");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaCombinatorTest, CancellingALuaTimeoutClearsItsTimerAndReleasesItsInput) {
    ASSERT_EQ(m_h->run(R"(
        f = future()
        g = timeout(f, 50)
        assert(g:cancel() and f.state == "cancelled")
        held = t:promised()
        h = timeout(held, 50)
        assert(h:cancel())
    )"),
              "");
    ASSERT_TRUE(lastPromised.has_value());
    EXPECT_EQ(lastPromised->status(), FutureStatus::Cancelled); // the C++ side too
    advance(0.1);
    m_h->renderFrame("root");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_NE(m_h->run("timeout(5, 10)").find("timeout: a future expected"), std::string::npos);
    EXPECT_NE(m_h->run("timeout(future(), -1)").find("timeout: the delay must be"), std::string::npos);
}

TEST_F(LuaCombinatorTest, LuaRaceTakesTheFirstAndReleasesTheRest) {
    EXPECT_EQ(m_h->run(R"(
        a, b, c = future(), future(), future()
        c:next(function() end) -- another consumer
        r = race({ a, b, c })
        b:resolve("b", "more")
        assert(r.state == "done" and r.value == "b" and r.__values[2] == "more")
        assert(a.state == "cancelled" and c.state == "pending")

        x = future(); x:reject("early")
        e = race({ future(), x }); e:catch(function() end)
        assert(e.state == "failed" and e.error == "early")
        p = future()
        v = race({ p, 7 })
        assert(v.state == "done" and v.value == 7 and p.state == "cancelled")
        assert(race({}).state == "cancelled")

        q1, q2 = future(), future()
        q = race({ q1, q2 })
        assert(q:cancel() and q1.state == "cancelled" and q2.state == "cancelled")
        y = future()
        z = race({ y })
        y:cancel()
        assert(z.state == "cancelled")
    )"),
              "");
    // A C++ input that loses has its work cancelled.
    ASSERT_EQ(m_h->run("w = future(); r2 = race({ t:promised(), w }); w:resolve(1)"), "");
    ASSERT_TRUE(lastPromise.has_value());
    EXPECT_TRUE(lastPromise->stopToken().stop_requested());
    EXPECT_NE(m_h->run("race(1)").find("race: a list of futures expected"), std::string::npos);
    m_h->renderFrame("root");
    EXPECT_TRUE(luaui::errors().empty());
}
