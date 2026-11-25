// TempDir: a fresh folder under the system temp directory, removed with everything in it on destruction.
// Test-only (built into imlui_tests).
export module imlui.test.temp_dir;

import std;

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
    /// Deletes file `relative`.
    void remove(std::string_view relative) const;
    /// Makes `link` a link to folder `target` (both relative; `target` needn't exist): a symbolic link, or on
    /// Windows, where those need a privilege, a directory junction. False when neither can be made (tests skip then).
    [[nodiscard]] bool linkFolder(std::string_view link, std::string_view target) const;

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

void TempDir::remove(std::string_view relative) const {
    fs::remove(m_path / fs::path{std::u8string{relative.begin(), relative.end()}});
}

bool TempDir::linkFolder(std::string_view link, std::string_view target) const {
    fs::path from = m_path / fs::path{std::u8string{link.begin(), link.end()}};
    fs::path to = m_path / fs::path{std::u8string{target.begin(), target.end()}};
    std::error_code ec;
    fs::create_directory_symlink(to, from, ec);
    if (!ec) return true;
#ifdef _WIN32
    // A junction needs no privilege; `mklink` is a cmd built-in. The temp folder's path is plain enough to quote.
    const std::string command = "cmd /c mklink /J \"" + from.make_preferred().string() + "\" \"" +
                                to.make_preferred().string() + "\" >nul 2>&1";
    return std::system(command.c_str()) == 0 && fs::exists(fs::symlink_status(from, ec));
#else
    return false;
#endif
}

} // namespace imlui::test
