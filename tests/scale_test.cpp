// Tests for imlui.theme.dpi: content scale x zoom, dp(), zoom clamping and zoom steps.
#include <gtest/gtest.h>

import imlui.theme.dpi;

using namespace imlui;

namespace {

class ScaleTest : public ::testing::Test {
protected:
    void TearDown() override {
        setContentScale(1.0f);
        setZoom(1.0f);
    }
};

} // namespace

TEST_F(ScaleTest, DefaultsToOne) {
    EXPECT_FLOAT_EQ(contentScale(), 1.0f);
    EXPECT_FLOAT_EQ(zoom(), 1.0f);
    EXPECT_FLOAT_EQ(dpiScale(), 1.0f);
    EXPECT_FLOAT_EQ(dp(24.0f), 24.0f);
}

TEST_F(ScaleTest, EffectiveScaleIsContentScaleTimesZoom) {
    setContentScale(1.5f);
    setZoom(2.0f);
    EXPECT_FLOAT_EQ(dpiScale(), 3.0f);
    EXPECT_FLOAT_EQ(dp(10.0f), 30.0f);
    EXPECT_FLOAT_EQ(dp(-56.0f), -168.0f);
}

TEST_F(ScaleTest, InvalidContentScaleFallsBackToOne) {
    setContentScale(0.0f);
    EXPECT_FLOAT_EQ(contentScale(), 1.0f);
    setContentScale(-2.0f);
    EXPECT_FLOAT_EQ(contentScale(), 1.0f);
}

TEST_F(ScaleTest, ZoomIsClamped) {
    setZoom(0.1f);
    EXPECT_FLOAT_EQ(zoom(), minZoom);
    setZoom(10.0f);
    EXPECT_FLOAT_EQ(zoom(), maxZoom);
    setZoom(1.25f);
    EXPECT_FLOAT_EQ(zoom(), 1.25f);
    setZoom(__builtin_nanf(""));
    EXPECT_FLOAT_EQ(zoom(), 1.0f);
}

TEST_F(ScaleTest, ZoomStepFromPresets) {
    EXPECT_FLOAT_EQ(zoomStep(1.0f, 1), 1.1f);
    EXPECT_FLOAT_EQ(zoomStep(1.0f, -1), 0.9f);
    EXPECT_FLOAT_EQ(zoomStep(1.0f, 3), 1.5f);
    EXPECT_FLOAT_EQ(zoomStep(1.0f, 0), 1.0f);
}

TEST_F(ScaleTest, ZoomStepBetweenPresets) {
    // 1.3 lies between 1.25 and 1.5.
    EXPECT_FLOAT_EQ(zoomStep(1.3f, 1), 1.5f);
    EXPECT_FLOAT_EQ(zoomStep(1.3f, -1), 1.25f);
}

TEST_F(ScaleTest, ZoomStepStaysInRange) {
    EXPECT_FLOAT_EQ(zoomStep(maxZoom, 1), maxZoom);
    EXPECT_FLOAT_EQ(zoomStep(minZoom, -1), minZoom);
    EXPECT_FLOAT_EQ(zoomStep(1.0f, 100), maxZoom);
    EXPECT_FLOAT_EQ(zoomStep(1.0f, -100), minZoom);
    EXPECT_FLOAT_EQ(zoomStep(0.2f, 1), minZoom);
    EXPECT_FLOAT_EQ(zoomStep(0.2f, -1), minZoom);
}
