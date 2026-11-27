// Tests for the log wrapper (imlui.log): a file sink by a name outside the ANSI code page, a file that can't be
// opened reported without an exception, and messages at a level that's off never formatted (C++ and Lua).
#include <gtest/gtest.h>

import std;

import imlui.log;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

namespace fs = std::filesystem;

/// Counts how often std::format formats it.
struct Counted {
    int* formats;
};

/// Restores the default level (debug) however the test ends.
struct LevelGuard {
    explicit LevelGuard(log::Level level) { log::setLevel(level); }
    ~LevelGuard() { log::setLevel(log::Level::Debug); }
    LevelGuard(const LevelGuard&) = delete;
    LevelGuard& operator=(const LevelGuard&) = delete;
};

std::string contents(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

template <> struct std::formatter<Counted> : std::formatter<std::string_view> {
    auto format(const Counted& c, std::format_context& ctx) const {
        ++*c.formats;
        return std::formatter<std::string_view>::format("counted", ctx);
    }
};

TEST(Log, ADisabledLevelIsNotFormatted) {
    const LevelGuard level{log::Level::Info};
    EXPECT_FALSE(log::enabled(log::Level::Trace));
    EXPECT_FALSE(log::enabled(log::Level::Debug));
    EXPECT_TRUE(log::enabled(log::Level::Info));
    EXPECT_TRUE(log::enabled(log::Level::Error));
    int formats = 0;
    log::trace("{}", Counted{&formats});
    log::debug("{}", Counted{&formats});
    EXPECT_EQ(formats, 0);
    log::info("{}", Counted{&formats});
    EXPECT_EQ(formats, 1);
}

// log.* and print() in Lua: at a level that's off, neither string.format nor the arguments' __tostring runs, and
// native.log doesn't convert its message either.
TEST(Log, LuaSkipsDisabledLevelsBeforeFormatting) {
    test::HeadlessFrame frame;
    const LevelGuard level{log::Level::Warn};
    EXPECT_EQ(frame.run(R"(
        local calls = 0
        local loud = setmetatable({}, { __tostring = function() calls = calls + 1; return "loud" end })
        assert(log.enabled("debug") == false and log.enabled("info") == false and log.enabled("warn") == true)
        log.debug(loud)
        log.debug("%s", loud)
        log.info("%s %s", loud, loud)
        print(loud)
        require("imlui.native").log("info", loud)
        assert(calls == 0, "a disabled level formatted its message " .. calls .. " times")
        log.warn("%s", loud)
        assert(calls == 1, "an enabled level formats: " .. calls)
    )"),
              "");
}

// The sink stays open for the rest of the test process (each test runs in its own under ctest), so the file has a
// fixed name in the temp folder, replaced on every run, rather than a folder removed at the end.
TEST(Log, FileSinkTakesAnyFileName) {
    const fs::path file = fs::temp_directory_path() / fs::path{u8"imlui_log_test_журнал_日本.log"};
    std::error_code ec;
    fs::remove(file, ec);
    ASSERT_TRUE(log::addFileSink(file));
    log::warn("written to the file sink {}", 42);
    EXPECT_TRUE(fs::exists(file));
    EXPECT_NE(contents(file).find("written to the file sink 42"), std::string::npos);
}

TEST(Log, FileSinkThatCantBeOpenedIsReported) {
    const fs::path missing = fs::temp_directory_path() / "imlui_no_such_folder_for_logs" / "sub" / "x.log";
    bool added = true;
    EXPECT_NO_THROW(added = log::addFileSink(missing));
    EXPECT_FALSE(added);
    log::info("logging goes on");
}
