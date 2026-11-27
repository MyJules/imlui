// Tests for the hot reload's change rule (imlui.app.file_snapshot, resourcesChanged): what the Engine reloads for
// when it compares two scans of the watched resources.
#include <gtest/gtest.h>

import std;

import imlui.app.file_snapshot;
import imlui.resources.mount;

using namespace imlui;

namespace {

using namespace std::chrono_literals;

/// A fixed point in time, and times around it.
const ResourceTime t0 = ResourceTime{} + 1000h;

} // namespace

TEST(FileSnapshot, AChangedTimeOrARemovedScriptIsAChange) {
    const FileSnapshot before{{"app:/main.lua", t0}, {"app:/data.txt", t0}};
    const std::set<std::string> loaded{"app:/main.lua", "app:/data.txt"};
    EXPECT_FALSE(resourcesChanged(before, before, loaded, t0 + 1s));
    EXPECT_TRUE(resourcesChanged(before, {{"app:/main.lua", t0 + 1s}, {"app:/data.txt", t0}}, loaded, t0 + 1s));
    EXPECT_TRUE(resourcesChanged(before, {{"app:/main.lua", t0}, {"app:/data.txt", std::nullopt}}, loaded, t0 + 1s));
    EXPECT_TRUE(resourcesChanged(before, {{"app:/data.txt", t0}}, loaded, t0 + 1s));
}

TEST(FileSnapshot, ANewScriptFileIsAChange) {
    const FileSnapshot before{{"app:/main.lua", t0}};
    EXPECT_TRUE(resourcesChanged(before, {{"app:/main.lua", t0}, {"app:/New.lua", t0}}, {"app:/main.lua"}, t0 + 1s));
}

// A name the script stopped reading (a reload rebuilt the watched names without it) is no longer watched: dropping
// it from the scan isn't a change, or every such reload would be followed by another.
TEST(FileSnapshot, ANameNoLongerReadIsNotAChange) {
    const FileSnapshot before{{"app:/main.lua", t0}, {"app:/old.txt", t0}};
    EXPECT_FALSE(resourcesChanged(before, {{"app:/main.lua", t0}}, {"app:/main.lua"}, t0 + 1s));
}

// A name read since the last scan, with nothing known about when: unchanged when its time is before that scan (the
// read got that version), a change when it's at or after it (an edit that may have come after the read). That is the
// file first read while the script reloads and edited before the next scan, which used to be missed.
TEST(FileSnapshot, ANameReadSinceTheLastScanCountsWhenItsTimeIsLater) {
    const FileSnapshot before{{"app:/main.lua", t0}};
    const std::set<std::string> loaded{"app:/main.lua", "app:/data.txt"};
    const ResourceTime lastScan = t0 + 10s;
    EXPECT_FALSE(resourcesChanged(before, {{"app:/main.lua", t0}, {"app:/data.txt", t0}}, loaded, lastScan));
    EXPECT_TRUE(resourcesChanged(before, {{"app:/main.lua", t0}, {"app:/data.txt", lastScan}}, loaded, lastScan));
    EXPECT_TRUE(resourcesChanged(before, {{"app:/main.lua", t0}, {"app:/data.txt", lastScan + 1s}}, loaded, lastScan));
    // Embedded (no time) or missing: nothing to compare.
    EXPECT_FALSE(resourcesChanged(before, {{"app:/main.lua", t0}, {"app:/data.txt", std::nullopt}}, loaded, lastScan));
}
