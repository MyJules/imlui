// Tests for imlui.ui.frame_request: when the next on-demand frame is due.
#include <gtest/gtest.h>

import std;

import imlui.ui.clock;
import imlui.ui.frame_request;

using namespace imlui;

namespace {

constexpr double inf = __builtin_inf();

class FrameRequestTest : public ::testing::Test {
protected:
    void SetUp() override { clearFrameRequests(); }
    void TearDown() override {
        setMinFrameInterval(0.0);
        setFrameWakeHandler({});
        setLoopAwake(true);
        clearFrameRequests();
        setClockSource({});
    }
};

} // namespace

TEST_F(FrameRequestTest, NothingPendingMeansNoFrame) { EXPECT_EQ(secondsUntilFrame(), inf); }

TEST_F(FrameRequestTest, RequestFrameIsDueNow) {
    requestFrame();
    EXPECT_EQ(secondsUntilFrame(), 0.0);
}

TEST_F(FrameRequestTest, BeginFrameServesTheImmediateRequest) {
    requestFrame();
    beginFrameRequests();
    EXPECT_EQ(secondsUntilFrame(), inf);
}

TEST_F(FrameRequestTest, DelayedRequest) {
    requestFrameAfter(10.0);
    const double s = secondsUntilFrame();
    EXPECT_GT(s, 9.0);
    EXPECT_LE(s, 10.0);
    // A frame rendered earlier (e.g. for input) doesn't drop a future request.
    beginFrameRequests();
    EXPECT_GT(secondsUntilFrame(), 9.0);
}

TEST_F(FrameRequestTest, EarliestRequestWins) {
    requestFrameAfter(20.0);
    requestFrameAfter(5.0);
    requestFrameAfter(30.0);
    EXPECT_LE(secondsUntilFrame(), 5.0);
    EXPECT_GT(secondsUntilFrame(), 4.0);
}

// With vsync off the engine sets a minimum frame interval: a frame asked for right after one began (an animation
// that asks every frame) waits for the rest of the interval, a later one isn't delayed, and nothing pending stays
// nothing.
TEST_F(FrameRequestTest, MinFrameIntervalCapsTheFrameRate) {
    setMinFrameInterval(10.0);
    EXPECT_EQ(secondsUntilFrame(), inf);
    requestFrame();
    EXPECT_EQ(secondsUntilFrame(), 0.0); // no frame began yet
    beginFrameRequests();
    EXPECT_EQ(secondsUntilFrame(), inf);
    requestFrame();
    double s = secondsUntilFrame();
    EXPECT_GT(s, 9.0);
    EXPECT_LE(s, 10.0);
    beginFrameRequests();
    requestFrameAfter(20.0); // due after the interval: unchanged
    s = secondsUntilFrame();
    EXPECT_GT(s, 19.0);
    EXPECT_LE(s, 20.0);

    clearFrameRequests();
    beginFrameRequests();
    requestFramesFor(5.0); // the settle period is capped too
    EXPECT_GT(secondsUntilFrame(), 9.0);

    setMinFrameInterval(0.0); // off again
    requestFrame();
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    for (const double bad : {-1.0, inf, -inf, __builtin_nan("")}) {
        setMinFrameInterval(bad);
        EXPECT_EQ(secondsUntilFrame(), 0.0);
    }
}

// With vsync off the cap is the monitor's refresh rate; a rate the platform doesn't know (0) or one that isn't
// plausible falls back to 240 frames per second.
TEST_F(FrameRequestTest, UnsyncedFrameRateIsTheRefreshRate) {
    EXPECT_EQ(unsyncedFrameRate(60), 60.0);
    EXPECT_EQ(unsyncedFrameRate(144), 144.0);
    EXPECT_EQ(unsyncedFrameRate(360), 360.0);
    for (const int unknown : {0, -1, 1, 23, 1001, std::numeric_limits<int>::max()})
        EXPECT_EQ(unsyncedFrameRate(unknown), fallbackFrameRate) << unknown;
    EXPECT_EQ(fallbackFrameRate, 240.0);
}

TEST_F(FrameRequestTest, NonPositiveDelayIsImmediate) {
    requestFrameAfter(0.0);
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    beginFrameRequests();
    requestFrameAfter(-1.0);
    EXPECT_EQ(secondsUntilFrame(), 0.0);
}

TEST_F(FrameRequestTest, ContinuousFramesDuringSettlePeriod) {
    requestFramesFor(10.0);
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    beginFrameRequests(); // still within the period
    EXPECT_EQ(secondsUntilFrame(), 0.0);
}

TEST_F(FrameRequestTest, ExpiredSettlePeriodFallsBackToPending) {
    requestFramesFor(-1.0);
    requestFrameAfter(10.0);
    EXPECT_GT(secondsUntilFrame(), 9.0);
}

TEST_F(FrameRequestTest, RepeatedRequestsMerge) {
    // A requester asking every frame for the same deadline doesn't grow the queue without bound;
    // after serving, a sooner one still wins.
    for (int i = 0; i < 1000; ++i)
        requestFrameAfter(10.0);
    requestFrameAfter(1.0);
    EXPECT_LE(secondsUntilFrame(), 1.0);
}

// Requests within the merge window share a frame only when it doesn't come before one of them: a frame due a little
// after an earlier request serves it (a few ms late), but an earlier frame doesn't serve a later request (a Timer
// would find itself not due yet on it, ask again, and fire a frame late). On a simulated clock.
TEST_F(FrameRequestTest, OnlyAFrameAtOrJustAfterTheDueTimeCoversARequest) {
    const double start = clockTime(); // the clock never goes back: simulate from where it is
    double now = start;
    setClockSource([&now] { return now; });
    requestFrameAfter(1.0);
    requestFrameAfter(0.998); // the frame at 1.0 is 2 ms after it: covered
    requestFrameAfter(1.003); // 3 ms after the frame at 1.0: not covered by it, a frame of its own
    EXPECT_NEAR(secondsUntilFrame(), 1.0, 1e-9);
    now = start + 1.0;
    beginFrameRequests(); // the frame at 1.0 serves the first two
    EXPECT_NEAR(secondsUntilFrame(), 0.003, 1e-9);
    now = start + 1.003;
    beginFrameRequests();
    EXPECT_EQ(secondsUntilFrame(), inf);
}

TEST_F(FrameRequestTest, WakeHandlerOnlyWhileAsleep) {
    int wakes = 0;
    setFrameWakeHandler([&wakes] { ++wakes; });
    setLoopAwake(true);
    requestFrame();
    EXPECT_EQ(wakes, 0);
    beginFrameRequests();
    setLoopAwake(false);
    requestFrame();
    EXPECT_EQ(wakes, 1);
    requestFrame(); // already pending
    EXPECT_EQ(wakes, 1);
    requestFrameAfter(5.0); // later than a pending one
    EXPECT_EQ(wakes, 1);
}

TEST_F(FrameRequestTest, WakeOnEarlierDelayedRequest) {
    int wakes = 0;
    setFrameWakeHandler([&wakes] { ++wakes; });
    setLoopAwake(false);
    requestFrameAfter(10.0);
    EXPECT_EQ(wakes, 1);
    requestFrameAfter(20.0);
    EXPECT_EQ(wakes, 1);
    requestFrameAfter(2.0);
    EXPECT_EQ(wakes, 2);
}

TEST_F(FrameRequestTest, AFullQueueKeepsTheEarliestRequests) {
    for (int i = 0; i < 64; ++i) // the queue's capacity
        requestFrameAfter(100.0 + i);
    requestFrameAfter(162.5); // lands before the last entry, which is dropped to make room
    requestFrameAfter(170.0); // later than every entry of a full queue: dropped
    requestFrameAfter(50.0);  // earlier than every entry
    EXPECT_LE(secondsUntilFrame(), 50.0);
    EXPECT_GT(secondsUntilFrame(), 49.0);
}

TEST_F(FrameRequestTest, NonFiniteDelaysAreIgnored) {
    requestFrameAfter(__builtin_nan(""));
    requestFrameAfter(inf);
    requestFramesFor(__builtin_nan(""));
    requestFramesFor(inf);
    EXPECT_EQ(secondsUntilFrame(), inf);
    requestFrameAfter(5.0); // the queue still works
    EXPECT_LE(secondsUntilFrame(), 5.0);
    EXPECT_GT(secondsUntilFrame(), 4.0);
    requestFrameAfter(-inf); // as soon as possible, like any delay <= 0
    EXPECT_EQ(secondsUntilFrame(), 0.0);
}
