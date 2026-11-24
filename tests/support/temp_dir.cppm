// TempDir: a fresh folder under the system temp directory, removed with everything in it on destruction.
// Test-only (built into imlui_tests).
module;

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

export module imlui.test.temp_dir;

export namespace imlui::test {

class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const;
    /// Writes `content` to `relative` (a `/`-separated path), creating its folders.
    void write(std::string_view relative, std::string_view content) const;
    /// Moves the modification time of `relative` by `seconds` (deterministic, unlike waiting for the clock).
    void touch(std::string_view relative, int seconds) const;

private:
    std::filesystem::path m_path;
};

} // namespace imlui::test

module :private;

namespace imlui::test {

namespace fs = std::filesystem;

namespace {
std::atomic<int> counter{0};
} // namespace

TempDir::TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = fs::temp_directory_path() /
             ("imlui_test_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(m_path);
}

TempDir::~TempDir() {
    std::error_code ec;
    fs::remove_all(m_path, ec);
}

const fs::path& TempDir::path() const { return m_path; }

void TempDir::write(std::string_view relative, std::string_view content) const {
    const fs::path file = m_path / fs::path{std::u8string{relative.begin(), relative.end()}};
    fs::create_directories(file.parent_path());
    std::ofstream out{file, std::ios::binary | std::ios::trunc};
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void TempDir::touch(std::string_view relative, int seconds) const {
    const fs::path file = m_path / fs::path{std::u8string{relative.begin(), relative.end()}};
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds{seconds});
}

} // namespace imlui::test
