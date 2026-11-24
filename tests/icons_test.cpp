// Tests for imlui.theme.icons: the name -> codepoint table read from the `.codepoints` file, and iconText()'s
// UTF-8 for every length (codepoints past U+FFFF take four bytes).
#include <gtest/gtest.h>

import std;

import imlui.resources;
import imlui.resources.mount;
import imlui.theme.icons;
import imlui.test.memory_mount;

using namespace imlui;

TEST(Icons, IconTextIsUtf8ForEveryCodepointLength) {
    Resources resources; // the codepoints alone: without the font, loadIcons() needs no ImGui context
    ASSERT_TRUE(resources.mount(
        "imlui:/fonts/",
        std::make_shared<test::MemoryMount>(std::initializer_list<std::pair<const std::string, std::string>>{
            {"MaterialIconsRound-Regular.codepoints",
             "one 41\ntwo 7ff\nthree e88a\nfour f0000\nlast 10ffff\nbeyond 110000\n"}})));
    loadIcons(resources);
    EXPECT_EQ(iconText("one"), "A");
    EXPECT_EQ(iconText("two"), "\xDF\xBF");
    EXPECT_EQ(iconText("three"), "\xEE\xA2\x8A");
    EXPECT_EQ(iconText("four"), "\xF3\xB0\x80\x80");
    EXPECT_EQ(iconText("last"), "\xF4\x8F\xBF\xBF");
    EXPECT_EQ(iconText("beyond"), ""); // not a Unicode codepoint
    EXPECT_EQ(iconText("missing"), "");
    EXPECT_TRUE(hasIcon("four"));

    loadIcons(Resources{}); // back to no icons, as before any harness loaded them
    EXPECT_FALSE(hasIcon("four"));
}
