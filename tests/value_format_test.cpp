// Tests for imlui.widgets.value_format (the Slider's `format` prop validation).
#include <gtest/gtest.h>

import imlui.widgets.value_format;

using imlui::md::isValidFloatFormat;

TEST(ValueFormat, AcceptsFloatingConversions) {
    EXPECT_TRUE(isValidFloatFormat("%f"));
    EXPECT_TRUE(isValidFloatFormat("%.2f"));
    EXPECT_TRUE(isValidFloatFormat("%.0f"));
    EXPECT_TRUE(isValidFloatFormat("%e"));
    EXPECT_TRUE(isValidFloatFormat("%.3E"));
    EXPECT_TRUE(isValidFloatFormat("%g"));
    EXPECT_TRUE(isValidFloatFormat("%G"));
    EXPECT_TRUE(isValidFloatFormat("%F"));
}

TEST(ValueFormat, AcceptsFlagsWidthPrecisionAndText) {
    EXPECT_TRUE(isValidFloatFormat("%+.1f"));
    EXPECT_TRUE(isValidFloatFormat("%-8.2f"));
    EXPECT_TRUE(isValidFloatFormat("%08.3f"));
    EXPECT_TRUE(isValidFloatFormat("% #g"));
    EXPECT_TRUE(isValidFloatFormat("%10f"));
    EXPECT_TRUE(isValidFloatFormat("%.f"));
    EXPECT_TRUE(isValidFloatFormat("Volume: %.0f dB"));
}

TEST(ValueFormat, AcceptsEscapedPercentAnywhere) {
    EXPECT_TRUE(isValidFloatFormat("%.0f%%"));
    EXPECT_TRUE(isValidFloatFormat("%%%.1f"));
    EXPECT_TRUE(isValidFloatFormat("%% %g %%"));
}

TEST(ValueFormat, RejectsNoConversion) {
    EXPECT_FALSE(isValidFloatFormat(""));
    EXPECT_FALSE(isValidFloatFormat("value"));
    EXPECT_FALSE(isValidFloatFormat("100%%"));
}

TEST(ValueFormat, RejectsMoreThanOneConversion) {
    EXPECT_FALSE(isValidFloatFormat("%f %f"));
    EXPECT_FALSE(isValidFloatFormat("%.1f/%.1g"));
}

TEST(ValueFormat, RejectsOtherConversions) {
    EXPECT_FALSE(isValidFloatFormat("%d"));
    EXPECT_FALSE(isValidFloatFormat("%i"));
    EXPECT_FALSE(isValidFloatFormat("%s"));
    EXPECT_FALSE(isValidFloatFormat("%n"));
    EXPECT_FALSE(isValidFloatFormat("%p"));
    EXPECT_FALSE(isValidFloatFormat("%x"));
    EXPECT_FALSE(isValidFloatFormat("%c"));
    EXPECT_FALSE(isValidFloatFormat("%a"));
    EXPECT_FALSE(isValidFloatFormat("%.2f %s"));
    EXPECT_FALSE(isValidFloatFormat("%s %.2f"));
}

TEST(ValueFormat, RejectsStarsLengthModifiersAndBrokenSpecs) {
    EXPECT_FALSE(isValidFloatFormat("%*f"));
    EXPECT_FALSE(isValidFloatFormat("%.*f"));
    EXPECT_FALSE(isValidFloatFormat("%Lf"));
    EXPECT_FALSE(isValidFloatFormat("%lf"));
    EXPECT_FALSE(isValidFloatFormat("%1$f"));
    EXPECT_FALSE(isValidFloatFormat("%.2"));
    EXPECT_FALSE(isValidFloatFormat("%"));
    EXPECT_FALSE(isValidFloatFormat("%.2f%"));
    EXPECT_FALSE(isValidFloatFormat("%.1.2f"));
}

TEST(ValueFormat, RejectsHugeWidthOrPrecision) {
    EXPECT_TRUE(isValidFloatFormat("%99.99f"));
    EXPECT_FALSE(isValidFloatFormat("%100f"));
    EXPECT_FALSE(isValidFloatFormat("%.100f"));
    EXPECT_FALSE(isValidFloatFormat("%.99999999999f"));
}
