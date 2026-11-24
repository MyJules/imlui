// Tests for imlui.ui.frame_request: when the next on-demand frame is due.
#include <gtest/gtest.h>

import imlui.ui.frame_request;

using namespace imlui;

namespace {

constexpr double inf = __builtin_inf();

class FrameRequestTest : public ::testing::Test {
protected:
    void SetUp() override { clearFrameRequests(); }
    void TearDown() override {
        setFrameWakeHandler({});
        setLoopAwake(true);
        clearFrameRequests();
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
