// Tests for imlui.ui.size_class: Material 3 window size classes.
#include <gtest/gtest.h>

import imlui.ui.size_class;

using imlui::SizeClass;
using imlui::sizeClassForWidth;
using imlui::sizeClassFromName;
using imlui::sizeClassName;

TEST(SizeClass, BoundariesFollowMaterial3) {
    EXPECT_EQ(sizeClassForWidth(0.0f), SizeClass::Compact);
    EXPECT_EQ(sizeClassForWidth(412.0f), SizeClass::Compact);
    EXPECT_EQ(sizeClassForWidth(599.9f), SizeClass::Compact);
    EXPECT_EQ(sizeClassForWidth(600.0f), SizeClass::Medium);
    EXPECT_EQ(sizeClassForWidth(839.9f), SizeClass::Medium);
    EXPECT_EQ(sizeClassForWidth(840.0f), SizeClass::Expanded);
    EXPECT_EQ(sizeClassForWidth(1199.9f), SizeClass::Expanded);
    EXPECT_EQ(sizeClassForWidth(1200.0f), SizeClass::Large);
    EXPECT_EQ(sizeClassForWidth(1599.9f), SizeClass::Large);
    EXPECT_EQ(sizeClassForWidth(1600.0f), SizeClass::ExtraLarge);
    EXPECT_EQ(sizeClassForWidth(5000.0f), SizeClass::ExtraLarge);
}

TEST(SizeClass, NegativeAndNaNWidthsAreCompact) {
    EXPECT_EQ(sizeClassForWidth(-10.0f), SizeClass::Compact);
    EXPECT_EQ(sizeClassForWidth(std::numeric_limits<float>::quiet_NaN()), SizeClass::Compact);
}

TEST(SizeClass, NamesRoundTrip) {
    EXPECT_EQ(sizeClassName(SizeClass::Compact), "compact");
    EXPECT_EQ(sizeClassName(SizeClass::Medium), "medium");
    EXPECT_EQ(sizeClassName(SizeClass::Expanded), "expanded");
    EXPECT_EQ(sizeClassName(SizeClass::Large), "large");
    EXPECT_EQ(sizeClassName(SizeClass::ExtraLarge), "extraLarge");
    for (SizeClass c :
         {SizeClass::Compact, SizeClass::Medium, SizeClass::Expanded, SizeClass::Large, SizeClass::ExtraLarge})
        EXPECT_EQ(sizeClassFromName(sizeClassName(c)), c);
    EXPECT_FALSE(sizeClassFromName("huge").has_value());
    EXPECT_FALSE(sizeClassFromName("Compact").has_value());
}
