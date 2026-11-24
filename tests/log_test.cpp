// Tests for the log wrapper (imlui.log): a file sink by a name outside the ANSI code page, and a file that can't be
// opened reported without an exception.
#include <gtest/gtest.h>

import std;

import imlui.log;

using namespace imlui;

namespace {

namespace fs = std::filesystem;

std::string contents(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

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
