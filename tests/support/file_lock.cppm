// FileLock: keeps a file from being opened by anyone else for one test, like an editor that holds a file without
// sharing it while it saves. Test-only (built into imlui_tests).
module;

#if defined(_WIN32)
// CreateFileW without sharing: the only way to make a file unopenable on Windows (permissions don't stop the owner).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

export module imlui.test.file_lock;

import std;

export namespace imlui::test {

class FileLock {
public:
    /// Locks `file`: on Windows an open handle that shares nothing, elsewhere no permissions (restored on
    /// destruction). locked() says whether that keeps it from opening (it doesn't for root).
    explicit FileLock(std::filesystem::path file);
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&&) = delete;
    FileLock& operator=(FileLock&&) = delete;

    /// True when the file can't be opened now.
    [[nodiscard]] bool locked() const;
    /// Lifts the lock early.
    void release();

private:
    std::filesystem::path m_file;
#if defined(_WIN32)
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    std::optional<std::filesystem::perms> m_perms;
#endif
};

} // namespace imlui::test

module :private;

namespace imlui::test {

namespace fs = std::filesystem;

FileLock::FileLock(fs::path file) : m_file{std::move(file)} {
#if defined(_WIN32)
    m_handle = CreateFileW(m_file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    std::error_code ec;
    const auto status = fs::status(m_file, ec);
    if (ec) return;
    m_perms = status.permissions();
    fs::permissions(m_file, fs::perms::none, ec);
#endif
}

FileLock::~FileLock() { release(); }

void FileLock::release() {
#if defined(_WIN32)
    if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
#else
    std::error_code ec;
    if (m_perms) fs::permissions(m_file, *m_perms, ec);
    m_perms.reset();
#endif
}

bool FileLock::locked() const {
    const std::ifstream in{m_file, std::ios::binary};
    return !in;
}

} // namespace imlui::test
