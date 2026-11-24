// DirectoryMount: a folder on disk as a resource mount (dev builds, loose overrides next to the executable).
module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

export module imlui.resources.directory_mount;

import imlui.resources.mount;
import imlui.resources.name;

export namespace imlui {

/// The files under a folder. Reads are cached per file and served from the cache while the file's time and size
/// are unchanged; a changed file is read again into a new buffer. Every buffer is kept until the mount is
/// destroyed, so spans returned earlier stay valid (data that must stay put, such as font data handed to ImGui,
/// can point straight into them). The folder's files are mutable, so modified() reports their times.
class DirectoryMount final : public Mount {
public:
    explicit DirectoryMount(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const;

    [[nodiscard]] std::optional<std::span<const std::byte>> read(std::string_view path) const override;
    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] std::vector<std::string> list(std::string_view path) const override;
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view path) const override;
    [[nodiscard]] std::string origin() const override;

private:
    struct Cached {
        ResourceTime time;
        std::uintmax_t size = 0;
        std::span<const std::byte> bytes;
    };

    /// The file system path of `path`, or nullopt when `path` isn't normalized.
    [[nodiscard]] std::optional<std::filesystem::path> file(std::string_view path) const;

    std::filesystem::path m_root;
    mutable std::mutex m_mutex;
    mutable std::map<std::string, Cached, std::less<>> m_cache;
    mutable std::deque<std::vector<std::byte>> m_buffers; ///< every buffer ever read; never shrinks
};

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

namespace {

/// A UTF-8 string as a path (on Windows a plain std::string would be read in the ANSI code page).
fs::path pathFromUtf8(std::string_view s) { return fs::path{std::u8string{s.begin(), s.end()}}; }

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string{s.begin(), s.end()};
}

} // namespace

DirectoryMount::DirectoryMount(fs::path root) : m_root{std::move(root)} {}

const fs::path& DirectoryMount::root() const { return m_root; }

std::optional<fs::path> DirectoryMount::file(std::string_view path) const {
    const auto normalized = normalizePath(path);
    if (!normalized || *normalized != path) return std::nullopt;
    return path.empty() ? m_root : m_root / pathFromUtf8(path);
}

std::optional<std::span<const std::byte>> DirectoryMount::read(std::string_view path) const {
    const auto filePath = file(path);
    if (!filePath) return std::nullopt;
    std::error_code ec;
    if (!fs::is_regular_file(*filePath, ec)) return std::nullopt;
    const ResourceTime time = fs::last_write_time(*filePath, ec);
    const std::uintmax_t size = fs::file_size(*filePath, ec);
    if (ec) return std::nullopt;

    std::scoped_lock lock{m_mutex};
    if (auto it = m_cache.find(path); it != m_cache.end() && it->second.time == time && it->second.size == size)
        return it->second.bytes;

    std::ifstream in{*filePath, std::ios::binary};
    if (!in) return std::nullopt;
    std::vector<std::byte> buffer(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    buffer.resize(static_cast<std::size_t>(in.gcount())); // the file may have shrunk since file_size()
    const std::span<const std::byte> bytes = m_buffers.emplace_back(std::move(buffer));
    m_cache.insert_or_assign(std::string{path}, Cached{time, size, bytes});
    return bytes;
}

bool DirectoryMount::exists(std::string_view path) const {
    const auto filePath = file(path);
    std::error_code ec;
    return filePath && fs::exists(*filePath, ec);
}

std::vector<std::string> DirectoryMount::list(std::string_view path) const {
    std::vector<std::string> out;
    const auto filePath = file(path);
    std::error_code ec;
    if (!filePath || !fs::is_directory(*filePath, ec)) return out;
    for (auto it = fs::directory_iterator(*filePath, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::string name = utf8(it->path().filename());
        if (it->is_directory(ec))
            name += '/';
        else if (!it->is_regular_file(ec))
            continue;
        out.push_back(std::move(name));
    }
    std::ranges::sort(out);
    return out;
}

std::optional<ResourceTime> DirectoryMount::modified(std::string_view path) const {
    const auto filePath = file(path);
    std::error_code ec;
    if (!filePath || !fs::is_regular_file(*filePath, ec)) return std::nullopt;
    const ResourceTime time = fs::last_write_time(*filePath, ec);
    if (ec) return std::nullopt;
    return time;
}

std::string DirectoryMount::origin() const { return "directory '" + utf8(m_root) + "'"; }

} // namespace imlui
